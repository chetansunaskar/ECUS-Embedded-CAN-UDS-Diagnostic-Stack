/**
 * @file    ShellCli.c
 * @brief   Interactive CLI shell implementation.
 *
 * C concepts demonstrated:
 *   Command dispatch table (array of structs with function pointers),
 *   strtok_r for safe token parsing (reentrant, no global state),
 *   sscanf for hex byte parsing,
 *   argv-style parameter passing to sub-commands,
 *   snprintf for safe string formatting,
 *   arrays of uint8_t for raw UDS PDU building.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#include "app/ShellCli.h"
#include "hal/CanHal.h"
#include "services/Logger.h"
#include "services/SignalHandler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* =========================================================================
 * Module state
 * ========================================================================= */
static UdsServer *s_server = NULL;

/* =========================================================================
 * Private: CLI command handler type
 * ========================================================================= */
typedef EcusStatus (*CliCmdFn)(int argc, char *argv[]);

typedef struct CliCommand
{
    const char *name;
    const char *usage;
    const char *help;
    CliCmdFn    handler;
} CliCommand;

/* =========================================================================
 * Private: PDU injection helper
 *
 * Builds a CAN frame from raw hex bytes and injects it into the
 * CAN HAL Rx buffer (simulating a tester sending a request).
 * ========================================================================= */
static EcusStatus InjectUdsPdu(const uint8_t *pdu, size_t pduLen)
{
    ECUS_CHECK(pdu    != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(pduLen >= 1U,   ECUS_ERR_INVALID_PARAM, return ECUS_ERR_INVALID_PARAM);

    /* For short messages (≤7 bytes): Single Frame. */
    if (pduLen <= 7U)
    {
        CanFrame frame;
        (void)memset(&frame, 0, sizeof(frame));
        frame.id.raw  = CanFrame_MakeStdId((uint16_t)s_server->config.ecuCanRxId);
        frame.type    = CAN_FRAME_DATA;
        frame.dlc     = (uint8_t)(pduLen + 1U);
        frame.data[0] = (uint8_t)(0x00U | (pduLen & 0x0FU));   /* SF PCI */
        (void)memcpy(&frame.data[1], pdu, pduLen);

        LOG_DEBUG("CLI: injecting SF (len=%zu) SID=0x%02X", pduLen, pdu[0]);
        return CanHal_InjectRxFrame(&frame);
    }

    /* For multi-frame: First Frame. */
    CanFrame ff;
    (void)memset(&ff, 0, sizeof(ff));
    ff.id.raw  = CanFrame_MakeStdId((uint16_t)s_server->config.ecuCanRxId);
    ff.type    = CAN_FRAME_DATA;
    ff.dlc     = 8U;
    ff.data[0] = (uint8_t)(0x10U | ((pduLen >> 8U) & 0x0FU));
    ff.data[1] = (uint8_t)(pduLen & 0xFFU);
    (void)memcpy(&ff.data[2], pdu, 6U);

    EcusStatus rc = CanHal_InjectRxFrame(&ff);
    if (rc != ECUS_OK) { return rc; }

    /* Consecutive frames. */
    size_t offset = 6U;
    uint8_t sn    = 1U;

    while (offset < pduLen)
    {
        size_t   chunk = ECUS_MIN(pduLen - offset, 7U);
        CanFrame cf;
        (void)memset(&cf, 0, sizeof(cf));
        cf.id.raw  = CanFrame_MakeStdId((uint16_t)s_server->config.ecuCanRxId);
        cf.type    = CAN_FRAME_DATA;
        cf.dlc     = (uint8_t)(chunk + 1U);
        cf.data[0] = (uint8_t)(0x20U | (sn & 0x0FU));
        (void)memcpy(&cf.data[1], pdu + offset, chunk);
        rc = CanHal_InjectRxFrame(&cf);
        if (rc != ECUS_OK) { return rc; }
        offset += chunk;
        sn = (uint8_t)((sn >= 0x0FU) ? 0x01U : sn + 1U);
    }

    return ECUS_OK;
}

/* =========================================================================
 * CLI command handlers
 * ========================================================================= */

static EcusStatus Cmd_Help(int argc, char *argv[])
{
    ECUS_UNUSED_PARAM(argc);
    ECUS_UNUSED_PARAM(argv);
    ShellCli_PrintHelp();
    return ECUS_OK;
}

static EcusStatus Cmd_Status(int argc, char *argv[])
{
    ECUS_UNUSED_PARAM(argc);
    ECUS_UNUSED_PARAM(argv);
    UdsServer_PrintStatus(s_server);
    CanHal_PrintStats();
    return ECUS_OK;
}

/* session <1|2|3> — send DiagnosticSessionControl */
static EcusStatus Cmd_Session(int argc, char *argv[])
{
    if (argc < 2)
    {
        (void)printf("Usage: session <1=Default|2=Programming|3=Extended>\n");
        return ECUS_ERR_INVALID_PARAM;
    }
    uint8_t sessType = (uint8_t)atoi(argv[1]);
    uint8_t pdu[2] = { (uint8_t)UDS_SID_DIAGNOSTIC_SESSION_CONTROL, sessType };
    (void)printf("→ Injecting SessionControl 0x%02X\n", sessType);
    return InjectUdsPdu(pdu, 2U);
}

/* reset <1=hard|2=keyOffOn|3=soft> — send ECUReset */
static EcusStatus Cmd_Reset(int argc, char *argv[])
{
    if (argc < 2)
    {
        (void)printf("Usage: reset <1=hard|2=keyOffOn|3=soft>\n");
        return ECUS_ERR_INVALID_PARAM;
    }
    uint8_t resetType = (uint8_t)atoi(argv[1]);
    uint8_t pdu[2] = { (uint8_t)UDS_SID_ECU_RESET, resetType };
    (void)printf("→ Injecting ECUReset 0x%02X\n", resetType);
    return InjectUdsPdu(pdu, 2U);
}

/* seed — request SecurityAccess seed (level 1) */
static EcusStatus Cmd_Seed(int argc, char *argv[])
{
    ECUS_UNUSED_PARAM(argc);
    ECUS_UNUSED_PARAM(argv);
    uint8_t pdu[2] = { (uint8_t)UDS_SID_SECURITY_ACCESS,
                        (uint8_t)UDS_SA_REQUEST_SEED_LEVEL1 };
    (void)printf("→ Requesting SecurityAccess seed\n");
    return InjectUdsPdu(pdu, 2U);
}

/* unlock <seed_hex> — compute and send the key for a given seed */
static EcusStatus Cmd_Unlock(int argc, char *argv[])
{
    if (argc < 2)
    {
        (void)printf("Usage: unlock <seed_hex>  (e.g. unlock AABBCCDD)\n");
        return ECUS_ERR_INVALID_PARAM;
    }

    uint32_t seed = 0U;
    if (sscanf(argv[1], "%x", &seed) != 1)
    {
        (void)printf("Invalid hex seed\n");
        return ECUS_ERR_INVALID_PARAM;
    }

    /* Apply the same algorithm as the server: key = ~seed XOR 0xA5A5A5A5 */
    uint32_t key = (~seed) ^ 0xA5A5A5A5UL;
    (void)printf("→ Seed=0x%08X  Key=0x%08X\n", seed, key);

    uint8_t pdu[6];
    pdu[0] = (uint8_t)UDS_SID_SECURITY_ACCESS;
    pdu[1] = (uint8_t)UDS_SA_SEND_KEY_LEVEL1;
    pdu[2] = (uint8_t)((key >> 24U) & 0xFFU);
    pdu[3] = (uint8_t)((key >> 16U) & 0xFFU);
    pdu[4] = (uint8_t)((key >>  8U) & 0xFFU);
    pdu[5] = (uint8_t)( key          & 0xFFU);

    return InjectUdsPdu(pdu, 6U);
}

/* rdid <did_hex> — ReadDataByIdentifier */
static EcusStatus Cmd_ReadDid(int argc, char *argv[])
{
    if (argc < 2)
    {
        (void)printf("Usage: rdid <DID_hex>  (e.g. rdid F190)\n");
        return ECUS_ERR_INVALID_PARAM;
    }
    uint32_t did = 0U;
    (void)sscanf(argv[1], "%x", &did);

    uint8_t pdu[3] = {
        (uint8_t)UDS_SID_READ_DATA_BY_ID,
        (uint8_t)((did >> 8U) & 0xFFU),
        (uint8_t)( did        & 0xFFU)
    };
    (void)printf("→ ReadDataByIdentifier DID=0x%04X\n", (uint16_t)did);
    return InjectUdsPdu(pdu, 3U);
}

/* rdtc — ReadDTCInformation (by status mask 0xFF) */
static EcusStatus Cmd_ReadDtc(int argc, char *argv[])
{
    ECUS_UNUSED_PARAM(argc);
    ECUS_UNUSED_PARAM(argv);
    uint8_t pdu[3] = {
        (uint8_t)UDS_SID_READ_DTC_INFO,
        0x02U,    /* reportDTCByStatusMask */
        0xFFU     /* all status bits */
    };
    (void)printf("→ ReadDTCInformation (all DTCs)\n");
    return InjectUdsPdu(pdu, 3U);
}

/* clrdtc — ClearDiagnosticInformation (all) */
static EcusStatus Cmd_ClearDtc(int argc, char *argv[])
{
    ECUS_UNUSED_PARAM(argc);
    ECUS_UNUSED_PARAM(argv);
    uint8_t pdu[4] = {
        (uint8_t)UDS_SID_CLEAR_DTC,
        0xFFU, 0xFFU, 0xFFU   /* group = all */
    };
    (void)printf("→ ClearDiagnosticInformation (all DTCs)\n");
    return InjectUdsPdu(pdu, 4U);
}

/* testerp — TesterPresent */
static EcusStatus Cmd_TesterPresent(int argc, char *argv[])
{
    ECUS_UNUSED_PARAM(argc);
    ECUS_UNUSED_PARAM(argv);
    uint8_t pdu[2] = { (uint8_t)UDS_SID_TESTER_PRESENT, 0x00U };
    (void)printf("→ TesterPresent\n");
    return InjectUdsPdu(pdu, 2U);
}

/* raw <SID> [bytes...] — inject arbitrary PDU as hex bytes */
static EcusStatus Cmd_Raw(int argc, char *argv[])
{
    if (argc < 2)
    {
        (void)printf("Usage: raw <byte0_hex> [byte1_hex] ...\n");
        return ECUS_ERR_INVALID_PARAM;
    }

    uint8_t pdu[ECUS_CAN_DLC_MAX];
    size_t  pduLen = 0U;

    for (int i = 1; (i < argc) && (pduLen < sizeof(pdu)); i++)
    {
        uint32_t b = 0U;
        (void)sscanf(argv[i], "%x", &b);
        pdu[pduLen++] = (uint8_t)(b & 0xFFU);
    }

    (void)printf("→ Raw PDU (%zu bytes)\n", pduLen);
    return InjectUdsPdu(pdu, pduLen);
}

/* =========================================================================
 * Command dispatch table
 * ========================================================================= */
static const CliCommand k_commands[] =
{
    { "help",    "",                           "Print this help text",           Cmd_Help        },
    { "status",  "",                           "Print ECU and HAL status",       Cmd_Status      },
    { "session", "<1|2|3>",                    "DiagnosticSessionControl",       Cmd_Session     },
    { "reset",   "<1|2|3>",                    "ECUReset (1=hard,2=KOO,3=soft)", Cmd_Reset       },
    { "seed",    "",                           "Request SecurityAccess seed",    Cmd_Seed        },
    { "unlock",  "<seed_hex>",                 "Send SecurityAccess key",        Cmd_Unlock      },
    { "rdid",    "<DID_hex>",                  "ReadDataByIdentifier",           Cmd_ReadDid     },
    { "rdtc",    "",                           "ReadDTCInformation (all)",       Cmd_ReadDtc     },
    { "clrdtc",  "",                           "ClearDiagnosticInformation",     Cmd_ClearDtc    },
    { "testerp", "",                           "TesterPresent",                  Cmd_TesterPresent},
    { "raw",     "<byte0> [byte1...]",         "Inject raw PDU bytes (hex)",     Cmd_Raw         },
};

static const size_t k_commandCount = ECUS_ARRAY_SIZE(k_commands);

/* =========================================================================
 * Public API
 * ========================================================================= */

EcusStatus ShellCli_Init(UdsServer *server)
{
    ECUS_CHECK(server != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    s_server = server;
    LOG_INFO("CLI shell initialised (%zu commands)", k_commandCount);
    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

EcusStatus ShellCli_ProcessLine(const char *line)
{
    ECUS_CHECK(line != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(s_server != NULL, ECUS_ERR_NOT_READY, return ECUS_ERR_NOT_READY);

    /* Copy line into a mutable buffer for strtok_r. */
    char buf[CLI_MAX_LINE];
    (void)strncpy(buf, line, sizeof(buf) - 1U);
    buf[sizeof(buf) - 1U] = '\0';

    /* Tokenise (reentrant version — no global state). */
    char *saveptr = NULL;
    char *argv[CLI_MAX_ARGS];
    int   argc = 0;

    char *tok = strtok_r(buf, " \t\r\n", &saveptr);
    while ((tok != NULL) && ((size_t)argc < CLI_MAX_ARGS))
    {
        argv[argc++] = tok;
        tok = strtok_r(NULL, " \t\r\n", &saveptr);
    }

    if (argc == 0) { return ECUS_OK; }   /* Empty line — ignore. */

    /* Look up command in dispatch table. */
    for (size_t i = 0U; i < k_commandCount; i++)
    {
        if (strcmp(argv[0], k_commands[i].name) == 0)
        {
            return k_commands[i].handler(argc, argv);
        }
    }

    (void)printf("Unknown command: '%s'  (type 'help' for list)\n", argv[0]);
    return ECUS_ERR_INVALID_PARAM;
}

/* -------------------------------------------------------------------------- */

void ShellCli_PrintHelp(void)
{
    (void)printf("\n  ╔══════════════════════════════════════════════╗\n");
    (void)printf("  ║   ECUS — CAN-UDS Simulator Shell             ║\n");
    (void)printf("  ╚══════════════════════════════════════════════╝\n\n");
    (void)printf("  %-10s  %-22s  %s\n", "COMMAND", "USAGE", "DESCRIPTION");
    (void)printf("  %-10s  %-22s  %s\n",
                 "----------", "----------------------", "-----------------------------");

    for (size_t i = 0U; i < k_commandCount; i++)
    {
        (void)printf("  %-10s  %-22s  %s\n",
                     k_commands[i].name,
                     k_commands[i].usage,
                     k_commands[i].help);
    }

    (void)printf("\n  Type 'exit' to quit.\n\n");
}

/* -------------------------------------------------------------------------- */

void ShellCli_Run(void)
{
    char line[CLI_MAX_LINE];

    (void)printf("\nECUS Simulator ready.  Type 'help' for commands.\n");
    (void)printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    while (!SignalHandler_ShutdownRequested())
    {
        (void)printf("ecus> ");
        (void)fflush(stdout);

        if (fgets(line, (int)sizeof(line), stdin) == NULL)
        {
            /* EOF (Ctrl+D) or error — treat as exit. */
            (void)printf("\n");
            break;
        }

        /* Strip trailing newline. */
        size_t len = strlen(line);
        if ((len > 0U) && (line[len - 1U] == '\n'))
        {
            line[len - 1U] = '\0';
        }

        if (strcmp(line, "exit") == 0)
        {
            (void)printf("Goodbye.\n");
            break;
        }

        (void)ShellCli_ProcessLine(line);

        /* Small delay to allow async CAN ISR thread to process and print. */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 20000000L }; /* 20 ms */
        (void)nanosleep(&ts, NULL);
    }
}
