/**
 * @file    Main.c
 * @brief   ECUS application entry point.
 *
 * Responsibilities:
 *  1. Parse command-line arguments (argc/argv).
 *  2. Initialise core services (ErrorHandler, Logger, CrcEngine).
 *  3. Register signal handlers for graceful shutdown.
 *  4. Bootstrap the simulated ECU (EcuSession_Start).
 *  5. Run the interactive shell (or batch script if -b given).
 *  6. Clean up all subsystems on exit.
 *
 * C concepts demonstrated:
 *   int main(int argc, char *argv[]) — command-line argument parsing,
 *   getopt-style manual parsing (no external dependency),
 *   structured program lifecycle (init → run → deinit),
 *   return codes (EXIT_SUCCESS / EXIT_FAILURE),
 *   file handling (batch script reading via fopen/fgets).
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#include "app/Main.h"
#include "app/EcuSession.h"
#include "app/ShellCli.h"
#include "services/ErrorHandler.h"
#include "services/Logger.h"
#include "services/CrcEngine.h"
#include "services/SignalHandler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* =========================================================================
 * Private: print version banner
 * ========================================================================= */
static void PrintBanner(void)
{
    (void)printf("\n");
    (void)printf("  ███████╗ ██████╗██╗   ██╗███████╗\n");
    (void)printf("  ██╔════╝██╔════╝██║   ██║██╔════╝\n");
    (void)printf("  █████╗  ██║     ██║   ██║███████╗\n");
    (void)printf("  ██╔══╝  ██║     ██║   ██║╚════██║\n");
    (void)printf("  ███████╗╚██████╗╚██████╔╝███████║\n");
    (void)printf("  ╚══════╝ ╚═════╝ ╚═════╝ ╚══════╝\n");
    (void)printf("  Embedded CAN-UDS Diagnostic Stack  v%u.%u.%u\n",
                 ECUS_VERSION_MAJOR, ECUS_VERSION_MINOR, ECUS_VERSION_PATCH);
    (void)printf("  ISO 14229 (UDS) + ISO 15765-2 (ISO-TP) — Pure C11\n\n");
}

/* =========================================================================
 * Private: print usage help
 * ========================================================================= */
static void PrintUsage(const char *progName)
{
    (void)printf("Usage: %s [options]\n\n", progName);
    (void)printf("Options:\n");
    (void)printf("  -v, --verbose         Enable DEBUG-level logging\n");
    (void)printf("  -vv                   Enable TRACE-level logging\n");
    (void)printf("  -q, --quiet           Only show WARN/ERROR logs\n");
    (void)printf("  -l, --log <path>      Write logs to file (in addition to stdout)\n");
    (void)printf("  -c, --color           Enable ANSI colour log output\n");
    (void)printf("  -b, --batch <script>  Run commands from script file, then exit\n");
    (void)printf("  -h, --help            Show this help message\n");
    (void)printf("      --version         Show version information\n\n");
    (void)printf("Examples:\n");
    (void)printf("  %s                       Start interactive shell\n", progName);
    (void)printf("  %s -v -c                 Start with debug logging and colour\n", progName);
    (void)printf("  %s -b demo_script.txt    Run a batch diagnostic script\n\n", progName);
}

/* =========================================================================
 * Private: parse command-line arguments
 *
 * Manual argv parsing (no getopt dependency — fully portable across
 * Linux/macOS/embedded cross-compile toolchains).
 * ========================================================================= */
static EcusStatus ParseArgs(int argc, char *argv[], AppOptions *opts)
{
    /* Set defaults. */
    opts->logLevel    = LOG_LEVEL_INFO;
    opts->logFilePath = NULL;
    opts->colorOutput = false;
    opts->batchMode   = false;
    opts->batchScript = NULL;
    opts->showVersion = false;
    opts->showHelp    = false;

    for (int i = 1; i < argc; i++)
    {
        const char *arg = argv[i];

        if ((strcmp(arg, "-h") == 0) || (strcmp(arg, "--help") == 0))
        {
            opts->showHelp = true;
        }
        else if (strcmp(arg, "--version") == 0)
        {
            opts->showVersion = true;
        }
        else if ((strcmp(arg, "-v") == 0) || (strcmp(arg, "--verbose") == 0))
        {
            opts->logLevel = LOG_LEVEL_DEBUG;
        }
        else if (strcmp(arg, "-vv") == 0)
        {
            opts->logLevel = LOG_LEVEL_TRACE;
        }
        else if ((strcmp(arg, "-q") == 0) || (strcmp(arg, "--quiet") == 0))
        {
            opts->logLevel = LOG_LEVEL_WARN;
        }
        else if ((strcmp(arg, "-c") == 0) || (strcmp(arg, "--color") == 0))
        {
            opts->colorOutput = true;
        }
        else if ((strcmp(arg, "-l") == 0) || (strcmp(arg, "--log") == 0))
        {
            if ((i + 1) >= argc)
            {
                (void)fprintf(stderr, "Error: %s requires a file path argument\n", arg);
                return ECUS_ERR_INVALID_PARAM;
            }
            opts->logFilePath = argv[++i];
        }
        else if ((strcmp(arg, "-b") == 0) || (strcmp(arg, "--batch") == 0))
        {
            if ((i + 1) >= argc)
            {
                (void)fprintf(stderr, "Error: %s requires a script path argument\n", arg);
                return ECUS_ERR_INVALID_PARAM;
            }
            opts->batchMode   = true;
            opts->batchScript = argv[++i];
        }
        else
        {
            (void)fprintf(stderr, "Error: unrecognised option '%s'\n", arg);
            return ECUS_ERR_INVALID_PARAM;
        }
    }

    return ECUS_OK;
}

/* =========================================================================
 * Private: run a batch script file (one CLI command per line)
 *
 * Demonstrates file handling: fopen, fgets, fclose, line-by-line processing.
 * ========================================================================= */
static EcusStatus RunBatchScript(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL)
    {
        LOG_ERROR("Cannot open batch script: %s", path);
        return ECUS_ERR_IO;
    }

    char   line[CLI_MAX_LINE];
    size_t lineNum = 0U;

    LOG_INFO("Running batch script: %s", path);

    while (fgets(line, (int)sizeof(line), fp) != NULL)
    {
        lineNum++;

        /* Strip trailing newline. */
        size_t len = strlen(line);
        if ((len > 0U) && (line[len - 1U] == '\n'))
        {
            line[len - 1U] = '\0';
        }

        /* Skip blank lines and comments (lines starting with '#'). */
        if ((line[0] == '\0') || (line[0] == '#'))
        {
            continue;
        }

        (void)printf("ecus[%zu]> %s\n", lineNum, line);
        (void)ShellCli_ProcessLine(line);

        /* Allow async ISR thread to process and print responses. */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 50000000L }; /* 50 ms */
        (void)nanosleep(&ts, NULL);
    }

    (void)fclose(fp);
    LOG_INFO("Batch script completed (%zu lines processed)", lineNum);
    return ECUS_OK;
}

/* =========================================================================
 * Private: shutdown callback for SignalHandler
 * ========================================================================= */
static void OnShutdownRequested(void)
{
    LOG_INFO("Shutdown signal received — cleaning up...");
}

/* =========================================================================
 * Application entry point
 * ========================================================================= */
int main(int argc, char *argv[])
{
    AppOptions opts;

    /* --- Step 1: Parse command-line arguments --- */
    if (ParseArgs(argc, argv, &opts) != ECUS_OK)
    {
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
    }

    if (opts.showHelp)
    {
        PrintBanner();
        PrintUsage(argv[0]);
        return EXIT_SUCCESS;
    }

    if (opts.showVersion)
    {
        (void)printf("ECUS version %u.%u.%u\n",
                     ECUS_VERSION_MAJOR, ECUS_VERSION_MINOR, ECUS_VERSION_PATCH);
        return EXIT_SUCCESS;
    }

    PrintBanner();

    /* --- Step 2: Initialise core services (order matters) --- */
    if (ErrorHandler_Init() != ECUS_OK)
    {
        (void)fprintf(stderr, "FATAL: ErrorHandler_Init failed\n");
        return EXIT_FAILURE;
    }

    LoggerConfig logCfg = {
        .minLevel         = opts.logLevel,
        .logFilePath      = opts.logFilePath,
        .echoToStdout     = true,
        .colorEnabled     = opts.colorOutput,
        .timestampEnabled = (opts.logFilePath != NULL)
    };
    if (Logger_Init(&logCfg) != ECUS_OK)
    {
        (void)fprintf(stderr, "FATAL: Logger_Init failed\n");
        return EXIT_FAILURE;
    }

    if (CrcEngine_Init() != ECUS_OK)
    {
        LOG_ERROR("CrcEngine_Init failed");
        Logger_Deinit();
        return EXIT_FAILURE;
    }

    /* --- Step 3: Register signal handlers for graceful shutdown --- */
    if (SignalHandler_Init(OnShutdownRequested) != ECUS_OK)
    {
        LOG_ERROR("SignalHandler_Init failed");
        Logger_Deinit();
        return EXIT_FAILURE;
    }

    /* --- Step 4: Bootstrap the simulated ECU --- */
    EcusStatus rc = EcuSession_Start();
    if (rc != ECUS_OK)
    {
        LOG_ERROR("EcuSession_Start failed: %s", ErrorHandler_StatusStr(rc));
        Logger_Deinit();
        return EXIT_FAILURE;
    }

    rc = ShellCli_Init(EcuSession_GetServer());
    if (rc != ECUS_OK)
    {
        LOG_ERROR("ShellCli_Init failed: %s", ErrorHandler_StatusStr(rc));
        EcuSession_Stop();
        Logger_Deinit();
        return EXIT_FAILURE;
    }

    /* --- Step 5: Run interactive shell or batch script --- */
    if (opts.batchMode)
    {
        rc = RunBatchScript(opts.batchScript);
        if (rc != ECUS_OK)
        {
            LOG_ERROR("Batch script execution failed: %s", ErrorHandler_StatusStr(rc));
        }
    }
    else
    {
        ShellCli_Run();
    }

    /* --- Step 6: Clean shutdown --- */
    LOG_INFO("ECUS shutting down...");
    EcuSession_Stop();
    Logger_Deinit();

    return EXIT_SUCCESS;
}
