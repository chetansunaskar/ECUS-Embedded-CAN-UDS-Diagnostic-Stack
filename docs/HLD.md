# High-Level Design (HLD)

## 1. Purpose and Scope

ECUS ("Embedded CAN-UDS Simulator") implements a **UDS (ISO 14229-1)
diagnostic server** running over a simulated **ISO-TP (ISO 15765-2)**
transport, on top of a **virtual CAN bus**. It represents the diagnostic
subsystem of a real automotive ECU (Engine Control Unit / Body Control
Module / etc.), built as host-executable C so it can be built, run, and
debugged without any physical hardware.

This document describes the system at the architecture level: layering,
major components, data flow, and the rationale behind key design choices.
For per-module APIs and state-machine detail, see [`LLD.md`](LLD.md).

## 2. System Context

```
                         ┌───────────────────────┐
                         │   Tester / CLI        │
                         │  (ShellCli, or a      │
                         │   real diagnostic     │
                         │   tool in production) │
                         └──────────┬────────────┘
                                    │ UDS requests (raw PDU bytes)
                                    ▼
                         ┌───────────────────────┐
                         │   ECUS  (this repo)   │
                         │   simulated ECU       │
                         └──────────┬────────────┘
                                    │ CAN frames (11-bit ID, ≤8 bytes)
                                    ▼
                         ┌─────────────────────────┐
                         │  Virtual CAN Bus        │
                         │  (loopback, in-process) │
                         └─────────────────────────┘
```

In production, the tester would be a real diagnostic tool (Vector CANoe,
a J2534 pass-thru device, or a garage OBD-II scanner) connected via a
physical CAN transceiver. ECUS replaces the physical bus with an
in-process loopback so the entire protocol stack can be exercised and
debugged identically to production, without hardware.

## 3. Layered Architecture

ECUS follows a strict four-layer architecture with one cross-cutting
services layer, mirroring how real AUTOSAR-style ECU software is
organised (Application → Diagnostic stack → Transport → HAL):

| Layer | Responsibility | Key modules |
|---|---|---|
| **Application** | CLI shell, ECU bootstrap/composition root, program entry point | `Main.c`, `ShellCli.c`, `EcuSession.c` |
| **Diagnostic stack (UDS)** | Service dispatch, session management, security access, DTC storage | `UdsServer.c`, `UdsDispatcher.c` |
| **Transport** | ISO-TP segmentation/reassembly, CAN frame routing, ring buffering | `IsoTp.c`, `RingBuffer.c`, `CanFrame.h` |
| **Platform (HAL)** | Virtual CAN hardware abstraction | `CanHal.c` |
| **Services** *(cross-cutting)* | Logging, CRC, error handling, endian utils, memory pool, signal handling | `Logger.c`, `CrcEngine.c`, `ErrorHandler.c`, `EndianUtils.h`, `MemPool.c`, `SignalHandler.c` |

**Dependency rule**: each layer depends only on the layer(s) below it and
on the Services layer. The Application layer never reaches into Transport
directly — all diagnostic traffic flows through the UDS layer. This
mirrors the real-world separation between a diagnostic tester application
and the underlying communication stack, and it's what lets the UDS layer
be unit-tested independently of the CLI.

## 4. Major Components

### 4.1 Virtual CAN HAL (`CanHal`)

Simulates a CAN peripheral using two lock-free ring buffers (Tx, Rx) and
a background POSIX thread that plays the role of a hardware CAN
interrupt handler: it drains the Tx queue at ~1ms cadence, and in
**loopback mode** echoes transmitted frames back into the Rx queue —
letting the whole stack run without a physical bus. In **normal mode**,
external test code can inject frames via `CanHal_InjectRxFrame()` to
simulate a real tester sending requests.

### 4.2 ISO-TP Transport (`IsoTp`)

Implements ISO 15765-2 segmentation and reassembly as two independent
state machines (Rx and Tx) per logical channel:

- **Rx state machine**: `IDLE → RECEIVING → COMPLETE`, driven by
  incoming Single/First/Consecutive Frames, with a `Cr` timeout guard.
- **Tx state machine**: `IDLE → WAIT_FC → SENDING_CF → COMPLETE`, honouring
  the Flow Control block-size (BS) and separation-time (STmin) parameters
  returned by the receiver.

Full detail in [`architecture/StateMachines.md`](architecture/StateMachines.md).

### 4.3 UDS Server & Dispatcher (`UdsServer`, `UdsDispatcher`)

`UdsServer` owns the per-ECU diagnostic state (active session, security
state, DTC table, DID table) and wires the ISO-TP channel's PDU-complete
callback to `UdsDispatcher_Dispatch()`.

`UdsDispatcher` uses a **declarative dispatch table**
(`ServiceEntry[]`) mapping each Service ID to: a handler function
pointer, a bitmask of sessions in which the service is legal, and whether
security unlock is required. This is the same pattern used by real UDS
stack generators (e.g. Vector's CANdelaStudio output) — adding a new
service is a one-line table entry, not a new `if`/`switch` branch.

### 4.4 Cross-cutting Services

- **Logger**: five-level (`TRACE`…`ERROR`), thread-safe (mutex-protected),
  variadic (`printf`-style), with optional file output and ANSI colour.
- **CrcEngine**: table-driven CRC-8/16/32 with both one-shot and
  streaming (chunked) APIs — used conceptually the way a bootloader would
  verify firmware image integrity.
- **ErrorHandler**: a single `EcusStatus` enum spans the whole codebase
  (ranges reserved per layer), with `ECUS_ASSERT`/`ECUS_CHECK` macros for
  consistent defensive programming.
- **MemPool**: fixed-block pool allocator — zero heap usage in any hot
  path, O(1) alloc/free, canary-poisoned on free to catch use-after-free.

## 5. Data Flow — A Complete Request/Response Cycle

Walking through `session 3` (DiagnosticSessionControl → Extended) typed
into the CLI:

1. `ShellCli` builds a raw UDS PDU `[0x10, 0x03]` and calls
   `InjectUdsPdu()`, which frames it as a Single Frame CAN message and
   calls `CanHal_InjectRxFrame()`.
2. The CAN HAL's ISR-simulator thread dequeues the frame from the Rx
   ring buffer and invokes the registered Rx callback
   (`OnCanFrameReceived` in `EcuSession.c`).
3. That callback forwards the frame to `UdsServer_ProcessCanFrame()`,
   which hands it to `IsoTp_ProcessRxFrame()`.
4. ISO-TP recognises the Single-Frame PCI, copies the 2-byte payload into
   its reassembly buffer, and — since the message is complete in one
   frame — immediately invokes the PDU-complete callback
   (`OnPduReceived` in `UdsServer.c`).
5. `OnPduReceived` calls `UdsDispatcher_Dispatch()`, which looks up SID
   `0x10` in the service table, confirms the request is legal in the
   current session, and invokes `Handle_DiagnosticSessionControl()`.
6. The handler updates `server->activeSession`, builds a positive
   response `[0x50, 0x03, 0x00, 0x19, 0x01, 0xF4]`, and returns it.
7. The dispatcher calls `IsoTp_Transmit()` to send the response — since
   it's ≤7 bytes, it goes out as a Single Frame via `CanHal_Transmit()`.
8. The ISR-simulator thread (loopback mode) echoes this frame back to Rx,
   completing the round trip exactly as it would over a physical bus.

## 6. Non-Functional Characteristics

| Aspect | Approach |
|---|---|
| **Determinism** | No dynamic allocation in any diagnostic hot path (`MemPool` for pooled allocation where needed); ring buffers are fixed-capacity. |
| **Concurrency safety** | SPSC ring buffers use C11 atomics with explicit acquire/release ordering; Logger uses a POSIX mutex; UDS server state is single-threaded by design (only the ISR-sim thread and the tick thread touch it, both serialised through the ring buffer boundary). |
| **Defensive programming** | Every public API validates parameters via `ECUS_CHECK`; `ECUS_ASSERT` guards internal invariants; buffer writes are bounds-checked before every `memcpy`. |
| **Portability** | Pure C11 + POSIX (`pthread`, `sigaction`, `clock_gettime`); compiler-attribute macros degrade gracefully on non-GCC/Clang toolchains. |
| **Testability** | Every layer can be exercised independently (see `tests/unit/`) or as a full stack (see `tests/integration/`), because the UDS/Transport/HAL boundaries are real function-call boundaries, not compile-time `#ifdef` seams. |

## 7. What This Project Deliberately Does *Not* Do

To keep scope honest for a portfolio project:

- No real CAN hardware driver (SocketCAN, FDCAN registers) — the HAL is
  explicitly virtual; swapping in a real driver would only require
  reimplementing `CanHal.c` behind the same header.
- No full UDS service catalogue — a representative, realistic subset
  (session, security, DID read/write, DTC, tester-present) is
  implemented deeply rather than a shallow stub of all ~20 services.
- No AUTOSAR RTE/BSW layering — this targets recruiters evaluating raw C
  systems skills, not AUTOSAR tooling experience specifically.


