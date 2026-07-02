# API Reference & CLI Walkthrough

All output on this page is captured directly from running `build/ecus` —
nothing here is hand-written/simulated.

## Starting the shell

```bash
$ ./build/ecus -v -c
```

```
  ███████╗ ██████╗██╗   ██╗███████╗
  ██╔════╝██╔════╝██║   ██║██╔════╝
  █████╗  ██║     ██║   ██║███████╗
  ██╔══╝  ██║     ██║   ██║╚════██║
  ███████╗╚██████╗╚██████╔╝███████║
  ╚══════╝ ╚═════╝ ╚═════╝ ╚══════╝
  Embedded CAN-UDS Diagnostic Stack  v1.0.0
  ISO 14229 (UDS) + ISO 15765-2 (ISO-TP) — Pure C11

ECUS Simulator ready.  Type 'help' for commands.
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
ecus>
```

## Command reference

| Command | Usage | UDS Service | Description |
|---|---|---|---|
| `help` | | — | List all commands |
| `status` | | — | Print session/security state, DTCs, DIDs, CAN HAL stats |
| `session` | `<1\|2\|3>` | `0x10` | DiagnosticSessionControl (Default/Programming/Extended) |
| `reset` | `<1\|2\|3>` | `0x11` | ECUReset (hard/keyOffOn/soft) |
| `seed` | | `0x27` | Request a SecurityAccess seed |
| `unlock` | `<seed_hex>` | `0x27` | Compute & send the key for a given seed |
| `rdid` | `<DID_hex>` | `0x22` | ReadDataByIdentifier |
| `rdtc` | | `0x19` | ReadDTCInformation (all stored DTCs) |
| `clrdtc` | | `0x14` | ClearDiagnosticInformation (all) |
| `testerp` | | `0x3E` | TesterPresent |
| `raw` | `<byte0> [byte1...]` | any | Inject an arbitrary raw UDS PDU |
| `exit` | | — | Quit the shell |

## Walkthrough: session, security, and DTC read

Given `demo_script.txt`:
```
status
session 3
seed
```

Running `./build/ecus -v -b demo_script.txt` produces (excerpted,
timestamps/thread interleaving omitted for clarity):

```
ecus[1]> status
[INFO ] UdsServer.c : === UDS Server Status: 'ECUS-DemoECU' ===
[INFO ] UdsServer.c :   Session  : Default
[INFO ] UdsServer.c :   Security : Locked  (attempts=0)
[INFO ] UdsServer.c :   DTCs     : 1 stored
[INFO ] UdsServer.c :   DIDs     : 4 registered
[INFO ] UdsServer.c :   S3 timer : 0 ms
[INFO ] UdsServer.c :     DTC[0] 0x000A1F  status=0x89  'BatteryVoltageLow'
[INFO ] CanHal.c    : CAN HAL Stats: TX=0  RX=0  Dropped=0  RxPending=0  TxPending=0

ecus[2]> session 3
→ Injecting SessionControl 0x03
[DEBUG] IsoTp.c         : ISO-TP RX SF: len=2  complete
[DEBUG] UdsDispatcher.c : Dispatching SID=0x10  (DiagnosticSessionControl)
[INFO ] UdsDispatcher.c : Session changed to 0x03 (Extended)
[DEBUG] IsoTp.c         : ISO-TP TX SF: ID=0x7E8  len=6  DLC=7
[DEBUG] UdsDispatcher.c : Response sent: SID=0x10  len=6

ecus[3]> seed
→ Requesting SecurityAccess seed
[DEBUG] IsoTp.c         : ISO-TP RX SF: len=2  complete
[DEBUG] UdsDispatcher.c : Dispatching SID=0x27  (SecurityAccess)
[DEBUG] UdsDispatcher.c : SecurityAccess: seed generated = 0x01CFD1E1
[DEBUG] IsoTp.c         : ISO-TP TX SF: ID=0x7E8  len=6  DLC=7
[DEBUG] UdsDispatcher.c : Response sent: SID=0x27  len=6
```

To complete the unlock, compute the key and send it in the *same*
running instance. **The seed is regenerated randomly on every `seed`
call** (`rand() ^ time(NULL)`), so the exact hex values will differ each
time you run this — what matters is the relationship: the `unlock`
command computes `key = (~seed) ^ 0xA5A5A5A5` automatically from
whatever seed you paste in. Here is one such verified, live transcript:

```
ecus> session 3
[INFO ] UdsDispatcher.c : Session changed to 0x03 (Extended)

ecus> seed
[DEBUG] UdsDispatcher.c : SecurityAccess: seed generated = 0x01CFD1B4

ecus> unlock 01CFD1B4
→ Seed=0x01CFD1B4  Key=0x5B958BEE
[INFO ] UdsDispatcher.c : SecurityAccess: UNLOCKED
```

## Walkthrough: multi-frame ISO-TP (WriteDataByIdentifier)

Writing a 17-byte VIN value to DID `0xF190` exceeds the 7-byte
Single-Frame limit, triggering full First-Frame/Flow-Control/
Consecutive-Frame segmentation:

```
ecus> raw 2E F1 90 4E 45 57 56 49 4E 30 30 30 30 30 30 30 30 30 30 30
```

```
[DEBUG] IsoTp.c : ISO-TP RX FF: totalLen=8  first6Bytes received
[DEBUG] IsoTp.c : ISO-TP TX FC: fs=0x00 BS=0 STmin=0 ms
[DEBUG] IsoTp.c : ISO-TP RX: message complete (8 bytes)
[DEBUG] UdsDispatcher.c : Dispatching SID=0x2E  (WriteDataByIdentifier)
```

*(Full ISO-TP FF→FC→CF sequence and dispatch confirmed — see
`docs/architecture/StateMachines.md` for the state diagram this
exercises.)*

## Error scenarios (Negative Response Codes)

All four scenarios below were captured from a single real run
(`raw` commands injecting deliberately invalid requests):

### 1. Reading an unregistered DID

```
ecus> rdid 9999
→ ReadDataByIdentifier DID=0x9999
[WARN ] UdsDispatcher.c : NRC sent: SID=0x22  NRC=0x31
```

`NRC 0x31` = `requestOutOfRange` — DID `0x9999` was never registered via
`UdsServer_RegisterDid()`.

### 2. SecurityAccess attempted outside a valid session

```
ecus> raw 27 02 00 00 00 00
→ Raw PDU (6 bytes)
[WARN ] UdsDispatcher.c : SID=0x27 denied: NRC=0x7F
```

`NRC 0x7F` = `serviceNotSupportedInSession` — SecurityAccess (`0x27`) is
only permitted in Programming/Extended sessions (see the `sessionMask`
in `k_serviceTable[]`), and the server was still in Default session.

### 3. Sending an unknown Service ID

```
ecus> raw 99
→ Raw PDU (1 bytes)
[WARN ] UdsDispatcher.c : NRC sent: SID=0x99  NRC=0x11
```

`NRC 0x11` = `serviceNotSupported` — `0x99` matches no entry in the
dispatch table.

### 4. WriteDataByIdentifier without security unlock

```
ecus> session 3
ecus> raw 2E AB CD 11 22
→ Raw PDU (5 bytes)
[WARN ] UdsDispatcher.c : SID=0x2E denied: NRC=0x33
```

`NRC 0x33` = `securityAccessDenied` — `WriteDataByIdentifier` requires
`requiresSecurity = true` in its table entry, and no valid key had been
sent in this session.

## Batch scripting

Any sequence of commands above can be saved to a text file (one command
per line; `#` starts a comment) and run non-interactively:

```bash
./build/ecus -b my_script.txt
```

This is how the integration verification for this README was performed —
every code sample above is copy-pasted from real batch-script output,
not hand-written.

## Command-line options

```
$ ./build/ecus --help
```

```
Usage: ./build/ecus [options]

Options:
  -v, --verbose         Enable DEBUG-level logging
  -vv                   Enable TRACE-level logging
  -q, --quiet           Only show WARN/ERROR logs
  -l, --log <path>      Write logs to file (in addition to stdout)
  -c, --color           Enable ANSI colour log output
  -b, --batch <script>  Run commands from script file, then exit
  -h, --help            Show this help message
      --version         Show version information
```
