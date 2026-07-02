/**
 * @file    SignalHandler.h
 * @brief   POSIX signal handler registration for graceful shutdown.
 *
 * Handles SIGINT (Ctrl+C) and SIGTERM to ensure all modules are
 * deinitialised cleanly before process exit.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include "hal/Platform.h"
#include "services/ErrorHandler.h"

/** Shutdown callback invoked when a termination signal is received. */
typedef void (*ShutdownCallbackFn)(void);

/**
 * @brief  Register signal handlers for SIGINT and SIGTERM.
 * @param  cb  Callback to invoke on signal (may be NULL).
 * @return ECUS_OK on success.
 */
EcusStatus SignalHandler_Init(ShutdownCallbackFn cb);

/**
 * @brief  Check whether a shutdown signal has been received.
 * @return true if SIGINT or SIGTERM was received.
 */
bool SignalHandler_ShutdownRequested(void);

#endif /* SIGNAL_HANDLER_H */
