/**
 * @file    EcuSession.h
 * @brief   Application-level ECU bootstrap: wires HAL, UDS server, and demo data.
 *
 * This module owns the single UdsServer instance for the simulated ECU,
 * registers default DIDs and DTCs, connects the CanHal Rx callback to the
 * UDS server, and runs a background "tick" thread that drives all timers
 * (ISO-TP pacing, S3 session watchdog, security lockout countdown).
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#ifndef ECU_SESSION_H
#define ECU_SESSION_H

#include "hal/Platform.h"
#include "services/ErrorHandler.h"
#include "uds/UdsServer.h"

/**
 * @brief  Bootstrap the simulated ECU: init CAN HAL, UDS server, demo data.
 * @return ECUS_OK on success.
 */
EcusStatus EcuSession_Start(void);

/**
 * @brief  Stop the tick thread and deinitialise all subsystems.
 */
void EcuSession_Stop(void);

/**
 * @brief  Get a pointer to the active UDS server (for CLI command injection).
 * @return Pointer to the singleton UdsServer instance.
 */
UdsServer *EcuSession_GetServer(void);

#endif /* ECU_SESSION_H */
