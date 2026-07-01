# State Machines

This document details the two core state machines in ECUS. Diagrams are
written in [Mermaid](https://mermaid.js.org/) syntax, which GitHub renders
natively in the web UI.

## 1. ISO-TP Rx Reassembly State Machine

Implemented in `src/transport/IsoTp.c`, driven by `IsoTp_ProcessRxFrame()`.
One instance per `IsoTpChannel`.

```mermaid
stateDiagram-v2
    [*] --> IDLE

    IDLE --> COMPLETE: Single Frame received
    IDLE --> RECEIVING: First Frame received\n(send Flow Control CTS)

    RECEIVING --> RECEIVING: Consecutive Frame\n(SN matches, more data pending)
    RECEIVING --> COMPLETE: Consecutive Frame\n(SN matches, final chunk)
    RECEIVING --> ERROR: Consecutive Frame\n(SN mismatch)
    RECEIVING --> ERROR: First Frame while already\nRECEIVING (protocol violation)

    COMPLETE --> IDLE: PDU delivered to callback

    ERROR --> IDLE: IsoTp_Reset() called

    note right of RECEIVING
        rxNextSN cycles 0x1..0xF
        (never 0x0), wrapping F→1
    end note

    note right of ERROR
        Bounds-checked before every
        write into rxBuf — a malicious
        FF length claim cannot overflow
    end note
```

### Rx frame handling summary

| PCI byte (upper nibble) | Frame type | Handler | Effect |
|---|---|---|---|
| `0x0N` | Single Frame | `HandleSingleFrame` | Copies N bytes, delivers PDU immediately |
| `0x1N` | First Frame | `HandleFirstFrame` | Extracts 12-bit length, copies first 6 bytes, sends FC |
| `0x2N` | Consecutive Frame | `HandleConsecutiveFrame` | Validates SN, appends up to 7 bytes, checks completion |
| `0x3N` | Flow Control | `HandleFlowControl` | Consumed by the **Tx** state machine, not Rx |

## 2. ISO-TP Tx Segmentation State Machine

Driven by `IsoTp_Transmit()` (entry) and `IsoTp_TxPump()` (periodic, called
every tick from `UdsServer_Tick()`).

```mermaid
stateDiagram-v2
    [*] --> IDLE

    IDLE --> COMPLETE: payload ≤ 7 bytes\n(Single Frame sent immediately)
    IDLE --> WAIT_FC: payload > 7 bytes\n(First Frame sent)

    WAIT_FC --> SENDING_CF: Flow Control\nFS = CTS received
    WAIT_FC --> WAIT_FC: Flow Control\nFS = WAIT received
    WAIT_FC --> ERROR: Flow Control\nFS = OVFL received
    WAIT_FC --> ERROR: As timeout\n(150 ms, no FC received)

    SENDING_CF --> SENDING_CF: send next CF\n(honouring STmin delay)
    SENDING_CF --> WAIT_FC: block-size (BS)\nlimit reached — need new FC
    SENDING_CF --> COMPLETE: all bytes sent

    COMPLETE --> IDLE: ready for next Tx
    ERROR --> IDLE: IsoTp_Reset() called
```

### Timing parameters honoured

| Parameter | Meaning | ECUS default |
|---|---|---|
| **BS** (Block Size) | Number of CFs to send before requiring a new FC | `0` (unlimited — receiver's choice) |
| **STmin** (Separation Time min) | Minimum delay between consecutive CFs | `0 ms` (as fast as possible) |
| **As** | Max time to complete a single Tx frame | `150 ms` |
| **Cr** | Max time to wait for the next CF while receiving | `150 ms` |

## 3. UDS Diagnostic Session State Machine

Implemented across `UdsServer.c` (state storage, S3 timer) and
`UdsDispatcher.c` (`Handle_DiagnosticSessionControl`, session-gated
dispatch via `CheckSessionAccess`).

```mermaid
stateDiagram-v2
    [*] --> DEFAULT

    DEFAULT --> PROGRAMMING: DiagnosticSessionControl(0x02)
    DEFAULT --> EXTENDED: DiagnosticSessionControl(0x03)

    PROGRAMMING --> DEFAULT: DiagnosticSessionControl(0x01)\nor S3 timeout\nor ECUReset
    PROGRAMMING --> EXTENDED: DiagnosticSessionControl(0x03)

    EXTENDED --> DEFAULT: DiagnosticSessionControl(0x01)\nor S3 timeout\nor ECUReset
    EXTENDED --> PROGRAMMING: DiagnosticSessionControl(0x02)

    note right of DEFAULT
        Security access is reset
        to LOCKED whenever this
        state is entered
    end note

    note right of EXTENDED
        S3 = 5000 ms.
        TesterPresent (0x3E) or
        any service request
        resets the S3 timer.
    end note
```

## 4. Security Access State Machine

Implemented in `Handle_SecurityAccess()` (`UdsDispatcher.c`) and
`UdsServer_Tick()` (lockout countdown).

```mermaid
stateDiagram-v2
    [*] --> LOCKED

    LOCKED --> SEED_SENT: RequestSeed (0x27 0x01)
    SEED_SENT --> UNLOCKED: SendKey (0x27 0x02)\nwith correct key
    SEED_SENT --> LOCKED: SendKey with\nincorrect key\n(attempts < 3)
    SEED_SENT --> DELAY_PENDING: SendKey with\nincorrect key\n(attempts == 3)

    DELAY_PENDING --> LOCKED: 10 s lockout\ntimer expires

    UNLOCKED --> LOCKED: Session returns\nto DEFAULT\n(S3 timeout or explicit)

    note right of DELAY_PENDING
        All SecurityAccess requests
        rejected with NRC 0x22
        (conditionsNotCorrect)
        while locked out
    end note
```

### Key algorithm (demonstration only)

```
seed          = rand() ^ time(NULL)              // server-generated, 32-bit
key_expected  = (~seed) ^ 0xA5A5A5A5              // deterministic function
unlock        = (key_received == key_expected)
```

> **Note**: this XOR-based algorithm exists to exercise the *protocol
> mechanics* correctly (seed/key round-trip, attempt counting, lockout
> timing). Real automotive ECUs use manufacturer-specific, non-public
> cryptographic algorithms — the mechanics demonstrated here transfer
> directly regardless of which specific key-derivation function sits
> behind them.
