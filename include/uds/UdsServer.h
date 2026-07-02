/**
 * @file    UdsServer.h
 * @brief   UDS server (ECU-side) top-level context and API.
 *
 * The UDS server is the integration point that connects:
 *   ISO-TP channel  →  UDS dispatcher  →  service handlers
 *
 * It maintains:
 *  - The ISO-TP channel instance (for PDU receive/transmit).
 *  - The active diagnostic session type.
 *  - Security access state (locked/unlocked, attempt counter).
 *  - DTC storage (up to UDS_SERVER_MAX_DTCS entries).
 *  - DID (Data Identifier) table.
 *  - A periodic TesterPresent watchdog timer.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#ifndef UDS_SERVER_H
#define UDS_SERVER_H

#include "hal/Platform.h"
#include "services/ErrorHandler.h"
#include "transport/IsoTp.h"
#include "uds/UdsTypes.h"

/* =========================================================================
 * Limits
 * ========================================================================= */
#define UDS_SERVER_MAX_DTCS         16U    /**< Maximum stored DTCs.       */
#define UDS_SERVER_MAX_DIDS         24U    /**< Maximum registered DIDs.   */
#define UDS_SERVER_SA_MAX_ATTEMPTS   3U    /**< Max failed SA attempts.    */
#define UDS_SERVER_S3_TIMEOUT_MS  5000U    /**< S3 session timeout (ms).   */

/* =========================================================================
 * Security access state
 * ========================================================================= */
typedef enum UdsSecurityState
{
    UDS_SEC_LOCKED        = 0U,   /**< No valid key received.             */
    UDS_SEC_SEED_SENT     = 1U,   /**< Seed sent; awaiting key.           */
    UDS_SEC_UNLOCKED      = 2U,   /**< Valid key received; access granted.*/
    UDS_SEC_DELAY_PENDING = 3U,   /**< Too many attempts; delay active.   */
} UdsSecurityState;

/* =========================================================================
 * UDS Server configuration
 * ========================================================================= */
typedef struct UdsServerConfig
{
    uint32_t   ecuCanTxId;     /**< CAN ID for ECU → Tester messages.    */
    uint32_t   ecuCanRxId;     /**< CAN ID for Tester → ECU messages.    */
    uint8_t    ecuAddress;     /**< ECU logical address (for NRC frames). */
    const char *ecuName;       /**< Human-readable ECU name string.       */
} UdsServerConfig;

/* =========================================================================
 * UDS Server context (opaque to application — use API functions)
 * ========================================================================= */
typedef struct UdsServer
{
    UdsServerConfig   config;
    IsoTpChannel      isotp;           /**< ISO-TP channel for this ECU.  */

    UdsSession        activeSession;   /**< Current diagnostic session.   */
    UdsSecurityState  secState;        /**< Security access state.        */
    uint32_t          secSeed;         /**< Last generated seed.          */
    uint8_t           secAttempts;     /**< Failed key attempts.          */
    uint32_t          secDelayMs;      /**< Remaining lockout delay.      */

    DtcRecord         dtcTable[UDS_SERVER_MAX_DTCS]; /**< DTC storage.   */
    uint8_t           dtcCount;        /**< Active DTC count.             */

    DidRecord         didTable[UDS_SERVER_MAX_DIDS]; /**< DID table.     */
    uint8_t           didCount;        /**< Registered DID count.         */

    uint32_t          s3TimerMs;       /**< S3 session-keep-alive timer.  */
    bool              testerPresentReceived; /**< Reset S3 timer.         */

    bool              initialised;
} UdsServer;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief  Initialise the UDS server.
 * @param  server  Caller-allocated server context.
 * @param  config  Server configuration.
 * @return ECUS_OK on success.
 */
EcusStatus UdsServer_Init(UdsServer            *server,
                           const UdsServerConfig *config);

/**
 * @brief  Deinitialise the UDS server and free resources.
 * @param  server  Initialised server context.
 */
void UdsServer_Deinit(UdsServer *server);

/**
 * @brief  Feed a raw CAN frame to the UDS server for processing.
 *         This is the CanHal Rx callback entry point.
 * @param  server  UDS server context.
 * @param  frame   Received CAN frame.
 * @return ECUS_OK on success.
 */
EcusStatus UdsServer_ProcessCanFrame(UdsServer      *server,
                                      const CanFrame *frame);

/**
 * @brief  Periodic tick — call every 1 ms to drive timers and watchdogs.
 * @param  server     UDS server context.
 * @param  elapsedMs  Milliseconds since last tick.
 */
void UdsServer_Tick(UdsServer *server, uint32_t elapsedMs);

/**
 * @brief  Register a DID (Data Identifier) with its current value.
 * @param  server   UDS server context.
 * @param  did      2-byte DID code.
 * @param  data     Data bytes.
 * @param  dataLen  Number of data bytes.
 * @param  label    Human-readable label (optional, may be NULL).
 * @return ECUS_OK on success.
 */
EcusStatus UdsServer_RegisterDid(UdsServer     *server,
                                  uint16_t       did,
                                  const uint8_t *data,
                                  uint8_t        dataLen,
                                  const char    *label);

/**
 * @brief  Store a DTC in the server's DTC table.
 * @param  server  UDS server context.
 * @param  code    3-byte DTC code.
 * @param  status  DTC status mask.
 * @param  label   Human-readable description.
 * @return ECUS_OK on success.
 */
EcusStatus UdsServer_StoreDtc(UdsServer     *server,
                                uint32_t       code,
                                DtcStatusMask  status,
                                const char    *label);

/**
 * @brief  Clear all DTCs from the server's DTC table.
 * @param  server  UDS server context.
 */
void UdsServer_ClearAllDtcs(UdsServer *server);

/**
 * @brief  Print a summary of the server state to the logger.
 * @param  server  UDS server context.
 */
void UdsServer_PrintStatus(const UdsServer *server);

#endif /* UDS_SERVER_H */
