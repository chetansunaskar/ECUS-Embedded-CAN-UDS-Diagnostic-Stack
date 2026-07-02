/**
 * @file    ErrorHandler.h
 * @brief   Project-wide error codes, assertion macros, and fault handler API.
 *
 * Design decisions:
 *  - Errors are represented as a signed int32 typedef (EcusStatus).
 *    Positive values mean success or informational codes; negative values
 *    mean failure.  This mirrors POSIX errno conventions while remaining
 *    self-contained.
 *  - ECUS_ASSERT() is a defensive-programming macro that calls the
 *    registered fault handler instead of calling abort(), allowing the
 *    application layer to log the fault before terminating.
 *  - A last-error slot per thread would require TLS; for simplicity we use
 *    an atomic global (sufficient for this simulator).
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

#include "hal/Platform.h"

/* =========================================================================
 * Error code enumeration
 * ========================================================================= */

/**
 * @brief  ECUS status / error codes.
 *
 * Naming convention: ECUS_OK = 0, ECUS_ERR_* = negative.
 * Modules may define additional module-specific codes in their own headers
 * using ranges reserved below.
 *
 * Range allocation:
 *   0            : Success
 *  -1  …  -99   : Generic errors (this file)
 * -100 … -199   : Transport / ISO-TP errors
 * -200 … -299   : UDS layer errors
 * -300 … -399   : HAL errors
 * -400 … -499   : Service errors (logger, memory pool …)
 */
typedef enum EcusStatus
{
    /* ---- Generic -------------------------------------------------------- */
    ECUS_OK                    =   0,   /**< Operation succeeded.           */
    ECUS_ERR_GENERIC           =  -1,   /**< Unspecified error.             */
    ECUS_ERR_NULL_PTR          =  -2,   /**< Unexpected NULL pointer.       */
    ECUS_ERR_INVALID_PARAM     =  -3,   /**< Parameter out of valid range.  */
    ECUS_ERR_TIMEOUT           =  -4,   /**< Operation timed out.           */
    ECUS_ERR_OVERFLOW          =  -5,   /**< Buffer / counter overflow.     */
    ECUS_ERR_UNDERFLOW         =  -6,   /**< Buffer underflow / empty.      */
    ECUS_ERR_NOT_READY         =  -7,   /**< Resource not yet initialised.  */
    ECUS_ERR_ALREADY_INIT      =  -8,   /**< Module already initialised.    */
    ECUS_ERR_NOT_SUPPORTED     =  -9,   /**< Feature not supported.         */
    ECUS_ERR_NO_MEMORY         = -10,   /**< Memory allocation failed.      */
    ECUS_ERR_BUSY              = -11,   /**< Resource busy / locked.        */
    ECUS_ERR_CRC               = -12,   /**< CRC mismatch.                  */
    ECUS_ERR_IO                = -13,   /**< I/O error.                     */

    /* ---- Transport / ISO-TP (−100 … −199) ------------------------------- */
    ECUS_ERR_ISOTP_OVERFLOW    = -100,  /**< ISO-TP buffer overflow.        */
    ECUS_ERR_ISOTP_INVALID_SN  = -101,  /**< Wrong consecutive frame SN.    */
    ECUS_ERR_ISOTP_TIMEOUT_CR  = -102,  /**< Cr timeout (no CF received).   */
    ECUS_ERR_ISOTP_TIMEOUT_AS  = -103,  /**< As timeout (Tx stall).         */
    ECUS_ERR_ISOTP_WRONG_SN    = -104,  /**< Sequence number mismatch.      */
    ECUS_ERR_ISOTP_UNEXP_PDU   = -105,  /**< Unexpected PDU type.           */

    /* ---- UDS layer (−200 … −299) ---------------------------------------- */
    ECUS_ERR_UDS_SERVICE_NA    = -200,  /**< Service not available in session. */
    ECUS_ERR_UDS_SECURITY      = -201,  /**< Security access denied.        */
    ECUS_ERR_UDS_BUSY_REPEAT   = -202,  /**< Request correctly received, response pending. */
    ECUS_ERR_UDS_COND_NOT_MET  = -203,  /**< Conditions not correct.        */
    ECUS_ERR_UDS_SEQ_ERROR     = -204,  /**< Request sequence error.        */
    ECUS_ERR_UDS_OUT_OF_RANGE  = -205,  /**< Request out of range.          */
    ECUS_ERR_UDS_WRONG_LEN     = -206,  /**< Incorrect message length.      */

    /* ---- HAL (−300 … −399) ---------------------------------------------- */
    ECUS_ERR_HAL_INIT          = -300,  /**< HAL initialisation failed.     */
    ECUS_ERR_HAL_TX_FULL       = -301,  /**< CAN Tx FIFO full.              */
    ECUS_ERR_HAL_RX_EMPTY      = -302,  /**< CAN Rx FIFO empty.             */

    /* ---- Services (−400 … −499) ----------------------------------------- */
    ECUS_ERR_LOG_INIT          = -400,  /**< Logger init failed.            */
    ECUS_ERR_POOL_EXHAUSTED    = -401,  /**< Memory pool exhausted.         */
    ECUS_ERR_NVM_READ          = -402,  /**< NVM read error.                */
    ECUS_ERR_NVM_WRITE         = -403   /**< NVM write error.               */
} EcusStatus;

/* =========================================================================
 * Fault severity levels
 * ========================================================================= */
typedef enum FaultSeverity
{
    FAULT_SEV_WARNING  = 0U,  /**< Non-fatal; execution continues.         */
    FAULT_SEV_ERROR    = 1U,  /**< Recoverable error; module resets.       */
    FAULT_SEV_FATAL    = 2U   /**< Unrecoverable; terminate after logging. */
} FaultSeverity;

/* =========================================================================
 * Fault info structure (passed to the fault handler callback)
 * ========================================================================= */
typedef struct FaultInfo
{
    FaultSeverity  severity;          /**< Severity level.                  */
    EcusStatus     code;              /**< Associated error code.           */
    const char    *file;             /**< Source file name (__FILE__).      */
    uint32_t       line;             /**< Source line number (__LINE__).    */
    const char    *expression;       /**< Stringified expression.          */
    const char    *message;          /**< Optional human-readable message. */
} FaultInfo;

/* =========================================================================
 * Fault handler callback type
 * ========================================================================= */
/**
 * @brief  Callback invoked when a fault is detected.
 * @param  info  Pointer to fault details (never NULL).
 */
typedef void (*FaultHandlerFn)(const FaultInfo *info);

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief  Initialise the error-handler module.
 *         Must be called once before any other module.
 * @return ECUS_OK on success.
 */
EcusStatus ErrorHandler_Init(void);

/**
 * @brief  Register a custom fault handler callback.
 *         The default handler logs to stderr and calls exit(EXIT_FAILURE)
 *         for FATAL faults.
 * @param  handler  Callback function pointer (NULL restores default).
 */
void ErrorHandler_RegisterHandler(FaultHandlerFn handler);

/**
 * @brief  Raise a fault explicitly (called by macros below).
 * @param  severity    Fault severity.
 * @param  code        Error code.
 * @param  file        __FILE__ of the call site.
 * @param  line        __LINE__ of the call site.
 * @param  expression  Stringified expression (may be NULL).
 * @param  message     Descriptive message (may be NULL).
 */
void ErrorHandler_RaiseFault(FaultSeverity  severity,
                              EcusStatus     code,
                              const char    *file,
                              uint32_t       line,
                              const char    *expression,
                              const char    *message);

/**
 * @brief  Return a human-readable string for an EcusStatus code.
 * @param  status  The status code to describe.
 * @return Pointer to a static string (never NULL).
 */
ECUS_ATTR_PURE
const char *ErrorHandler_StatusStr(EcusStatus status);

/**
 * @brief  Store the most recent error code (thread-unsafe, for demo use).
 * @param  status  Error code to record.
 */
void ErrorHandler_SetLastError(EcusStatus status);

/**
 * @brief  Retrieve the most recently stored error code.
 * @return Last recorded EcusStatus.
 */
EcusStatus ErrorHandler_GetLastError(void);

/* =========================================================================
 * Assertion and error-checking macros
 * ========================================================================= */

/**
 * @brief  Fatal assertion.  Raises a FATAL fault if @p expr is false.
 *         Never disabled in release builds (use for invariants that MUST hold).
 */
#define ECUS_ASSERT(expr)                                               \
    do {                                                                 \
        if (ECUS_UNLIKELY(!(expr))) {                                   \
            ErrorHandler_RaiseFault(FAULT_SEV_FATAL,                    \
                                    ECUS_ERR_GENERIC,                   \
                                    __FILE__,                            \
                                    (uint32_t)__LINE__,                  \
                                    #expr,                               \
                                    "Assertion failed");                 \
        }                                                                \
    } while (0)

/**
 * @brief  Check a condition; log a WARNING and execute @p action on failure.
 *         Suitable for parameter validation in public APIs.
 *
 * Example:
 *   ECUS_CHECK(buf != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
 */
#define ECUS_CHECK(expr, code, action)                                  \
    do {                                                                 \
        if (ECUS_UNLIKELY(!(expr))) {                                   \
            ErrorHandler_RaiseFault(FAULT_SEV_WARNING,                  \
                                    (code),                              \
                                    __FILE__,                            \
                                    (uint32_t)__LINE__,                  \
                                    #expr,                               \
                                    NULL);                               \
            action;                                                      \
        }                                                                \
    } while (0)

/**
 * @brief  Propagate an error: if @p expr evaluates to a non-OK status,
 *         store it and return it immediately.
 *
 * Example:
 *   ECUS_PROPAGATE(SomeModule_Init());
 */
#define ECUS_PROPAGATE(expr)                                            \
    do {                                                                 \
        EcusStatus _ecus_rc = (expr);                                   \
        if (ECUS_UNLIKELY(_ecus_rc != ECUS_OK)) {                       \
            ErrorHandler_SetLastError(_ecus_rc);                        \
            return _ecus_rc;                                             \
        }                                                                \
    } while (0)

#endif /* ERROR_HANDLER_H */
