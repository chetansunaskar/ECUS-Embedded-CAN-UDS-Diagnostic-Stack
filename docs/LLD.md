# Low-Level Design (LLD)

This document details each module's internal API, data structures, and
behaviour. For the system-level view, see [`HLD.md`](HLD.md).

## 1. Services Layer

### 1.1 `ErrorHandler`

**Purpose**: single source of truth for status codes and fault reporting
across every layer.

```c
typedef enum EcusStatus {
    ECUS_OK = 0,
    ECUS_ERR_GENERIC = -1, ECUS_ERR_NULL_PTR = -2, /* ... generic: -1..-99   */
    ECUS_ERR_ISOTP_OVERFLOW = -100, /* ...              transport: -100..-199 */
    ECUS_ERR_UDS_SERVICE_NA = -200, /* ...               UDS: -200..-299     */
    ECUS_ERR_HAL_INIT = -300,       /* ...               HAL: -300..-399     */
    ECUS_ERR_LOG_INIT = -400,       /* ...               services: -400..-499*/
} EcusStatus;
```

Range partitioning means a developer can identify which layer raised an
error just from its numeric value — useful when debugging integration
issues without a symbol table.

**Key macros**:

| Macro | Behaviour |
|---|---|
| `ECUS_ASSERT(expr)` | Raises a `FATAL` fault (terminates) if `expr` is false. For invariants that must never be violated. |
| `ECUS_CHECK(expr, code, action)` | Raises a `WARNING` fault and executes `action` (typically `return code`) if `expr` is false. For public-API parameter validation. |
| `ECUS_PROPAGATE(expr)` | If `expr` (an `EcusStatus`-returning call) is non-OK, stores the error and returns it immediately from the current function. |

**Fault handler pattern**: `ErrorHandler_RaiseFault()` dispatches to a
registered `FaultHandlerFn` callback (function pointer), defaulting to a
built-in handler that logs to `stderr` and calls `exit()` on `FATAL`
severity. Applications can override this via
`ErrorHandler_RegisterHandler()` — e.g. to trigger a watchdog reset
instead of `exit()` on real hardware.

### 1.2 `Logger`

Five levels (`TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`), each with a
zero-argument-safe macro (`LOG_INFO("static message")` compiles even
with no format arguments, via the GNU `##__VA_ARGS__` extension).

**Thread safety**: a single `pthread_mutex_t` serialises all writers.
Because the CAN HAL's ISR-simulator thread and the tick thread both log
concurrently with the main thread, this mutex is load-bearing, not
decorative — removing it produces interleaved/corrupted log lines
(verified during development).

**Output routing**: configurable to stdout, a file, or both
simultaneously (`LoggerConfig.echoToStdout`). Every write is followed by
an explicit `fflush()` so log output survives a crash — critical for
diagnosing the exact point of failure in a `FATAL` fault path.

### 1.3 `CrcEngine`

Three algorithms, all table-driven (256-entry lookup tables built once at
`CrcEngine_Init()`):

| Algorithm | Polynomial | Init | XorOut | Use case (real-world) |
|---|---|---|---|---|
| CRC-8/SAE-J1850 | `0x1D` | `0xFF` | `0xFF` | OBD-II frame validation |
| CRC-16/CCITT-FALSE | `0x1021` | `0xFFFF` | `0x0000` | ISO-TP header integrity |
| CRC-32/ISO-HDLC | `0x04C11DB7` (reflected) | `0xFFFFFFFF` | `0xFFFFFFFF` | Firmware image verification |

All three are verified against the standard `"123456789"` check-value
test vectors in `tests/unit/test_CrcEngine.c`.

**Streaming API**: `CrcContext` holds an in-progress accumulator, letting
large payloads (e.g. a simulated firmware image during
`RequestDownload`/`TransferData`) be CRC-checked incrementally without
holding the entire buffer in memory at once — the same pattern a real
bootloader uses while streaming flash-programming data over CAN.

### 1.4 `EndianUtils`

CAN payloads are raw byte arrays; UDS DIDs and DTC codes are
multi-byte, big-endian ("network order") values. This header provides:

- `EndianUtils_Swap16/32/64()` — `__builtin_bswap*` intrinsics (falls
  back to manual shift/mask on non-GCC/Clang).
- `ECUS_HTONS`/`ECUS_NTOHS` etc. — no-ops on a big-endian host, real
  swaps on little-endian (the common case for ARM Cortex-M / x86).
- `EndianUtils_ReadBE16/32()`, `WriteBE16/32()` — safe unaligned
  byte-at-a-time access, avoiding undefined behaviour from casting a
  `uint8_t*` to `uint32_t*` on unaligned CAN payload buffers.

### 1.5 `MemPool`

Fixed-block allocator backed by a caller-supplied static buffer:

- **Free list** is a singly-linked list of `FreeNode` structs *overlaid
  on the free blocks themselves* — zero bookkeeping memory overhead.
- **O(1)** allocate (pop free-list head) and free (push to head).
- **Poisoning**: freed blocks are `memset(0xFE, ...)` to make
  use-after-free bugs visible immediately under a debugger memory view.
- **Canary-ready**: `MEM_POOL_CANARY` constant reserved for future
  overflow-detection headers per block (see Future Enhancements).

### 1.6 `SignalHandler`

Registers `SIGINT`/`SIGTERM` handlers using `sigaction()`. The handler
itself only sets a `volatile sig_atomic_t` flag — the *only* type
guaranteed safe to touch inside a POSIX signal handler per C11 §7.14.1.1.
The actual shutdown callback is invoked later, from the main loop
(`SignalHandler_ShutdownRequested()`), which is **not** async-signal-safe
context, so it's free to call `printf`, `pthread_join`, etc.

## 2. Transport Layer

### 2.1 `RingBuffer`

Single-Producer/Single-Consumer lock-free queue.

```c
typedef struct RingBuffer {
    uint8_t       *storage;
    size_t         elemSize, capacity, mask;
    atomic_size_t  head, tail;
} RingBuffer;
```

**Correctness argument** (why no mutex is needed): with exactly one
producer thread and one consumer thread, `head` is written only by the
producer and read by both; `tail` is written only by the consumer and
read by both. The push path loads `tail` with `memory_order_acquire`
(observing the consumer's most recent free-slot release) and stores
`head` with `memory_order_release` (publishing the new element before
the consumer can see the updated index). The pop path mirrors this. This
acquire/release pairing is the standard SPSC pattern and is verified
under ThreadSanitizer-equivalent (`-fsanitize=address,undefined`, plus
manual review) in this project — see `tests/unit/test_RingBuffer.c` for
wraparound-boundary coverage.

**Power-of-2 capacity** lets index wrapping use a bitmask
(`index & mask`) instead of modulo — both faster and free of the
platform-dependent-signedness pitfalls of `%` on `size_t`.

### 2.2 `CanFrame`

```c
typedef union CanId {
    uint32_t raw;
    struct { uint32_t id:29, rsvd:1, rtr:1, ide:1; } fields;
} CanId;
```

The union lets code read/write the whole 32-bit arbitration word (for
fast comparison/copy) or the individual bit-fields (for building a
specific frame type) without any conversion function — a direct mirror
of how CAN controller peripheral registers are typically memory-mapped
on real silicon (e.g. STM32 FDCAN `RXF0R`).

### 2.3 `IsoTp` — the core transport state machine

**Rx state machine**:

```
IDLE ──(SF received)──────────────────────► COMPLETE ──► IDLE
IDLE ──(FF received, send FC)─────────────► RECEIVING
RECEIVING ──(CF, SN matches)──────────────► RECEIVING (loop until done)
RECEIVING ──(CF, SN matches, last chunk)──► COMPLETE ──► IDLE
RECEIVING ──(CF, SN mismatch)─────────────► ERROR
```

**Tx state machine**:

```
IDLE ──(payload ≤7B: send SF)─────────────► COMPLETE (immediate)
IDLE ──(payload >7B: send FF)─────────────► WAIT_FC
WAIT_FC ──(FC=CTS received)───────────────► SENDING_CF
WAIT_FC ──(As timeout)─────────────────────► ERROR
SENDING_CF ──(all bytes sent)──────────────► COMPLETE
SENDING_CF ──(BS block limit hit)──────────► WAIT_FC (next FC needed)
```

Full diagrams: [`architecture/StateMachines.md`](architecture/StateMachines.md).

**Bounds checking**: every write into `rxBuf` in
`HandleConsecutiveFrame()` is preceded by an explicit range check against
`ISOTP_MAX_PAYLOAD_BUF` — a malicious or corrupted First Frame claiming a
length larger than the buffer cannot cause an overflow; it's rejected
with `ECUS_ERR_ISOTP_OVERFLOW` before any `memcpy`.

**Sequence number handling**: consecutive-frame SNs cycle `0x1..0xF`
(never `0x0`) per ISO 15765-2 — `HandleConsecutiveFrame()` and
`SendConsecutiveFrame()` both implement the `0xF → 0x1` wrap explicitly.

## 3. HAL Layer

### 3.1 `CanHal`

```c
typedef struct CanHalState {
    CanHalConfig     config;
    RingBuffer       rxBuf, txBuf;
    CanRxCallbackFn  rxCallback;   void *rxCallbackCtx;
    pthread_t        isrThread;
    volatile bool    isrRunning;
    atomic_uint_fast32_t statTxCount, statRxCount, statDropped;
} CanHalState;
```

The `CanIsrThread` function is the entire "hardware simulation": it
wakes every ~1ms (`nanosleep`), drains the Tx ring buffer, and — in
loopback mode — pushes each transmitted frame straight into the Rx ring
buffer, then drains Rx and invokes the registered callback. This gives
the rest of the stack the *exact* asynchronous, interrupt-driven
programming model it would have on real silicon, without needing a real
CAN transceiver.

**Why a thread instead of a synchronous function call?** A synchronous
`CanHal_Transmit()` that directly invoked the Rx callback would collapse
the Tx/Rx timing relationship and hide bugs that only manifest with
real asynchrony (e.g. a race between `IsoTp_TxPump()` and an incoming
Flow Control frame). Using a real thread with real scheduling makes the
simulator load-bearing for concurrency bugs, not just a demo.

## 4. UDS Layer

### 4.1 `UdsServer`

Owns the full diagnostic state for one simulated ECU: active session,
security state machine, DTC table (fixed array,
`UDS_SERVER_MAX_DTCS = 16`), DID table (`UDS_SERVER_MAX_DIDS = 24`), and
S3 session-timeout tracking.

**S3 timer**: per ISO 14229, a non-default session must revert to
Default if no diagnostic traffic (including `TesterPresent`) arrives
within `S3` (here, 5000 ms). `UdsServer_Tick()`, driven by a 10ms POSIX
thread in `EcuSession.c`, accumulates elapsed time and performs this
reversion — including re-locking security — exactly as a real ECU would.

### 4.2 `UdsDispatcher` — the dispatch table pattern

```c
typedef struct ServiceEntry {
    UdsSid              sid;
    UdsServiceHandlerFn handler;
    uint8_t             sessionMask;       /* bitmask of legal sessions */
    bool                requiresSecurity;
    const char         *name;
} ServiceEntry;

static const ServiceEntry k_serviceTable[] = { /* one row per service */ };
```

`UdsDispatcher_Dispatch()`:
1. Linear-searches `k_serviceTable[]` by SID (table is small — 8 entries
   — so O(n) is appropriate; a real ECU with 20+ services might switch to
   a sorted table + binary search, or a perfect hash).
2. Calls `CheckSessionAccess()` to verify the current session's bitmask
   intersects `entry->sessionMask`, and that `requiresSecurity` is
   satisfied.
3. Invokes `entry->handler` via function pointer, passing the raw
   request bytes and a response buffer for the handler to fill.
4. Maps any non-OK `EcusStatus` the handler returns to the appropriate
   UDS Negative Response Code (NRC) and sends it via
   `UdsDispatcher_SendNegativeResponse()`.
5. On success, transmits the handler's positive response via
   `IsoTp_Transmit()`.

**Security Access algorithm** (`Handle_SecurityAccess`):

```
seed = rand() ^ time(NULL)                    // server generates & stores
key_expected = (~seed) ^ 0xA5A5A5A5            // deterministic function of seed
if key_received == key_expected: UNLOCKED
else: attempts++; if attempts >= 3: 10s lockout
```

This is explicitly a **demonstration algorithm** — real automotive
security access uses a manufacturer-specific cryptographic function
(often AES- or HMAC-based) that is never disclosed publicly. The
project's algorithm exists to exercise the *protocol mechanics*
(seed generation, key verification, attempt counting, lockout) correctly,
which is the reusable skill; the specific XOR function is intentionally
simple and documented as such in the source.

## 5. Application Layer

### 5.1 `EcuSession` — composition root

`EcuSession_Start()` is the single place where all modules are wired
together: it initialises the CAN HAL, initialises the UDS server,
registers the CAN HAL's Rx callback to point at the UDS server, seeds
demo DIDs/DTCs, and starts the 10ms tick thread. This "composition root"
pattern keeps every other module free of knowledge about how it's wired
to its neighbours — `UdsServer.c` has zero `#include` on `CanHal.h`
questions of *bootstrapping*; it only knows about `IsoTpChannel`.

### 5.2 `ShellCli` — command dispatch table

Mirrors the same dispatch-table pattern as `UdsDispatcher`, but for CLI
commands:

```c
typedef struct CliCommand {
    const char *name, *usage, *help;
    CliCmdFn    handler;
} CliCommand;
static const CliCommand k_commands[] = { /* one row per command */ };
```

Tokenisation uses `strtok_r` (the reentrant variant — no hidden global
state, safe even though this project is single-threaded for CLI
parsing, on principle).

## Unit Testing Strategy

Tests are organised in three tiers mirroring the architecture:

1. **Pure-logic unit tests** (`test_CrcEngine.c`, `test_MemPool.c`,
   `test_RingBuffer.c`) — no HAL, no threads; test a single module's
   contract against known-good vectors (e.g. standard CRC check values)
   and edge cases (empty buffer, full buffer, wraparound).
2. **Protocol unit tests** (`test_IsoTp.c`, `test_UdsDispatcher.c`) —
   bring up the CAN HAL (in loopback mode) as a fixture, then inject
   frames/PDUs and assert on internal state transitions. These require a
   short `nanosleep` after injection to let the async ISR-simulator
   thread process the frame — documented in each test.
3. **Integration tests** (`test_DtcScenario.c`) — exercise the entire
   stack through the same code path a CLI command would use (raw PDU
   injection → CanHal → ISO-TP → UDS dispatch), verifying multi-step
   scenarios (store→read→clear→verify; session→security→write) rather
   than single calls.

All three tiers share the same hand-rolled test framework
(`tests/framework/TestRunner.h`) — deliberately dependency-free so the
same pattern would port directly to a cross-compiled target's host-side
test harness where pulling in Unity/CMock may not be an option.
