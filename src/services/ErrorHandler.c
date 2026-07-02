/**
 * @file    ErrorHandler.c
 * @brief   Implementation of the ECUS error and fault handling framework.
 *
 * Design notes:
 *  - The module maintains a single globally-installed fault handler pointer
 *    and a last-error code.  An atomic_int is used for the last-error slot
 *    to demonstrate C11 atomics without requiring full TLS.
 *  - The default fault handler writes to stderr and terminates on FATAL.
 *    Applications register their own handler via ErrorHandler_RegisterHandler().
 *
 * C concepts demonstrated:
 *   function pointers, callback registration, variadic-ready pattern,
 *   C11 stdatomic, storage classes (static), const strings, switch/enum.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * 
 * 
 * @version 1.0.0
 */

#include "services/ErrorHandler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>   /* C11 atomics */

/* =========================================================================
 * Module-private state (storage class: static — translation-unit scope)
 * ========================================================================= */

/** Currently installed fault handler (NULL = default handler active). */
static FaultHandlerFn s_faultHandler = NULL;

/** Last recorded error code, accessed atomically. */
static atomic_int s_lastError = 0;

/** Initialisation guard — demonstrates 'volatile' for state flags. */
static volatile bool s_initialised = false;

/* =========================================================================
 * Private helpers
 * ========================================================================= */

/**
 * @brief  Default fault handler: prints to stderr, exits on FATAL.
 * @param  info  Pointer to fault info (guaranteed non-NULL by caller).
 */
static void DefaultFaultHandler(const FaultInfo *info)
{
    const char *sevStr = "UNKNOWN";

    /* Demonstrates switch on enum — MISRA-C Rule 16.4: all cases covered. */
    switch (info->severity)
    {
        case FAULT_SEV_WARNING:  sevStr = "WARNING";  break;
        case FAULT_SEV_ERROR:    sevStr = "ERROR";    break;
        case FAULT_SEV_FATAL:    sevStr = "FATAL";    break;
        default:                 sevStr = "UNKNOWN";  break;
    }

    (void)fprintf(stderr,
                  "[FAULT][%s] code=%d  %s:%u",
                  sevStr,
                  (int)info->code,
                  (info->file     != NULL) ? info->file     : "<?>",
                  info->line);

    if (info->expression != NULL)
    {
        (void)fprintf(stderr, "  expr=(%s)", info->expression);
    }

    if (info->message != NULL)
    {
        (void)fprintf(stderr, "  msg=\"%s\"", info->message);
    }

    (void)fprintf(stderr, "\n");

    if (info->severity == FAULT_SEV_FATAL)
    {
        (void)fprintf(stderr, "[FATAL] Terminating process.\n");
        /* MISRA deviate 21.8: exit() required for fatal error path. */
        MISRA_DEVIATE(21.8, "Fatal fault — controlled shutdown required")
        exit(EXIT_FAILURE); /* NORETURN path */
    }
}

/* =========================================================================
 * Public API implementation
 * ========================================================================= */

EcusStatus ErrorHandler_Init(void)
{
    if (s_initialised)
    {
        return ECUS_ERR_ALREADY_INIT;
    }

    s_faultHandler = NULL;          /* use default handler              */
    atomic_store(&s_lastError, 0);  /* C11 atomic store                 */
    s_initialised  = true;

    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

void ErrorHandler_RegisterHandler(FaultHandlerFn handler)
{
    /*
     * Accepts NULL to restore the built-in default.
     * No synchronisation needed here — registration is expected during init
     * before any concurrent activity starts.
     */
    s_faultHandler = handler;
}

/* -------------------------------------------------------------------------- */

void ErrorHandler_RaiseFault(FaultSeverity  severity,
                              EcusStatus     code,
                              const char    *file,
                              uint32_t       line,
                              const char    *expression,
                              const char    *message)
{
    FaultInfo info;

    /* Populate fault record (aggregate initialiser with designated fields). */
    info.severity   = severity;
    info.code       = code;
    info.file       = file;
    info.line       = line;
    info.expression = expression;
    info.message    = message;

    /* Update last-error atomically before invoking handler. */
    atomic_store(&s_lastError, (int)code);

    /* Dispatch to registered handler or default. */
    if (s_faultHandler != NULL)
    {
        s_faultHandler(&info);
    }
    else
    {
        DefaultFaultHandler(&info);
    }
}

/* -------------------------------------------------------------------------- */

const char *ErrorHandler_StatusStr(EcusStatus status)
{
    /*
     * Lookup table approach: array of {code, string} pairs searched linearly.
     * Demonstrates structures, arrays of structs, and const pointers.
     */
    typedef struct { EcusStatus code; const char *str; } StatusEntry;

    /* 'restrict' is not applicable to local arrays, but 'const' is.        */
    static const StatusEntry k_table[] =
    {
        { ECUS_OK,                    "OK"                           },
        { ECUS_ERR_GENERIC,           "Generic error"                },
        { ECUS_ERR_NULL_PTR,          "Null pointer"                 },
        { ECUS_ERR_INVALID_PARAM,     "Invalid parameter"            },
        { ECUS_ERR_TIMEOUT,           "Timeout"                      },
        { ECUS_ERR_OVERFLOW,          "Overflow"                     },
        { ECUS_ERR_UNDERFLOW,         "Underflow"                    },
        { ECUS_ERR_NOT_READY,         "Not ready"                    },
        { ECUS_ERR_ALREADY_INIT,      "Already initialised"          },
        { ECUS_ERR_NOT_SUPPORTED,     "Not supported"                },
        { ECUS_ERR_NO_MEMORY,         "No memory"                    },
        { ECUS_ERR_BUSY,              "Busy"                         },
        { ECUS_ERR_CRC,               "CRC error"                    },
        { ECUS_ERR_IO,                "I/O error"                    },
        { ECUS_ERR_ISOTP_OVERFLOW,    "ISO-TP: buffer overflow"      },
        { ECUS_ERR_ISOTP_INVALID_SN,  "ISO-TP: invalid sequence"     },
        { ECUS_ERR_ISOTP_TIMEOUT_CR,  "ISO-TP: Cr timeout"           },
        { ECUS_ERR_ISOTP_TIMEOUT_AS,  "ISO-TP: As timeout"           },
        { ECUS_ERR_ISOTP_WRONG_SN,    "ISO-TP: wrong SN"             },
        { ECUS_ERR_ISOTP_UNEXP_PDU,   "ISO-TP: unexpected PDU"       },
        { ECUS_ERR_UDS_SERVICE_NA,    "UDS: service not available"   },
        { ECUS_ERR_UDS_SECURITY,      "UDS: security denied"         },
        { ECUS_ERR_UDS_BUSY_REPEAT,   "UDS: busy, repeat request"    },
        { ECUS_ERR_UDS_COND_NOT_MET,  "UDS: conditions not correct"  },
        { ECUS_ERR_UDS_SEQ_ERROR,     "UDS: sequence error"          },
        { ECUS_ERR_UDS_OUT_OF_RANGE,  "UDS: out of range"            },
        { ECUS_ERR_UDS_WRONG_LEN,     "UDS: incorrect message length"},
        { ECUS_ERR_HAL_INIT,          "HAL: init failed"             },
        { ECUS_ERR_HAL_TX_FULL,       "HAL: Tx full"                 },
        { ECUS_ERR_HAL_RX_EMPTY,      "HAL: Rx empty"                },
        { ECUS_ERR_LOG_INIT,          "Logger: init failed"          },
        { ECUS_ERR_POOL_EXHAUSTED,    "Memory pool: exhausted"       },
        { ECUS_ERR_NVM_READ,          "NVM: read error"              },
        { ECUS_ERR_NVM_WRITE,         "NVM: write error"             },
    };

    const size_t count = ECUS_ARRAY_SIZE(k_table);

    /* Linear search — table is small; for large tables use binary search. */
    for (size_t i = 0U; i < count; i++)
    {
        if (k_table[i].code == status)
        {
            return k_table[i].str;
        }
    }

    return "Unknown status code";
}

/* -------------------------------------------------------------------------- */

void ErrorHandler_SetLastError(EcusStatus status)
{
    atomic_store(&s_lastError, (int)status);
}

/* -------------------------------------------------------------------------- */

EcusStatus ErrorHandler_GetLastError(void)
{
    return (EcusStatus)atomic_load(&s_lastError);
}
