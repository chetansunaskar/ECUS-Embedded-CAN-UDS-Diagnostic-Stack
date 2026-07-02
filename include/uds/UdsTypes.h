/**
 * @file    UdsTypes.h
 * @brief   UDS (ISO 14229-1) protocol type definitions, service IDs, and NRCs.
 *
 * This header is the single source of truth for all UDS protocol constants.
 * It is intentionally a pure "types" header — no functions, no state.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#ifndef UDS_TYPES_H
#define UDS_TYPES_H

#include "hal/Platform.h"

/* =========================================================================
 * UDS Service Identifiers (SID)
 * ========================================================================= */
typedef enum UdsSid
{
    /* Session & security */
    UDS_SID_DIAGNOSTIC_SESSION_CONTROL  = 0x10U,
    UDS_SID_ECU_RESET                   = 0x11U,
    UDS_SID_SECURITY_ACCESS             = 0x27U,
    UDS_SID_COMMUNICATION_CONTROL       = 0x28U,
    UDS_SID_TESTER_PRESENT              = 0x3EU,
    UDS_SID_ACCESS_TIMING_PARAMS        = 0x83U,

    /* Data */
    UDS_SID_READ_DATA_BY_ID             = 0x22U,
    UDS_SID_READ_MEM_BY_ADDR            = 0x23U,
    UDS_SID_READ_SCALING_DATA           = 0x24U,
    UDS_SID_READ_DATA_BY_PERIODIC_ID    = 0x2AU,
    UDS_SID_WRITE_DATA_BY_ID            = 0x2EU,
    UDS_SID_WRITE_MEM_BY_ADDR           = 0x3DU,

    /* DTC */
    UDS_SID_CLEAR_DTC                   = 0x14U,
    UDS_SID_READ_DTC_INFO               = 0x19U,

    /* Routine */
    UDS_SID_ROUTINE_CONTROL             = 0x31U,

    /* Upload/Download */
    UDS_SID_REQUEST_DOWNLOAD            = 0x34U,
    UDS_SID_REQUEST_UPLOAD              = 0x35U,
    UDS_SID_TRANSFER_DATA               = 0x36U,
    UDS_SID_REQUEST_TRANSFER_EXIT       = 0x37U,

    /* Positive response offset (SID | 0x40). */
    UDS_SID_POSITIVE_RESPONSE_OFFSET    = 0x40U,

    /* Negative response SID. */
    UDS_SID_NEGATIVE_RESPONSE           = 0x7FU,
} UdsSid;

/* =========================================================================
 * UDS Negative Response Codes (NRC)
 * ========================================================================= */
typedef enum UdsNrc
{
    UDS_NRC_POSITIVE_RESPONSE             = 0x00U,
    UDS_NRC_GENERAL_REJECT                = 0x10U,
    UDS_NRC_SERVICE_NOT_SUPPORTED         = 0x11U,
    UDS_NRC_SUBFUNCTION_NOT_SUPPORTED     = 0x12U,
    UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT   = 0x13U,
    UDS_NRC_RESPONSE_TOO_LONG             = 0x14U,
    UDS_NRC_BUSY_REPEAT_REQUEST           = 0x21U,
    UDS_NRC_CONDITIONS_NOT_CORRECT        = 0x22U,
    UDS_NRC_REQUEST_SEQUENCE_ERROR        = 0x24U,
    UDS_NRC_NO_RESPONSE_FROM_SUBNET       = 0x25U,
    UDS_NRC_FAILURE_PREVENTS_EXEC         = 0x26U,
    UDS_NRC_REQUEST_OUT_OF_RANGE          = 0x31U,
    UDS_NRC_SECURITY_ACCESS_DENIED        = 0x33U,
    UDS_NRC_INVALID_KEY                   = 0x35U,
    UDS_NRC_EXCEEDED_NUMBER_OF_ATTEMPTS   = 0x36U,
    UDS_NRC_REQUIRED_TIME_DELAY_NOT_EXP   = 0x37U,
    UDS_NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED  = 0x70U,
    UDS_NRC_TRANSFER_DATA_SUSPENDED       = 0x71U,
    UDS_NRC_GENERAL_PROGRAMMING_FAILURE   = 0x72U,
    UDS_NRC_WRONG_BLOCK_SEQUENCE_COUNTER  = 0x73U,
    UDS_NRC_REQUEST_CORRECTLY_RCVD_RESP_PENDING = 0x78U,
    UDS_NRC_SUBFUNCTION_NOT_SUPPORTED_IN_SESSION = 0x7EU,
    UDS_NRC_SERVICE_NOT_SUPPORTED_IN_SESSION     = 0x7FU,
} UdsNrc;

/* =========================================================================
 * UDS Session types (0x10 sub-function)
 * ========================================================================= */
typedef enum UdsSession
{
    UDS_SESSION_DEFAULT       = 0x01U,
    UDS_SESSION_PROGRAMMING   = 0x02U,
    UDS_SESSION_EXTENDED      = 0x03U,
} UdsSession;

/* =========================================================================
 * ECU Reset types (0x11 sub-function)
 * ========================================================================= */
typedef enum UdsResetType
{
    UDS_RESET_HARD            = 0x01U,
    UDS_RESET_KEY_OFF_ON      = 0x02U,
    UDS_RESET_SOFT            = 0x03U,
} UdsResetType;

/* =========================================================================
 * Security Access sub-functions (0x27)
 * ========================================================================= */
#define UDS_SA_REQUEST_SEED_LEVEL1   0x01U
#define UDS_SA_SEND_KEY_LEVEL1       0x02U
#define UDS_SA_REQUEST_SEED_LEVEL2   0x03U
#define UDS_SA_SEND_KEY_LEVEL2       0x04U

/* =========================================================================
 * DTC status bit-mask (each DTC has an 8-bit status byte)
 *
 * Demonstrates: bit-fields within a typedef'd struct used as a bitmask.
 * ========================================================================= */
typedef union DtcStatusMask
{
    uint8_t raw;
    struct
    {
        uint8_t testFailed              : 1;   /**< Bit 0: current trip fail. */
        uint8_t testFailedThisOperation : 1;   /**< Bit 1.                    */
        uint8_t pendingDtc              : 1;   /**< Bit 2.                    */
        uint8_t confirmedDtc            : 1;   /**< Bit 3: stored/confirmed.  */
        uint8_t testNotCompleted        : 1;   /**< Bit 4.                    */
        uint8_t testFailedSinceCleared  : 1;   /**< Bit 5.                    */
        uint8_t testNotCompletedSinceClear : 1;/**< Bit 6.                   */
        uint8_t warningIndicator        : 1;   /**< Bit 7: MIL/warning lamp.  */
    } bits;
} DtcStatusMask;

/* =========================================================================
 * DTC record
 * ========================================================================= */
typedef struct DtcRecord
{
    uint32_t      dtcCode;    /**< 3-byte DTC code (e.g. 0x002345).        */
    DtcStatusMask status;     /**< 1-byte status mask.                      */
    uint8_t       severity;   /**< Severity (0=no info, 0x20=check, etc.)  */
    char          label[32];  /**< Human-readable label (for simulator UI). */
} DtcRecord;

/* =========================================================================
 * UDS Data Identifier (DID) record for ReadDataByIdentifier (0x22)
 * ========================================================================= */
typedef struct DidRecord
{
    uint16_t  did;                /**< 2-byte Data Identifier.             */
    uint8_t   data[64];           /**< DID value bytes.                    */
    uint8_t   dataLen;            /**< Actual length of data field.        */
    char      label[32];          /**< Human-readable name.                */
} DidRecord;

/* =========================================================================
 * UDS PDU (raw request or response buffer)
 * ========================================================================= */
typedef struct UdsPdu
{
    uint8_t  data[ECUS_UDS_MAX_PDU_LEN + 1U]; /**< Raw PDU bytes.         */
    size_t   length;                            /**< Used bytes.            */
} UdsPdu;

/* =========================================================================
 * Macro helpers
 * ========================================================================= */

/** Build the positive response SID for a given request SID. */
#define UDS_POSITIVE_RESPONSE_SID(reqSid)  \
    ((uint8_t)((uint8_t)(reqSid) | (uint8_t)UDS_SID_POSITIVE_RESPONSE_OFFSET))

/** True if byte is a valid UDS SID (not a reserved range). */
#define UDS_IS_VALID_SID(sid)   (((sid) >= 0x10U) && ((sid) <= 0xBEU))

#endif /* UDS_TYPES_H */
