/**
 * @file    UdsDispatcher.h
 * @brief   UDS service dispatcher — dispatch table pattern.
 *
 * The dispatcher receives a raw UDS PDU, looks up the service handler
 * in a dispatch table indexed by SID, validates session/security
 * prerequisites, and invokes the handler.
 *
 * Dispatch table pattern:
 *   typedef struct { UdsSid sid; UdsServiceHandlerFn handler; ... } ServiceEntry;
 *   static const ServiceEntry k_serviceTable[] = { ... };
 *
 * This is the embedded equivalent of a vtable or jump table.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#ifndef UDS_DISPATCHER_H
#define UDS_DISPATCHER_H

#include "hal/Platform.h"
#include "services/ErrorHandler.h"
#include "uds/UdsTypes.h"

/* Forward declaration — UdsServer.h includes UdsDispatcher.h indirectly. */
typedef struct UdsServer UdsServer;

/* =========================================================================
 * Service handler function pointer type
 *
 * Each UDS service handler has this signature:
 *   server  : the active UDS server context
 *   req     : incoming request PDU
 *   reqLen  : request PDU length
 *   resp    : output response PDU buffer (handler fills this)
 *   respLen : output: number of bytes written to resp
 *
 * Returns ECUS_OK on success, or an error code.
 * ========================================================================= */
typedef EcusStatus (*UdsServiceHandlerFn)(UdsServer     *server,
                                           const uint8_t *req,
                                           size_t         reqLen,
                                           uint8_t       *resp,
                                           size_t        *respLen);

/* =========================================================================
 * Session access flags — bitmask of sessions that permit a service.
 * ========================================================================= */
#define UDS_SESSION_FLAG_DEFAULT      (1U << 0U)
#define UDS_SESSION_FLAG_PROGRAMMING  (1U << 1U)
#define UDS_SESSION_FLAG_EXTENDED     (1U << 2U)
#define UDS_SESSION_FLAG_ALL          (0x07U)

/* =========================================================================
 * Service table entry
 * ========================================================================= */
typedef struct ServiceEntry
{
    UdsSid              sid;              /**< Service ID.                 */
    UdsServiceHandlerFn handler;          /**< Handler function pointer.   */
    uint8_t             sessionMask;      /**< Permitted sessions bitmask. */
    bool                requiresSecurity; /**< Must be security-unlocked.  */
    const char         *name;            /**< Service name for logging.   */
} ServiceEntry;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief  Dispatch a received UDS PDU to the correct service handler.
 *
 *         1. Validates minimum PDU length.
 *         2. Looks up SID in the dispatch table (linear search, table is small).
 *         3. Checks session and security prerequisites.
 *         4. Invokes the handler.
 *         5. Sends the response via ISO-TP.
 *
 * @param  server  Active UDS server context.
 * @param  pdu     Raw PDU bytes (first byte = SID).
 * @param  pduLen  PDU length.
 * @return ECUS_OK on success, error code on protocol failure.
 */
EcusStatus UdsDispatcher_Dispatch(UdsServer     *server,
                                   const uint8_t *pdu,
                                   size_t         pduLen);

/**
 * @brief  Build and send a Negative Response frame.
 * @param  server   UDS server.
 * @param  reqSid   The SID of the request being rejected.
 * @param  nrc      Negative Response Code.
 * @return ECUS_OK on success.
 */
EcusStatus UdsDispatcher_SendNegativeResponse(UdsServer *server,
                                               uint8_t    reqSid,
                                               UdsNrc     nrc);

#endif /* UDS_DISPATCHER_H */
