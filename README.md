# ECUS — Embedded CAN-UDS Diagnostic Stack

[![Build](https://img.shields.io/badge/build-passing-brightgreen)]()
[![Tests](https://img.shields.io/badge/tests-43%2F43_passing-brightgreen)]()
[![Sanitizers](https://img.shields.io/badge/ASan%2FUBSan-clean-brightgreen)]()
[![Standard](https://img.shields.io/badge/C-C11-blue)]()
[![License](https://img.shields.io/badge/license-MIT-lightgrey)]()

A production-quality, host-buildable implementation of an automotive ECU
diagnostic stack in pure ANSI C (C11), demonstrating **ISO 14229 (UDS)**
and **ISO 15765-2 (ISO-TP)** running over a simulated CAN bus — with no
hardware, no RTOS, and no external dependencies beyond the POSIX/GCC
toolchain.

This project is not a toy CRUD app. It is a from-scratch protocol stack —
the same class of software that ships inside real ECUs -built to demonstrate systems-level C
proficiency: state machines, lock-free concurrency, memory pools, CRC
engines, and defensive programming, all wired into a working diagnostic
session simulator you can drive from a CLI.

```
ecus> session 3
ecus> seed
ecus> unlock 5B9280C3
ecus> rdid F190
ecus> rdtc
```

---

## Table of Contents

- [Why this project](#why-this-project)
- [Architecture](#architecture)
- [Features](#features)
- [C concepts demonstrated](#c-concepts-demonstrated)
- [Getting started](#getting-started)
- [CLI usage](#cli-usage)
- [Project layout](#project-layout)
- [Testing](#testing)
- [Static analysis & MISRA-C](#static-analysis--misra-c)
- [Documentation](#documentation)
- [Design decisions](#design-decisions)
- [Roadmap](#roadmap)
- [License](#license)

---

## Why this project

Most "embedded" portfolio projects are either firmware tied to a specific
board (unreviewable without hardware) or thin wrappers around a database.
ECUS solves both problems:

- **Runs anywhere** — pure C11 + POSIX, compiles with `gcc`/`clang`,
  debuggable with `gdb`/VS Code, no board required.
- **Real protocol depth** — implements the actual ISO-TP segmentation
  state machine and a UDS service dispatcher with session/security gating,
  not a mocked-up simplification.
- **Verified correctness** — 43 unit + integration tests, zero warnings
  under `-Wall -Wextra -Wshadow -Wconversion`, zero findings under
  AddressSanitizer + UndefinedBehaviorSanitizer.

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│  Application Layer   CLI shell · ECU session bootstrap       │
├──────────────────────────────────────────────────────────────┤
│  UDS Stack (ISO 14229)                                       │
│  Dispatch table · Session FSM · Security Access · DTC mgr    │
├──────────────────────────────────────────────────────────────┤
│  Transport Layer (ISO 15765-2)                               │
│  ISO-TP segmentation/reassembly · CAN frame router           │
├──────────────────────────────────────────────────────────────┤
│  Platform Abstraction Layer (HAL)                            │
│  Virtual CAN HAL (POSIX-thread ISR simulator) · Timer HAL    │
├──────────────────────────────────────────────────────────────┤
│  Cross-cutting Services                                      │
│  Logger · CRC engine · Error handler · Endian utils          │
│  Memory pool · Signal handler                                │
└──────────────────────────────────────────────────────────────┘
```

Full details: [`docs/HLD.md`](docs/HLD.md), [`docs/LLD.md`](docs/LLD.md),
[`docs/architecture/SystemArch.md`](docs/architecture/SystemArch.md).

## Features

- **ISO-TP transport** (ISO 15765-2): Single Frame, First Frame,
  Consecutive Frame, Flow Control — full Rx reassembly and Tx segmentation
  state machines with block-size and STmin pacing.
- **UDS services** (ISO 14229): `0x10` DiagnosticSessionControl,
  `0x11` ECUReset, `0x27` SecurityAccess (seed/key), `0x22`/`0x2E`
  Read/WriteDataByIdentifier, `0x14`/`0x19` Clear/ReadDTCInformation,
  `0x3E` TesterPresent.
- **Session & security gating**: services are permitted per-session
  (Default/Programming/Extended) and per-security-state via a declarative
  dispatch table — no `if`/`else` chains.
- **Virtual CAN bus**: a POSIX-thread "ISR simulator" drains Tx frames and
  loops them back to Rx, so the entire stack runs without hardware.
- **Interactive CLI**: inject raw or pre-built UDS requests, unlock
  security, read/write DIDs, manage DTCs — from a shell or a batch script.
- **Lock-free ring buffer**: SPSC queue with C11 atomics for the CAN HAL's
  Tx/Rx FIFOs.
- **Fixed-size memory pool**: O(1) alloc/free with canary poisoning,
  zero dynamic allocation in the hot path (MISRA-C Rule 21.3 friendly).
- **Table-driven CRC engine**: CRC-8 (SAE J1850), CRC-16 (CCITT), CRC-32
  (ISO/Ethernet), both one-shot and streaming APIs.

## C concepts demonstrated

| Category | Where |
|---|---|
| Function pointers & callbacks | `UdsServiceHandlerFn`, `CanRxCallbackFn`, `IsoTpPduCallbackFn` |
| Dispatch tables | `UdsDispatcher.c` — `k_serviceTable[]` |
| State machines | `IsoTp.c` (Rx/Tx FSMs), `UdsDispatcher.c` (session FSM) |
| Bit-fields & unions | `CanFrame.h` (`CanId`), `UdsTypes.h` (`DtcStatusMask`) |
| Pointer arithmetic | `CrcEngine.c`, `MemPool.c`, `Logger.c` (hex dump) |
| Dynamic memory alternatives | `MemPool.c` — fixed-block pool, zero `malloc()` in hot paths |
| C11 atomics | `RingBuffer.c`, `CanHal.c` stats counters |
| POSIX threads | `CanHal.c` (ISR sim), `EcuSession.c` (tick), `Logger.c` (mutex) |
| Variadic functions | `Logger_Write()` (`va_list`) |
| Signal handling | `SignalHandler.c` (`sig_atomic_t`, `sigaction`) |
| Recursion | `CrcEngine.c` — `ReflectBits()` |
| Compiler attributes | `Platform.h` — `noreturn`, `pure`, `warn_unused_result`, `packed` |
| `const` / `volatile` / `restrict` | throughout; see `EndianUtils.h`, `CrcEngine.h` |
| Endianness handling | `EndianUtils.h` — union trick, `__builtin_bswap*` |
| Safe string handling | `snprintf`/`strncpy` patterns, never raw `strcpy`/`sprintf` |
| File I/O & CLI args | `Main.c` — batch script reading, `argv` parsing |

## Getting started

### Prerequisites

- GCC ≥ 9 or Clang ≥ 10 (C11 support, POSIX threads)
- GNU Make
- Linux or macOS (POSIX `pthread`, `sigaction`, `clock_gettime`)
- *(optional)* `cppcheck` for static analysis, `doxygen` for API docs

### Build

```bash
make
```

```
  ✓ Build complete: build/ecus
  Run with: ./build/ecus
```

### Run

```bash
./build/ecus -v -c
```

```
  ███████╗ ██████╗██╗   ██╗███████╗
  ██╔════╝██╔════╝██║   ██║██╔════╝
  █████╗  ██║     ██║   ██║███████╗
  ██╔══╝  ██║     ██║   ██║╚════██║
  ███████╗╚██████╗╚██████╔╝███████║
  ╚══════╝ ╚═════╝ ╚═════╝ ╚══════╝
  Embedded CAN-UDS Diagnostic Stack  v1.0.0

ECUS Simulator ready.  Type 'help' for commands.
ecus>
```

### Debug build (ASan + UBSan)

```bash
make debug
gdb ./build/ecus
```

### Run tests

```bash
make test
```

```
  Results: 43 run, 43 passed, 0 failed
  ✓ ALL TESTS PASSED
```

## CLI usage

| Command | Description |
|---|---|
| `status` | Print ECU session state, security state, DTCs, DIDs, HAL stats |
| `session <1\|2\|3>` | Send `DiagnosticSessionControl` (Default/Programming/Extended) |
| `reset <1\|2\|3>` | Send `ECUReset` (hard/key-off-on/soft) |
| `seed` | Request a `SecurityAccess` seed |
| `unlock <seed_hex>` | Compute and send the correct key for a given seed |
| `rdid <DID_hex>` | `ReadDataByIdentifier` |
| `rdtc` | `ReadDTCInformation` (all stored DTCs) |
| `clrdtc` | `ClearDiagnosticInformation` (all) |
| `testerp` | Send `TesterPresent` (resets the S3 session timer) |
| `raw <b0> [b1..]` | Inject an arbitrary raw UDS PDU (hex bytes) |

Batch mode runs a script of these commands non-interactively:

```bash
./build/ecus -b demo_script.txt
```

See [`docs/api/API_Reference.md`](docs/API_Reference.md) for the full
walk-through with sample outputs and error scenarios.

## Project layout

```
ecus/
├── src/            Implementation (.c) — mirrors include/ structure
│   ├── app/           CLI shell, ECU bootstrap, entry point
│   ├── uds/            UDS server + service dispatcher
│   ├── transport/       ISO-TP state machine, ring buffer
│   ├── hal/             Virtual CAN HAL
│   └── services/         Logger, CRC, error handler, mem pool, ...
├── include/        Public headers, same structure as src/
├── tests/
│   ├── unit/           Per-module unit tests
│   ├── integration/     Full-stack scenario tests
│   └── framework/        Minimal from-scratch test runner
├── docs/            HLD, LLD, API reference, diagrams
├── Makefile
└── README.md        (this file)
```

## Testing

- **43 tests** across unit (CRC, RingBuffer, MemPool, ISO-TP, UDS
  dispatcher) and integration (full DTC lifecycle, session+security+write
  end-to-end flow) layers.
- **Zero ASan/UBSan findings** — verified under
  `-fsanitize=address,undefined`.
- **Zero memory leaks** — verified with `ASAN_OPTIONS=detect_leaks=1`.
- See [Unit Testing Strategy](docs/LLD.md#unit-testing-strategy) in the LLD
  for the philosophy behind test organisation and fixture design.

## Static analysis & MISRA-C

```bash
make lint
```

Runs `cppcheck --enable=all` across the entire `src/` tree. The codebase
is written with MISRA-C:2012 guidelines in mind (see `MISRA_DEVIATE()`
annotations in source for documented, justified deviations — e.g. the
union-based endianness probe, or the GNU `##__VA_ARGS__` extension used
in the logging macros).

## Documentation

| Document | Contents |
|---|---|
| [`docs/HLD.md`](docs/HLD.md) | High-level design, layering, data flow |
| [`docs/LLD.md`](docs/LLD.md) | Low-level design, module APIs, state machines |
| [`docs/API_Reference.md`](docs/API_Reference.md) | CLI walkthrough, sample outputs, error scenarios |
| [`docs/architecture/SystemArch.md`](docs/architecture/SystemArch.md) | Full architecture rationale |
| [`docs/architecture/MemoryLayout.md`](docs/architecture/MemoryLayout.md) | Memory layout & footprint analysis |
| [`docs/architecture/StateMachines.md`](docs/architecture/StateMachines.md) | ISO-TP and UDS session FSMs |


Doxygen-ready comments are present on every public function; generate
browsable HTML docs with:

```bash
make docs   # requires doxygen; outputs to docs/doxygen/html/
```

## Design decisions

A few decisions are worth calling out explicitly (more detail in the LLD):

- **Why a virtual CAN HAL instead of SocketCAN?** SocketCAN would tie the
  project to Linux and require `sudo`/`vcan0` setup, hurting reviewer
  experience. The virtual HAL keeps the entire stack — including the
  "interrupt" timing behaviour — portable and buildable in one command.
- **Why a dispatch table instead of a `switch` in the UDS layer?** Adding
  a new service becomes a one-line table entry instead of touching a
  growing `switch`. It also makes session/security gating declarative and
  auditable at a glance — closer to how production UDS stacks (e.g.
  Vector's CANoe/CANdelaStudio-generated code) are structured.
- **Why a custom memory pool instead of `malloc`?** MISRA-C:2012 Rule 21.3
  disallows dynamic memory in safety-critical code; a fixed-block pool
  gives O(1), deterministic, fragmentation-free allocation — the pattern
  used in real AUTOSAR-style ECU software.


## License

MIT — see [`LICENSE`](LICENSE).

---

<p align="center">Built as a demonstration of embedded systems C programming for enhancing the concept of C.</p>


## Prepared By 

  Chetan S Sunaskar (Embedded Engineer)