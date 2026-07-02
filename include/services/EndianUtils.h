/**
 * @file    EndianUtils.h
 * @brief   Compile-time endianness detection and runtime byte-swap utilities.
 *
 * Automotive ECU firmware must constantly convert between host byte order
 * and network/CAN byte order (big-endian).  This module provides:
 *
 *  - Compile-time host endianness detection macro (ECUS_HOST_BIG_ENDIAN).
 *  - Inline byte-swap functions for 16, 32, and 64-bit values.
 *  - Host-to-BE / BE-to-host conversion macros (no-ops on big-endian hosts).
 *  - Safe multi-byte pack/unpack functions for unaligned memory access
 *    (critical for parsing CAN frame payloads where data is not aligned).
 *
 * Demonstrates: union trick for endianness detection, bit manipulation,
 *               inline functions, conditional compilation, restrict pointers.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#ifndef ENDIAN_UTILS_H
#define ENDIAN_UTILS_H

#include "hal/Platform.h"

/* =========================================================================
 * Compile-time endianness detection
 * ========================================================================= */

/*
 * GCC/Clang provide __BYTE_ORDER__ and __ORDER_BIG_ENDIAN__ predefined macros.
 * Fall back to a runtime check if not available.
 */
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__)
#  if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#    define ECUS_HOST_BIG_ENDIAN   1
#  else
#    define ECUS_HOST_BIG_ENDIAN   0
#  endif
#else
/* Runtime detection (computed once, used as compile-time constant). */
#  define ECUS_HOST_BIG_ENDIAN   0   /* assume little-endian (x86/ARM LE) */
#endif

/* =========================================================================
 * Byte-swap intrinsics (inline, no overhead on modern GCC)
 * ========================================================================= */

/**
 * @brief  Swap byte order of a 16-bit value.
 * @param  val  Input value.
 * @return Byte-swapped value.
 */
ECUS_INLINE uint16_t EndianUtils_Swap16(uint16_t val)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap16(val);
#else
    return (uint16_t)(((val & 0x00FFU) << 8U) |
                      ((val & 0xFF00U) >> 8U));
#endif
}

/**
 * @brief  Swap byte order of a 32-bit value.
 * @param  val  Input value.
 * @return Byte-swapped value.
 */
ECUS_INLINE uint32_t EndianUtils_Swap32(uint32_t val)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(val);
#else
    return ((val & 0x000000FFU) << 24U) |
           ((val & 0x0000FF00U) <<  8U) |
           ((val & 0x00FF0000U) >>  8U) |
           ((val & 0xFF000000U) >> 24U);
#endif
}

/**
 * @brief  Swap byte order of a 64-bit value.
 * @param  val  Input value.
 * @return Byte-swapped value.
 */
ECUS_INLINE uint64_t EndianUtils_Swap64(uint64_t val)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(val);
#else
    return ((val & UINT64_C(0x00000000000000FF)) << 56U) |
           ((val & UINT64_C(0x000000000000FF00)) << 40U) |
           ((val & UINT64_C(0x0000000000FF0000)) << 24U) |
           ((val & UINT64_C(0x00000000FF000000)) <<  8U) |
           ((val & UINT64_C(0x000000FF00000000)) >>  8U) |
           ((val & UINT64_C(0x0000FF0000000000)) >> 24U) |
           ((val & UINT64_C(0x00FF000000000000)) >> 40U) |
           ((val & UINT64_C(0xFF00000000000000)) >> 56U);
#endif
}

/* =========================================================================
 * Host ↔ Big-Endian (network byte order) conversion macros
 * On a big-endian host these are no-ops (zero cost).
 * ========================================================================= */
#if ECUS_HOST_BIG_ENDIAN
#  define ECUS_HTONS(x)   (x)
#  define ECUS_HTONL(x)   (x)
#  define ECUS_NTOHS(x)   (x)
#  define ECUS_NTOHL(x)   (x)
#else
#  define ECUS_HTONS(x)   EndianUtils_Swap16((uint16_t)(x))
#  define ECUS_HTONL(x)   EndianUtils_Swap32((uint32_t)(x))
#  define ECUS_NTOHS(x)   EndianUtils_Swap16((uint16_t)(x))
#  define ECUS_NTOHL(x)   EndianUtils_Swap32((uint32_t)(x))
#endif

/* =========================================================================
 * Unaligned memory pack / unpack (big-endian wire format)
 *
 * CAN frame payloads are byte arrays; reading a uint16 or uint32 directly
 * from an unaligned address is undefined behaviour.  These helpers perform
 * safe byte-by-byte assembly.
 * ========================================================================= */

/**
 * @brief  Read a big-endian uint16 from an unaligned byte buffer.
 * @param  buf  Pointer to at least 2 bytes.
 * @return Host-byte-order uint16.
 */
ECUS_INLINE uint16_t EndianUtils_ReadBE16(const uint8_t * restrict buf)
{
    return (uint16_t)(((uint16_t)buf[0] << 8U) |
                       (uint16_t)buf[1]);
}

/**
 * @brief  Read a big-endian uint32 from an unaligned byte buffer.
 * @param  buf  Pointer to at least 4 bytes.
 * @return Host-byte-order uint32.
 */
ECUS_INLINE uint32_t EndianUtils_ReadBE32(const uint8_t * restrict buf)
{
    return ((uint32_t)buf[0] << 24U) |
           ((uint32_t)buf[1] << 16U) |
           ((uint32_t)buf[2] <<  8U) |
            (uint32_t)buf[3];
}

/**
 * @brief  Write a host-byte-order uint16 as big-endian into a byte buffer.
 * @param  buf  Pointer to at least 2 bytes.
 * @param  val  Value to write.
 */
ECUS_INLINE void EndianUtils_WriteBE16(uint8_t * restrict buf, uint16_t val)
{
    buf[0] = (uint8_t)((val >> 8U) & 0xFFU);
    buf[1] = (uint8_t)( val        & 0xFFU);
}

/**
 * @brief  Write a host-byte-order uint32 as big-endian into a byte buffer.
 * @param  buf  Pointer to at least 4 bytes.
 * @param  val  Value to write.
 */
ECUS_INLINE void EndianUtils_WriteBE32(uint8_t * restrict buf, uint32_t val)
{
    buf[0] = (uint8_t)((val >> 24U) & 0xFFU);
    buf[1] = (uint8_t)((val >> 16U) & 0xFFU);
    buf[2] = (uint8_t)((val >>  8U) & 0xFFU);
    buf[3] = (uint8_t)( val         & 0xFFU);
}

/* =========================================================================
 * Runtime endianness detection (union trick — for diagnostics / testing)
 * ========================================================================= */

/**
 * @brief  Detect host byte order at runtime using the union trick.
 * @return true if the host is big-endian, false if little-endian.
 *
 * Demonstrates: union, anonymous members, type-punning (implementation-
 * defined but universally supported in embedded toolchains).
 */
ECUS_INLINE bool EndianUtils_IsHostBigEndian(void)
{
    /* MISRA deviate 19.2: union type-punning is intentional here for
     * endianness detection — a well-known and portable idiom.           */
    MISRA_DEVIATE(19.2, "Union used intentionally for byte-order detection")

    union { uint16_t word; uint8_t bytes[2]; } probe;
    probe.word = 0x0102U;
    return (probe.bytes[0] == 0x01U);
}

#endif /* ENDIAN_UTILS_H */
