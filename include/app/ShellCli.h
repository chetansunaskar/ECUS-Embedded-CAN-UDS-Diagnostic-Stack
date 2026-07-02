/**
 * @file    ShellCli.h
 * @brief   Interactive command-line shell for ECUS simulator control.
 *
 * Implements a mini shell that lets the user inject UDS requests,
 * inspect server state, and trigger fault scenarios — all without
 * needing a real CAN bus.
 *
 * Demonstrates: command dispatch table, argv/argc pattern, string parsing,
 *               sscanf for hex input, function pointer command handlers.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#ifndef SHELL_CLI_H
#define SHELL_CLI_H

#include "hal/Platform.h"
#include "services/ErrorHandler.h"
#include "uds/UdsServer.h"

/** Maximum number of tokens per CLI command line. */
#define CLI_MAX_ARGS     16U
/** Maximum length of a single CLI input line. */
#define CLI_MAX_LINE    256U

/**
 * @brief  Initialise the CLI shell with a pointer to the UDS server.
 * @param  server  Active UDS server (commands will target this server).
 * @return ECUS_OK on success.
 */
EcusStatus ShellCli_Init(UdsServer *server);

/**
 * @brief  Process one line of user input.
 *         Tokenises the line, looks up the command, and dispatches.
 * @param  line  NUL-terminated input string.
 * @return ECUS_OK on success, ECUS_ERR_INVALID_PARAM for unknown commands.
 */
EcusStatus ShellCli_ProcessLine(const char *line);

/**
 * @brief  Print the help text listing all available commands.
 */
void ShellCli_PrintHelp(void);

/**
 * @brief  Run the interactive REPL loop (blocking).
 *         Returns when the user types "exit" or a shutdown signal is received.
 */
void ShellCli_Run(void);

#endif /* SHELL_CLI_H */
