/**
 * @file    Platform.h
 * @brief   Master platform abstraction header.
 *
 * Provides fixed-width integer types, compiler-portability macros,
 * MISRA-C helper annotations, boolean type, and project-wide
 * compile-time constants.  Every translation unit in ECUS includes
 * this header (directly or transitively).
 *
 * @note    Target: hosted POSIX environment (Linux / macOS).
 *          Compiler: GCC >= 9 or Clang >= 10, C11 mode.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#ifndef PLATFORM_H
#define PLATFORM_H

/* =========================================================================
 * Standard C11 headers — included exactly once here so other modules
 * need not repeat them.
 * ========================================================================= */
#include <stdint.h>    /* uint8_t … uint64_t, int8_t … int64_t          */
#include <stddef.h>    /* size_t, ptrdiff_t, NULL                         */
#include <stdbool.h>   /* bool, true, false                               */
#include <stdarg.h>    /* va_list, va_start, va_end  (variadic functions) */
#include <limits.h>    /* CHAR_BIT, INT_MAX …                             */
#include <assert.h>    /* static_assert, assert()                         */

/* =========================================================================
 * C11 static_assert wrapper — emits a compile-time diagnostic.
 * Usage: STATIC_ASSERT(sizeof(uint32_t) == 4U, "uint32 must be 4 bytes");
 * ========================================================================= */
#define STATIC_ASSERT(cond, msg)  static_assert((cond), msg)

/* =========================================================================
 * Compiler attribute portability macros
 * These degrade gracefully on unknown compilers (no-op).
 * ========================================================================= */
#if defined(__GNUC__) || defined(__clang__)
#  define ECUS_ATTR_NORETURN     __attribute__((noreturn))
#  define ECUS_ATTR_UNUSED       __attribute__((unused))
#  define ECUS_ATTR_PACKED       __attribute__((packed))
#  define ECUS_ATTR_ALIGNED(n)   __attribute__((aligned(n)))
#  define ECUS_ATTR_PURE         __attribute__((pure))
#  define ECUS_ATTR_CONST        __attribute__((const))
#  define ECUS_ATTR_WARN_UNUSED  __attribute__((warn_unused_result))
#  define ECUS_LIKELY(x)         __builtin_expect(!!(x), 1)
#  define ECUS_UNLIKELY(x)       __builtin_expect(!!(x), 0)
#  define ECUS_INLINE            static inline __attribute__((always_inline))
#else
#  define ECUS_ATTR_NORETURN
#  define ECUS_ATTR_UNUSED
#  define ECUS_ATTR_PACKED
#  define ECUS_ATTR_ALIGNED(n)
#  define ECUS_ATTR_PURE
#  define ECUS_ATTR_CONST
#  define ECUS_ATTR_WARN_UNUSED
#  define ECUS_LIKELY(x)         (x)
#  define ECUS_UNLIKELY(x)       (x)
#  define ECUS_INLINE            static inline
#endif

/* =========================================================================
 * MISRA-C:2012 annotation helpers (documentation only — no runtime cost).
 * MISRA_DEVIATE tags mark intentional rule deviations with a justification.
 * ========================================================================= */
#define MISRA_DEVIATE(rule, justification)   /* rule: justification */
#define MISRA_SUPPRESS(rule)                 /* deliberate suppression */

/* =========================================================================
 * Utility macros
 * ========================================================================= */

/** Number of elements in a statically declared array. */
#define ECUS_ARRAY_SIZE(arr)   (sizeof(arr) / sizeof((arr)[0U]))

/** Clamp x to [lo, hi]. */
#define ECUS_CLAMP(x, lo, hi)  (((x) < (lo)) ? (lo) : (((x) > (hi)) ? (hi) : (x)))

/** Minimum / Maximum (evaluates arguments twice — use with caution). */
#define ECUS_MIN(a, b)   (((a) < (b)) ? (a) : (b))
#define ECUS_MAX(a, b)   (((a) > (b)) ? (a) : (b))

/** Suppress "unused parameter" warning in a MISRA-safe way. */
#define ECUS_UNUSED_PARAM(p)   ((void)(p))

/** Align a value up to the next multiple of 'align' (must be power of 2). */
#define ECUS_ALIGN_UP(val, align) \
    (((uint32_t)(val) + ((uint32_t)(align) - 1U)) & ~((uint32_t)(align) - 1U))

/* =========================================================================
 * Boolean aliases for MISRA-C compliance (prefer bool/true/false)
 * ========================================================================= */
#define ECUS_TRUE    (true)
#define ECUS_FALSE   (false)

/* =========================================================================
 * Project-wide compile-time constants
 * ========================================================================= */

/** Software version (Major.Minor.Patch). */
#define ECUS_VERSION_MAJOR   1U
#define ECUS_VERSION_MINOR   0U
#define ECUS_VERSION_PATCH   0U

/** Maximum length of any identifier string (ECU name, DTC name …). */
#define ECUS_MAX_NAME_LEN    64U

/** Maximum payload size for a single UDS PDU (4095 bytes per ISO 15765-2). */
#define ECUS_UDS_MAX_PDU_LEN  4095U

/** CAN 2.0B extended frame data length. */
#define ECUS_CAN_DLC_MAX     8U

/* =========================================================================
 * Compile-time type-size assertions — catch porting issues early.
 * ========================================================================= */
STATIC_ASSERT(sizeof(uint8_t)  == 1U, "uint8_t  must be 1 byte");
STATIC_ASSERT(sizeof(uint16_t) == 2U, "uint16_t must be 2 bytes");
STATIC_ASSERT(sizeof(uint32_t) == 4U, "uint32_t must be 4 bytes");
STATIC_ASSERT(sizeof(uint64_t) == 8U, "uint64_t must be 8 bytes");
STATIC_ASSERT(CHAR_BIT         == 8U, "char must be 8 bits");

#endif /* PLATFORM_H */
