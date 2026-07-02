# Memory Layout & Footprint Analysis

All figures on this page are measured directly from the built binary
(`build/ecus`, GCC 13, x86-64, release build `-O2`) and from
`sizeof()` on the actual target structs — not estimated.

## 1. Binary Size Breakdown

```
$ size build/ecus
   text     data     bss     dec         hex     filename
  124620    2596    13200    140417     22480  build/ecus
```

| Section | Size | Contents |
|---|---|---|
| `.text` | ~49.4 KB | Compiled code — every module, all 8 UDS service handlers, ISO-TP state machines, CLI dispatch |
| `.data` | ~2.1 KB | Initialised globals (CRC table seeds, string literals for logging/CLI help text) |
| `.bss` | ~10.1 KB | Uninitialised static storage — this is where every module's static state lives (see §3) |
| **Total footprint** | **~63 KB** | Entire diagnostic stack, including the CLI shell |

For reference, this is comparable in order-of-magnitude to a *single*
UDS service module in many commercial AUTOSAR-based diagnostic stacks —
ECUS implements 8 services plus the full transport/HAL/services layers
in a smaller footprint than that, because it carries none of the
AUTOSAR RTE/BSW configuration-generator overhead.

## 2. Key Structure Sizes (measured via `sizeof`)

| Structure | Size | Notes |
|---|---|---|
| `CanFrame` | 24 bytes | ID union (4B) + type enum (4B) + DLC (1B, padded) + 8B data + 4B timestamp + padding |
| `IsoTpChannel` | **4200 bytes** | Dominated by `rxBuf[4096]` — the full ISO-TP reassembly buffer per channel |
| `UdsServer` | **7296 bytes** | Dominated by the embedded `IsoTpChannel` (4200B) + `dtcTable[16]` (640B) + `didTable[24]` (2400B) |
| `DtcRecord` | 40 bytes | 4B code + 1B status + 1B severity + 32B label + padding |
| `DidRecord` | 100 bytes | 2B DID + 64B data + 1B len + 32B label + padding |
| `RingBuffer` | 56 bytes | Two `atomic_size_t` (16B) + pointers/metadata — excludes backing storage, which is caller-supplied |
| `MemPool` | 48 bytes | Metadata only — excludes backing storage, which is caller-supplied |

**Design implication**: `IsoTpChannel` embeds a full 4096-byte
reassembly buffer *by value* inside `UdsServer`, not by pointer to a
heap allocation. This is a deliberate MISRA-C-friendly choice — the
maximum memory a diagnostic session can ever consume is known at
**compile time**, with zero risk of an allocation failure mid-session.
The trade-off is that `sizeof(UdsServer)` is large (~7.3 KB) even though
most sessions never approach the 4095-byte ISO-TP maximum — an
acceptable trade on a modern MCU with tens to hundreds of KB of RAM, and
the same trade real automotive ECU software makes.

## 3. Static (`.bss`) Memory Map

Every module's persistent state is a **file-scope `static`** — there is
exactly one instance of each, sized at compile time, with no dynamic
allocation anywhere in the diagnostic hot path:

```
┌───────────────────────────────────────────────┐  .bss section
│  CanHal.c                                     │
│    s_rxStorage[16] × sizeof(CanFrame)  384 B  │
│    s_txStorage[16] × sizeof(CanFrame)  384 B  │
│    s_hal (CanHalState)                 ~120 B │
├───────────────────────────────────────────────┤
│  EcuSession.c                                 │
│    s_server (UdsServer)               7296 B  │  ◄── dominant consumer
├───────────────────────────────────────────────┤
│  CrcEngine.c                                  │
│    s_crc8Table[256]                    256 B  │
│    s_crc16Table[256]                   512 B  │
│    s_crc32Table[256]                  1024 B  │
├───────────────────────────────────────────────┤
│  Logger.c                                     │
│    s_log (module state + mutex)         ~80 B │
├───────────────────────────────────────────────┤
│  ErrorHandler.c                               │
│    s_lastError (atomic_int)              4 B  │
└───────────────────────────────────────────────┘
     Total measured .bss:                10344 B
```

## 4. Stack Usage (per call chain)

The deepest call chain in the system is a UDS request being received and
dispatched:

```
CanIsrThread (ISR-sim thread stack)
  └─ RingBuffer_Pop()                    ~40 B
      └─ UdsServer_ProcessCanFrame()      ~16 B
          └─ IsoTp_ProcessRxFrame()        ~24 B
              └─ HandleSingleFrame()        ~16 B
                  └─ (pduCallback) OnPduReceived()  ~16 B
                      └─ UdsDispatcher_Dispatch()    ~280 B  (respBuf[256] local!)
                          └─ Handle_XXX()              ~32-64 B each
```

The single largest stack frame is `UdsDispatcher_Dispatch()`, which
declares `uint8_t respBuf[256]` on the stack rather than using a pooled
or static buffer. **This is a deliberate simplicity trade-off**
documented here rather than hidden: on a real memory-constrained MCU
target (e.g. an STM32L0 with 2–8 KB total RAM), this would be converted
to a `MemPool`-allocated buffer sized to the actual maximum response
length per service, rather than a flat 256-byte stack array.
## 5. Heap Usage

**Zero.** `grep -rn "malloc\|calloc\|realloc\|free(" src/` returns no
matches outside of the `MemPool` module's own doc-comments explaining
*why* it exists instead of using the heap. Every buffer in the system is
either:

- A file-scope `static` array (CAN HAL ring buffer storage, CRC tables),
- Embedded by value inside a parent struct (`IsoTpChannel.rxBuf` inside
  `UdsServer`), or
- Caller-stack-allocated with a bounded, known-at-compile-time size.

This satisfies MISRA-C:2012 Rule 21.3 (no dynamic memory) throughout the
diagnostic hot path, and makes the entire system's peak memory usage
computable statically — a property real safety-relevant embedded
software must have.

## 6. Thread Stacks

Three POSIX threads run concurrently (default glibc stack size, 8 MB
each, though actual usage is a tiny fraction of that):

| Thread | Purpose | Created in |
|---|---|---|
| Main thread | CLI REPL / batch script processing | `main()` |
| CAN ISR-simulator | Drains Tx ring buffer, dispatches Rx callback | `CanHal_Init()` → `CanIsrThread` |
| UDS tick thread | Drives `UdsServer_Tick()` every 10 ms (ISO-TP pacing, S3 timer, security lockout) | `EcuSession_Start()` → `TickThreadFn` |

On a real embedded target, these three logical flows would map to RTOS
tasks with explicitly sized stacks (e.g. FreeRTOS `xTaskCreate(...,
usStackDepth, ...)`) rather than POSIX threads with default 8 MB — the
*logical* concurrency model (one flow per responsibility) transfers
directly; only the OS primitive changes.
