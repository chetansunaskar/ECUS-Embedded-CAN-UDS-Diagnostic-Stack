/**
 * @file    UdsDispatcher.c
 * @brief   UDS service dispatcher and all service handler implementations.
 *
 * C concepts demonstrated:
 *   Dispatch table (array of function pointers),
 *   function pointers (UdsServiceHandlerFn),
 *   seed/key security algorithm (XOR-based for simulation),
 *   linear search on const struct array,
 *   bit manipulation (session flags, security state),
 *   pointer-to-const (req), pointer-to-non-const (resp, respLen),
 *   safe memcpy for response building,
 *   stdint exact-width types throughout.
 *
 * Services implemented:
 *   0x10  DiagnosticSessionControl
 *   0x11  ECUReset
 *   0x22  ReadDataByIdentifier
 *   0x27  SecurityAccess
 *   0x14  ClearDiagnosticInformation
 *   0x19  ReadDTCInformation
 *   0x2E  WriteDataByIdentifier
 *   0x3E  TesterPresent
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#include "uds/UdsDispatcher.h"
#include "uds/UdsServer.h"
#include "transport/IsoTp.h"
#include "services/Logger.h"

#include <string.h>
#include <time.h>    /* for srand/rand seed */
#include <stdlib.h>

/* =========================================================================
 * Private: Response builder helpers
 * ========================================================================= */

/**
 * @brief  Write a positive response header into the response buffer.
 *         Positive response SID = request SID | 0x40.
 */
static size_t BuildPositiveHeader(uint8_t *resp, uint8_t reqSid)
{
    resp[0] = UDS_POSITIVE_RESPONSE_SID(reqSid);
    return 1U;
}

/* =========================================================================
 * Private: Session prerequisite checker
 *
 * Returns a UdsNrc: UDS_NRC_POSITIVE_RESPONSE (0x00) means "allowed".
 * ========================================================================= */
static UdsNrc CheckSessionAccess(const UdsServer      *server,
                                  const ServiceEntry   *entry)
{
    /* Map active session to its flag bit. */
    uint8_t sessionFlag;
    switch (server->activeSession)
    {
        case UDS_SESSION_DEFAULT:      sessionFlag = UDS_SESSION_FLAG_DEFAULT;     break;
        case UDS_SESSION_PROGRAMMING:  sessionFlag = UDS_SESSION_FLAG_PROGRAMMING; break;
        case UDS_SESSION_EXTENDED:     sessionFlag = UDS_SESSION_FLAG_EXTENDED;    break;
        default:                       sessionFlag = 0U; break;
    }

    if ((entry->sessionMask & sessionFlag) == 0U)
    {
        return UDS_NRC_SERVICE_NOT_SUPPORTED_IN_SESSION;
    }

    if (entry->requiresSecurity && (server->secState != UDS_SEC_UNLOCKED))
    {
        return UDS_NRC_SECURITY_ACCESS_DENIED;
    }

    return UDS_NRC_POSITIVE_RESPONSE;  /* 0x00 = allowed */
}

/* =========================================================================
 * Service Handlers (static — private to this translation unit)
 * ========================================================================= */

/* --- 0x10  DiagnosticSessionControl ------------------------------------ */
static EcusStatus Handle_DiagnosticSessionControl(UdsServer     *server,
                                                   const uint8_t *req,
                                                   size_t         reqLen,
                                                   uint8_t       *resp,
                                                   size_t        *respLen)
{
    if (reqLen < 2U)
    {
        return ECUS_ERR_UDS_WRONG_LEN;
    }

    uint8_t subFunc = req[1] & 0x7FU;   /* Strip suppressPosRspMsgIndicatorBit */

    switch (subFunc)
    {
        case UDS_SESSION_DEFAULT:
        case UDS_SESSION_PROGRAMMING:
        case UDS_SESSION_EXTENDED:
            server->activeSession = (UdsSession)subFunc;

            /* Reset security on session change. */
            if (subFunc == UDS_SESSION_DEFAULT)
            {
                server->secState    = UDS_SEC_LOCKED;
                server->secAttempts = 0U;
            }

            server->s3TimerMs = 0U;
            LOG_INFO("Session changed to 0x%02X (%s)", subFunc,
                     (subFunc == 0x01U) ? "Default"     :
                     (subFunc == 0x02U) ? "Programming" : "Extended");
            break;

        default:
            LOG_WARN("0x10: unsupported sub-function 0x%02X", subFunc);
            return ECUS_ERR_UDS_SERVICE_NA;
    }

    size_t idx = BuildPositiveHeader(resp, req[0]);
    resp[idx++] = subFunc;
    /* P2 server max timer bytes (P2=25ms, P2*=5000ms). */
    resp[idx++] = 0x00U;
    resp[idx++] = 0x19U;   /* P2 = 25 ms */
    resp[idx++] = 0x01U;
    resp[idx++] = 0xF4U;   /* P2* = 500 ms */
    *respLen = idx;

    return ECUS_OK;
}

/* --- 0x11  ECUReset ---------------------------------------------------- */
static EcusStatus Handle_EcuReset(UdsServer     *server,
                                   const uint8_t *req,
                                   size_t         reqLen,
                                   uint8_t       *resp,
                                   size_t        *respLen)
{
    if (reqLen < 2U)
    {
        return ECUS_ERR_UDS_WRONG_LEN;
    }

    uint8_t resetType = req[1];

    switch (resetType)
    {
        case UDS_RESET_HARD:
            LOG_INFO("ECU Reset: HARD reset requested"); break;
        case UDS_RESET_KEY_OFF_ON:
            LOG_INFO("ECU Reset: Key-Off-On reset requested"); break;
        case UDS_RESET_SOFT:
            LOG_INFO("ECU Reset: Soft reset requested"); break;
        default:
            return ECUS_ERR_UDS_SERVICE_NA;
    }

    /* Simulate reset: return to default session, lock security. */
    server->activeSession = UDS_SESSION_DEFAULT;
    server->secState      = UDS_SEC_LOCKED;
    server->s3TimerMs     = 0U;

    size_t idx = BuildPositiveHeader(resp, req[0]);
    resp[idx++] = resetType;
    *respLen = idx;

    return ECUS_OK;
}

/* --- 0x27  SecurityAccess ---------------------------------------------- */
static EcusStatus Handle_SecurityAccess(UdsServer     *server,
                                         const uint8_t *req,
                                         size_t         reqLen,
                                         uint8_t       *resp,
                                         size_t        *respLen)
{
    if (reqLen < 2U)
    {
        return ECUS_ERR_UDS_WRONG_LEN;
    }

    /* Lockout check. */
    if (server->secState == UDS_SEC_DELAY_PENDING)
    {
        LOG_WARN("SecurityAccess: lockout active (%u ms remaining)",
                 server->secDelayMs);
        return ECUS_ERR_UDS_COND_NOT_MET;
    }

    uint8_t subFunc = req[1];

    if (subFunc == UDS_SA_REQUEST_SEED_LEVEL1)
    {
        /* Already unlocked — return seed of 0x00000000. */
        if (server->secState == UDS_SEC_UNLOCKED)
        {
            size_t idx = BuildPositiveHeader(resp, req[0]);
            resp[idx++] = subFunc;
            resp[idx++] = 0x00U;
            resp[idx++] = 0x00U;
            resp[idx++] = 0x00U;
            resp[idx++] = 0x00U;
            *respLen = idx;
            return ECUS_OK;
        }

        /* Generate a pseudo-random 32-bit seed. */
        uint32_t seed = (uint32_t)((uint32_t)rand() ^ (uint32_t)time(NULL));
        server->secSeed  = seed;
        server->secState = UDS_SEC_SEED_SENT;

        LOG_DEBUG("SecurityAccess: seed generated = 0x%08X", seed);

        size_t idx = BuildPositiveHeader(resp, req[0]);
        resp[idx++] = subFunc;
        resp[idx++] = (uint8_t)((seed >> 24U) & 0xFFU);
        resp[idx++] = (uint8_t)((seed >> 16U) & 0xFFU);
        resp[idx++] = (uint8_t)((seed >>  8U) & 0xFFU);
        resp[idx++] = (uint8_t)( seed          & 0xFFU);
        *respLen = idx;
    }
    else if (subFunc == UDS_SA_SEND_KEY_LEVEL1)
    {
        if (server->secState != UDS_SEC_SEED_SENT)
        {
            return ECUS_ERR_UDS_SEQ_ERROR;
        }
        if (reqLen < 6U)
        {
            return ECUS_ERR_UDS_WRONG_LEN;
        }

        /* Extract received key (4 bytes). */
        uint32_t rxKey = ((uint32_t)req[2] << 24U) |
                         ((uint32_t)req[3] << 16U) |
                         ((uint32_t)req[4] <<  8U) |
                          (uint32_t)req[5];

        /*
         * ECUS key algorithm (demonstrative):
         *   expected_key = ~seed XOR 0xA5A5A5A5
         * In production: HMAC-SHA256 or manufacturer-specific algorithm.
         */
        uint32_t expectedKey = (~server->secSeed) ^ 0xA5A5A5A5UL;

        LOG_DEBUG("SecurityAccess: rxKey=0x%08X  expected=0x%08X",
                  rxKey, expectedKey);

        if (rxKey == expectedKey)
        {
            server->secState    = UDS_SEC_UNLOCKED;
            server->secAttempts = 0U;
            LOG_INFO("SecurityAccess: UNLOCKED");

            size_t idx = BuildPositiveHeader(resp, req[0]);
            resp[idx++] = subFunc;
            *respLen = idx;
        }
        else
        {
            server->secAttempts++;
            LOG_WARN("SecurityAccess: invalid key (attempt %u/%u)",
                     server->secAttempts, UDS_SERVER_SA_MAX_ATTEMPTS);

            if (server->secAttempts >= UDS_SERVER_SA_MAX_ATTEMPTS)
            {
                server->secState   = UDS_SEC_DELAY_PENDING;
                server->secDelayMs = 10000U;  /* 10-second lockout */
                LOG_WARN("SecurityAccess: max attempts reached — 10 s lockout");
                return ECUS_ERR_UDS_SECURITY;
            }

            return ECUS_ERR_UDS_SECURITY;
        }
    }
    else
    {
        return ECUS_ERR_UDS_SERVICE_NA;
    }

    return ECUS_OK;
}

/* --- 0x22  ReadDataByIdentifier ---------------------------------------- */
static EcusStatus Handle_ReadDataById(UdsServer     *server,
                                       const uint8_t *req,
                                       size_t         reqLen,
                                       uint8_t       *resp,
                                       size_t        *respLen)
{
    if (reqLen < 3U)
    {
        return ECUS_ERR_UDS_WRONG_LEN;
    }

    uint16_t did = (uint16_t)(((uint16_t)req[1] << 8U) | (uint16_t)req[2]);

    /* Linear search in DID table. */
    for (uint8_t i = 0U; i < server->didCount; i++)
    {
        if (server->didTable[i].did == did)
        {
            const DidRecord *rec = &server->didTable[i];
            size_t idx = BuildPositiveHeader(resp, req[0]);
            resp[idx++] = (uint8_t)((did >> 8U) & 0xFFU);
            resp[idx++] = (uint8_t)( did         & 0xFFU);
            (void)memcpy(&resp[idx], rec->data, rec->dataLen);
            idx += rec->dataLen;
            *respLen = idx;

            LOG_DEBUG("ReadDataById: DID=0x%04X  len=%u  '%s'",
                      did, rec->dataLen, rec->label);
            return ECUS_OK;
        }
    }

    LOG_WARN("ReadDataById: DID 0x%04X not found", did);
    return ECUS_ERR_UDS_OUT_OF_RANGE;
}

/* --- 0x2E  WriteDataByIdentifier --------------------------------------- */
static EcusStatus Handle_WriteDataById(UdsServer     *server,
                                        const uint8_t *req,
                                        size_t         reqLen,
                                        uint8_t       *resp,
                                        size_t        *respLen)
{
    if (reqLen < 4U)
    {
        return ECUS_ERR_UDS_WRONG_LEN;
    }

    uint16_t did     = (uint16_t)(((uint16_t)req[1] << 8U) | (uint16_t)req[2]);
    size_t   dataLen = reqLen - 3U;  /* Remaining bytes after DID */

    for (uint8_t i = 0U; i < server->didCount; i++)
    {
        if (server->didTable[i].did == did)
        {
            DidRecord *rec = &server->didTable[i];
            uint8_t writeLen = (uint8_t)ECUS_MIN(dataLen, sizeof(rec->data));
            (void)memcpy(rec->data, &req[3], writeLen);
            rec->dataLen = writeLen;

            LOG_INFO("WriteDataById: DID=0x%04X  written %u bytes", did, writeLen);

            size_t idx = BuildPositiveHeader(resp, req[0]);
            resp[idx++] = (uint8_t)((did >> 8U) & 0xFFU);
            resp[idx++] = (uint8_t)( did         & 0xFFU);
            *respLen = idx;
            return ECUS_OK;
        }
    }

    return ECUS_ERR_UDS_OUT_OF_RANGE;
}

/* --- 0x14  ClearDiagnosticInformation ---------------------------------- */
static EcusStatus Handle_ClearDtc(UdsServer     *server,
                                   const uint8_t *req,
                                   size_t         reqLen,
                                   uint8_t       *resp,
                                   size_t        *respLen)
{
    if (reqLen < 4U)
    {
        return ECUS_ERR_UDS_WRONG_LEN;
    }

    /* Group of DTC = 3 bytes.  0xFFFFFF = clear all. */
    uint32_t group = ((uint32_t)req[1] << 16U) |
                     ((uint32_t)req[2] <<  8U) |
                      (uint32_t)req[3];

    LOG_INFO("ClearDTC: group=0x%06X", group);

    if (group == 0xFFFFFFU)
    {
        UdsServer_ClearAllDtcs(server);
    }
    /* else: selective clear (not implemented in this demo). */

    size_t idx = BuildPositiveHeader(resp, req[0]);
    *respLen = idx;
    return ECUS_OK;
}

/* --- 0x19  ReadDTCInformation ------------------------------------------ */
static EcusStatus Handle_ReadDtc(UdsServer     *server,
                                  const uint8_t *req,
                                  size_t         reqLen,
                                  uint8_t       *resp,
                                  size_t        *respLen)
{
    if (reqLen < 3U)
    {
        return ECUS_ERR_UDS_WRONG_LEN;
    }

    uint8_t subFunc    = req[1];
    uint8_t statusMask = req[2];

    size_t idx = BuildPositiveHeader(resp, req[0]);
    resp[idx++] = subFunc;

    /*
     * Sub-function 0x02: reportDTCByStatusMask
     * Returns all DTCs whose status byte has at least one bit in common
     * with the requested mask.
     */
    if (subFunc == 0x02U)
    {
        resp[idx++] = statusMask;  /* DTCStatusAvailabilityMask */

        for (uint8_t i = 0U; i < server->dtcCount; i++)
        {
            const DtcRecord *d = &server->dtcTable[i];
            if ((d->status.raw & statusMask) != 0U)
            {
                /* 3-byte DTC code + 1-byte status. */
                resp[idx++] = (uint8_t)((d->dtcCode >> 16U) & 0xFFU);
                resp[idx++] = (uint8_t)((d->dtcCode >>  8U) & 0xFFU);
                resp[idx++] = (uint8_t)( d->dtcCode          & 0xFFU);
                resp[idx++] = d->status.raw;
            }
        }

        LOG_DEBUG("ReadDTC 0x02: mask=0x%02X  returned %zu bytes", statusMask, idx);
    }
    else
    {
        /* Sub-function not supported. */
        return ECUS_ERR_UDS_SERVICE_NA;
    }

    *respLen = idx;
    return ECUS_OK;
}

/* --- 0x3E  TesterPresent ----------------------------------------------- */
static EcusStatus Handle_TesterPresent(UdsServer     *server,
                                        const uint8_t *req,
                                        size_t         reqLen,
                                        uint8_t       *resp,
                                        size_t        *respLen)
{
    ECUS_UNUSED_PARAM(reqLen);

    server->testerPresentReceived = true;
    server->s3TimerMs             = 0U;

    uint8_t subFunc = (reqLen >= 2U) ? (req[1] & 0x7FU) : 0x00U;

    /* Sub-function 0x00 = zeroSubFunction (respond). */
    /* Sub-function 0x80 = suppressPosRspMsg (no response sent). */
    if ((reqLen >= 2U) && ((req[1] & 0x80U) != 0U))
    {
        *respLen = 0U;  /* Suppress positive response. */
        return ECUS_OK;
    }

    size_t idx = BuildPositiveHeader(resp, req[0]);
    resp[idx++] = subFunc;
    *respLen = idx;

    LOG_TRACE("TesterPresent: S3 timer reset");
    return ECUS_OK;
}

/* =========================================================================
 * Dispatch table — the core architectural pattern of this module.
 *
 * Array of ServiceEntry structs, each containing:
 *   - SID
 *   - Function pointer to handler
 *   - Bitmask of permitted sessions
 *   - Security requirement flag
 *   - Human-readable name for logging
 *
 * Adding a new service = adding ONE row to this table.  No if/else chains.
 * ========================================================================= */
static const ServiceEntry k_serviceTable[] =
{
    /* SID    Handler                          Sessions               SecReq  Name */
    { UDS_SID_DIAGNOSTIC_SESSION_CONTROL,
      Handle_DiagnosticSessionControl,
      UDS_SESSION_FLAG_ALL,            false,  "DiagnosticSessionControl" },

    { UDS_SID_ECU_RESET,
      Handle_EcuReset,
      UDS_SESSION_FLAG_DEFAULT | UDS_SESSION_FLAG_PROGRAMMING | UDS_SESSION_FLAG_EXTENDED,
      false,  "ECUReset" },

    { UDS_SID_SECURITY_ACCESS,
      Handle_SecurityAccess,
      UDS_SESSION_FLAG_EXTENDED | UDS_SESSION_FLAG_PROGRAMMING,
      false,  "SecurityAccess" },

    { UDS_SID_READ_DATA_BY_ID,
      Handle_ReadDataById,
      UDS_SESSION_FLAG_ALL,            false,  "ReadDataByIdentifier" },

    { UDS_SID_WRITE_DATA_BY_ID,
      Handle_WriteDataById,
      UDS_SESSION_FLAG_EXTENDED | UDS_SESSION_FLAG_PROGRAMMING,
      true,   "WriteDataByIdentifier" },

    { UDS_SID_CLEAR_DTC,
      Handle_ClearDtc,
      UDS_SESSION_FLAG_ALL,            false,  "ClearDiagnosticInformation" },

    { UDS_SID_READ_DTC_INFO,
      Handle_ReadDtc,
      UDS_SESSION_FLAG_ALL,            false,  "ReadDTCInformation" },

    { UDS_SID_TESTER_PRESENT,
      Handle_TesterPresent,
      UDS_SESSION_FLAG_ALL,            false,  "TesterPresent" },
};

static const size_t k_serviceTableCount = ECUS_ARRAY_SIZE(k_serviceTable);

/* =========================================================================
 * Public API
 * ========================================================================= */

EcusStatus UdsDispatcher_Dispatch(UdsServer     *server,
                                   const uint8_t *pdu,
                                   size_t         pduLen)
{
    ECUS_CHECK(server != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(pdu    != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(pduLen >= 1U,   ECUS_ERR_INVALID_PARAM, return ECUS_ERR_INVALID_PARAM);

    uint8_t sid = pdu[0];

    /* Reset S3 timer on any received service request. */
    server->testerPresentReceived = true;

    /* Linear search in dispatch table (table is small; O(n) is fine). */
    for (size_t i = 0U; i < k_serviceTableCount; i++)
    {
        if (k_serviceTable[i].sid == (UdsSid)sid)
        {
            const ServiceEntry *entry = &k_serviceTable[i];

            LOG_DEBUG("Dispatching SID=0x%02X  (%s)", sid, entry->name);

            /* Check session and security prerequisites. */
            UdsNrc nrc = CheckSessionAccess(server, entry);
            if (nrc != UDS_NRC_POSITIVE_RESPONSE)
            {
                LOG_WARN("SID=0x%02X denied: NRC=0x%02X", sid, nrc);
                return UdsDispatcher_SendNegativeResponse(server, sid, nrc);
            }

            /* Build response in a local buffer. */
            uint8_t respBuf[256U];
            size_t  respLen = 0U;

            (void)memset(respBuf, 0, sizeof(respBuf));

            /* Invoke the handler via function pointer. */
            EcusStatus rc = entry->handler(server, pdu, pduLen,
                                            respBuf, &respLen);

            if (rc != ECUS_OK)
            {
                /* Map internal error code to UDS NRC. */
                UdsNrc errNrc;
                switch (rc)
                {
                    case ECUS_ERR_UDS_WRONG_LEN:
                        errNrc = UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT; break;
                    case ECUS_ERR_UDS_SERVICE_NA:
                        errNrc = UDS_NRC_SUBFUNCTION_NOT_SUPPORTED;   break;
                    case ECUS_ERR_UDS_SECURITY:
                        errNrc = UDS_NRC_INVALID_KEY;                  break;
                    case ECUS_ERR_UDS_OUT_OF_RANGE:
                        errNrc = UDS_NRC_REQUEST_OUT_OF_RANGE;         break;
                    case ECUS_ERR_UDS_COND_NOT_MET:
                        errNrc = UDS_NRC_CONDITIONS_NOT_CORRECT;       break;
                    case ECUS_ERR_UDS_SEQ_ERROR:
                        errNrc = UDS_NRC_REQUEST_SEQUENCE_ERROR;       break;
                    default:
                        errNrc = UDS_NRC_GENERAL_REJECT;               break;
                }
                return UdsDispatcher_SendNegativeResponse(server, sid, errNrc);
            }

            /* Send positive response via ISO-TP. */
            if (respLen > 0U)
            {
                EcusStatus txRc = IsoTp_Transmit(&server->isotp,
                                                   respBuf, respLen);
                if (txRc != ECUS_OK)
                {
                    LOG_ERROR("ISO-TP Transmit failed: %s",
                              ErrorHandler_StatusStr(txRc));
                    return txRc;
                }
                LOG_DEBUG("Response sent: SID=0x%02X  len=%zu", sid, respLen);
            }

            return ECUS_OK;
        }
    }

    /* SID not found in table. */
    LOG_WARN("Unknown SID=0x%02X", sid);
    return UdsDispatcher_SendNegativeResponse(server, sid,
                                               UDS_NRC_SERVICE_NOT_SUPPORTED);
}

/* -------------------------------------------------------------------------- */

EcusStatus UdsDispatcher_SendNegativeResponse(UdsServer *server,
                                               uint8_t    reqSid,
                                               UdsNrc     nrc)
{
    uint8_t nrBuf[3];
    nrBuf[0] = (uint8_t)UDS_SID_NEGATIVE_RESPONSE;
    nrBuf[1] = reqSid;
    nrBuf[2] = (uint8_t)nrc;

    LOG_WARN("NRC sent: SID=0x%02X  NRC=0x%02X", reqSid, nrc);

    return IsoTp_Transmit(&server->isotp, nrBuf, 3U);
}
