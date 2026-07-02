/**
 * @file    UdsServer.c
 * @brief   UDS server implementation — top-level integration.
 *
 * C concepts demonstrated:
 *   Callback registration and invocation (ISO-TP PDU callback),
 *   struct member access and update,
 *   string handling (strncpy safe pattern),
 *   timer management (uint32_t ms counters),
 *   linear search on struct array (DID / DTC lookup),
 *   session/security state management.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#include "uds/UdsServer.h"
#include "uds/UdsDispatcher.h"
#include "hal/CanHal.h"
#include "services/Logger.h"

#include <string.h>

/* =========================================================================
 * Private: ISO-TP PDU callback
 *
 * This is the function pointer registered with the ISO-TP channel.
 * It is called by IsoTp_ProcessRxFrame() when a complete UDS PDU has
 * been reassembled from CAN frames.
 * ========================================================================= */
static void OnPduReceived(const uint8_t *pdu, size_t pduLen, void *userCtx)
{
    UdsServer *server = (UdsServer *)userCtx;

    ECUS_ASSERT(server != NULL);
    ECUS_ASSERT(pdu    != NULL);
    ECUS_ASSERT(pduLen > 0U);

    LOG_DEBUG("UDS PDU received: len=%zu  SID=0x%02X", pduLen, pdu[0]);

    /* Dispatch to the service handler table. */
    EcusStatus rc = UdsDispatcher_Dispatch(server, pdu, pduLen);
    if (rc != ECUS_OK)
    {
        LOG_WARN("UdsDispatcher returned: %s", ErrorHandler_StatusStr(rc));
    }
}

/* =========================================================================
 * Public API
 * ========================================================================= */

EcusStatus UdsServer_Init(UdsServer *server, const UdsServerConfig *config)
{
    ECUS_CHECK(server != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(config != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(!server->initialised, ECUS_ERR_ALREADY_INIT,
               return ECUS_ERR_ALREADY_INIT);

    /* Zero the entire context for safety. */
    (void)memset(server, 0, sizeof(UdsServer));

    /* Copy configuration. */
    server->config = *config;

    /* Set initial state. */
    server->activeSession = UDS_SESSION_DEFAULT;
    server->secState      = UDS_SEC_LOCKED;
    server->secSeed       = 0U;
    server->secAttempts   = 0U;
    server->s3TimerMs     = 0U;
    server->dtcCount      = 0U;
    server->didCount      = 0U;

    /* Initialise ISO-TP channel.
     * The OnPduReceived callback passes 'server' as userCtx — classic
     * callback context pattern used throughout embedded middleware.        */
    ECUS_PROPAGATE(IsoTp_Init(&server->isotp,
                               config->ecuCanTxId,
                               config->ecuCanRxId,
                               OnPduReceived,
                               server));

    server->initialised = true;

    LOG_INFO("UDS Server '%s' initialised: TX=0x%03X  RX=0x%03X  Addr=0x%02X",
             (config->ecuName != NULL) ? config->ecuName : "?",
             config->ecuCanTxId,
             config->ecuCanRxId,
             config->ecuAddress);

    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

void UdsServer_Deinit(UdsServer *server)
{
    if ((server == NULL) || !server->initialised)
    {
        return;
    }

    server->initialised = false;
    LOG_INFO("UDS Server deinitialised");
}

/* -------------------------------------------------------------------------- */

EcusStatus UdsServer_ProcessCanFrame(UdsServer *server, const CanFrame *frame)
{
    ECUS_CHECK(server != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(frame  != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(server->initialised, ECUS_ERR_NOT_READY, return ECUS_ERR_NOT_READY);

    /* Hand the CAN frame to the ISO-TP layer for reassembly. */
    return IsoTp_ProcessRxFrame(&server->isotp, frame);
}

/* -------------------------------------------------------------------------- */

void UdsServer_Tick(UdsServer *server, uint32_t elapsedMs)
{
    if ((server == NULL) || !server->initialised)
    {
        return;
    }

    /* Drive ISO-TP Tx state machine (sends pending CF frames). */
    (void)IsoTp_TxPump(&server->isotp, elapsedMs);

    /* S3 session watchdog:
     * If no TesterPresent or service request is received within S3 timeout,
     * fall back to Default session.                                        */
    if (server->activeSession != UDS_SESSION_DEFAULT)
    {
        server->s3TimerMs += elapsedMs;

        if (server->testerPresentReceived)
        {
            server->s3TimerMs          = 0U;
            server->testerPresentReceived = false;
        }

        if (server->s3TimerMs >= UDS_SERVER_S3_TIMEOUT_MS)
        {
            LOG_INFO("UDS Server: S3 timeout — returning to Default Session");
            server->activeSession = UDS_SESSION_DEFAULT;
            server->secState      = UDS_SEC_LOCKED;
            server->s3TimerMs     = 0U;
        }
    }

    /* Security lockout delay countdown. */
    if (server->secState == UDS_SEC_DELAY_PENDING)
    {
        if (server->secDelayMs > elapsedMs)
        {
            server->secDelayMs -= elapsedMs;
        }
        else
        {
            server->secDelayMs  = 0U;
            server->secState    = UDS_SEC_LOCKED;
            server->secAttempts = 0U;
            LOG_INFO("UDS Server: security lockout expired — attempts reset");
        }
    }
}

/* -------------------------------------------------------------------------- */

EcusStatus UdsServer_RegisterDid(UdsServer     *server,
                                  uint16_t       did,
                                  const uint8_t *data,
                                  uint8_t        dataLen,
                                  const char    *label)
{
    ECUS_CHECK(server  != NULL,   ECUS_ERR_NULL_PTR,      return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(data    != NULL,   ECUS_ERR_NULL_PTR,      return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(dataLen >  0U,     ECUS_ERR_INVALID_PARAM, return ECUS_ERR_INVALID_PARAM);
    ECUS_CHECK(dataLen <= 64U,    ECUS_ERR_OVERFLOW,      return ECUS_ERR_OVERFLOW);
    ECUS_CHECK(server->didCount < UDS_SERVER_MAX_DIDS,
               ECUS_ERR_OVERFLOW, return ECUS_ERR_OVERFLOW);

    DidRecord *rec = &server->didTable[server->didCount];
    rec->did       = did;
    rec->dataLen   = dataLen;
    (void)memcpy(rec->data, data, dataLen);

    if (label != NULL)
    {
        (void)strncpy(rec->label, label, sizeof(rec->label) - 1U);
        rec->label[sizeof(rec->label) - 1U] = '\0';
    }

    server->didCount++;

    LOG_DEBUG("DID 0x%04X registered: '%s'  len=%u", did,
              (label != NULL) ? label : "?", dataLen);
    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

EcusStatus UdsServer_StoreDtc(UdsServer     *server,
                                uint32_t       code,
                                DtcStatusMask  status,
                                const char    *label)
{
    ECUS_CHECK(server != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(server->dtcCount < UDS_SERVER_MAX_DTCS,
               ECUS_ERR_OVERFLOW, return ECUS_ERR_OVERFLOW);

    DtcRecord *rec = &server->dtcTable[server->dtcCount];
    rec->dtcCode   = code;
    rec->status    = status;
    rec->severity  = 0x20U;   /* checkAtNextHalt */

    if (label != NULL)
    {
        (void)strncpy(rec->label, label, sizeof(rec->label) - 1U);
        rec->label[sizeof(rec->label) - 1U] = '\0';
    }

    server->dtcCount++;

    LOG_WARN("DTC stored: 0x%06X  status=0x%02X  '%s'",
             code, status.raw, (label != NULL) ? label : "?");
    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

void UdsServer_ClearAllDtcs(UdsServer *server)
{
    if (server == NULL) { return; }

    (void)memset(server->dtcTable, 0, sizeof(server->dtcTable));
    server->dtcCount = 0U;
    LOG_INFO("UDS Server: all DTCs cleared");
}

/* -------------------------------------------------------------------------- */

void UdsServer_PrintStatus(const UdsServer *server)
{
    static const char * const k_sessionStr[] =
    {
        "?", "Default", "Programming", "Extended"
    };
    static const char * const k_secStr[] =
    {
        "Locked", "SeedSent", "Unlocked", "DelayPending"
    };

    if (server == NULL) { return; }

    uint8_t sessIdx = (uint8_t)server->activeSession;
    uint8_t secIdx  = (uint8_t)server->secState;

    LOG_INFO("=== UDS Server Status: '%s' ===",
             (server->config.ecuName != NULL) ? server->config.ecuName : "?");
    LOG_INFO("  Session  : %s",
             (sessIdx < ECUS_ARRAY_SIZE(k_sessionStr)) ? k_sessionStr[sessIdx] : "?");
    LOG_INFO("  Security : %s  (attempts=%u)",
             (secIdx < ECUS_ARRAY_SIZE(k_secStr)) ? k_secStr[secIdx] : "?",
             server->secAttempts);
    LOG_INFO("  DTCs     : %u stored", server->dtcCount);
    LOG_INFO("  DIDs     : %u registered", server->didCount);
    LOG_INFO("  S3 timer : %u ms", server->s3TimerMs);

    /* Print DTC list. */
    for (uint8_t i = 0U; i < server->dtcCount; i++)
    {
        const DtcRecord *d = &server->dtcTable[i];
        LOG_INFO("    DTC[%u] 0x%06X  status=0x%02X  '%s'",
                 i, d->dtcCode, d->status.raw, d->label);
    }
}
