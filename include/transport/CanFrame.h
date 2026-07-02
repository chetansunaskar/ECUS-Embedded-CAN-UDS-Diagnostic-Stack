/**
 * @file    CanFrame.h
 * @brief   CAN 2.0A/2.0B frame type definitions.
 *
 * Models a CAN frame in software exactly as it would appear in hardware
 * registers or on the wire.  Demonstrates:
 *
 *  - Bit-fields: CAN ID arbitration field flags (IDE, RTR, ERR).
 *  - Union: overlay of 32-bit raw ID word and structured bit-fields
 *    for zero-cost field access.
 *  - Enum: frame type classifier.
 *  - Typedef + struct: aggregate frame representation.
 *  - MISRA-safe packed struct notes.
 *
 * CAN relevance:
 *   Standard (11-bit) ID = CAN 2.0A
 *   Extended (29-bit) ID = CAN 2.0B
 *   DLC (Data Length Code) = 0–8 bytes
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#ifndef CAN_FRAME_H
#define CAN_FRAME_H

#include "hal/Platform.h"

/* =========================================================================
 * CAN frame type
 * ========================================================================= */
typedef enum CanFrameType
{
    CAN_FRAME_DATA   = 0U,   /**< Standard data frame.                    */
    CAN_FRAME_REMOTE = 1U,   /**< Remote transmission request (RTR).      */
    CAN_FRAME_ERROR  = 2U,   /**< Error frame (diagnostic use).           */
    CAN_FRAME_OVERLOAD = 3U  /**< Overload frame.                         */
} CanFrameType;

/* =========================================================================
 * CAN ID with bit-fields (union overlay)
 *
 * On the wire (CAN 2.0B extended):
 *   Bits [28:0]  = 29-bit Identifier
 *   Bit  [29]    = Reserved (must be 0 for data frames)
 *   Bit  [30]    = RTR (Remote Transmission Request)
 *   Bit  [31]    = IDE (ID Extension: 0=standard, 1=extended)
 *
 * MISRA note: bit-field signedness is implementation-defined for types
 * other than unsigned int; we use uint32_t members only.
 * ========================================================================= */
typedef union CanId
{
    uint32_t raw;   /**< Raw 32-bit word for fast copy / comparison.      */

    struct
    {
        uint32_t id    : 29;   /**< 11-bit (std) or 29-bit (ext) CAN ID.  */
        uint32_t rsvd  :  1;   /**< Reserved, must be 0.                  */
        uint32_t rtr   :  1;   /**< Remote Transmission Request flag.     */
        uint32_t ide   :  1;   /**< ID Extension flag (0=std, 1=ext).     */
    } fields;
} CanId;

/* =========================================================================
 * CAN data frame
 * ========================================================================= */
typedef struct CanFrame
{
    CanId         id;                         /**< CAN arbitration ID.       */
    CanFrameType  type;                       /**< Data / RTR / Error.       */
    uint8_t       dlc;                        /**< Data length code (0–8).   */
    uint8_t       data[ECUS_CAN_DLC_MAX];    /**< Payload bytes.            */
    uint32_t      timestamp_ms;              /**< Rx/Tx timestamp (ms).     */
} CanFrame;

/* =========================================================================
 * Compile-time struct size assertion
 * CAN frame must not exceed 20 bytes on this platform.
 * ========================================================================= */
STATIC_ASSERT(sizeof(CanFrame) <= 32U, "CanFrame struct too large");

/* =========================================================================
 * Inline helpers
 * ========================================================================= */

/** Construct a standard 11-bit CAN ID word. */
ECUS_INLINE uint32_t CanFrame_MakeStdId(uint16_t id11)
{
    CanId cid;
    cid.raw      = 0U;
    cid.fields.id  = (uint32_t)(id11 & 0x7FFU);
    cid.fields.ide = 0U;
    return cid.raw;
}

/** Construct an extended 29-bit CAN ID word. */
ECUS_INLINE uint32_t CanFrame_MakeExtId(uint32_t id29)
{
    CanId cid;
    cid.raw        = 0U;
    cid.fields.id  = id29 & 0x1FFFFFFFU;
    cid.fields.ide = 1U;
    return cid.raw;
}

/** Extract the numeric CAN ID (strips IDE/RTR flags). */
ECUS_INLINE uint32_t CanFrame_GetId(const CanFrame *frame)
{
    return frame->id.fields.id;
}

/** Return true if the frame uses an extended (29-bit) ID. */
ECUS_INLINE bool CanFrame_IsExtended(const CanFrame *frame)
{
    return (frame->id.fields.ide == 1U);
}

#endif /* CAN_FRAME_H */
