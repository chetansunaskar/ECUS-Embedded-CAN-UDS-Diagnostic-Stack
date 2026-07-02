/**
 * @file    CrcEngine.c
 * @brief   Table-driven CRC engine implementation.
 *
 * C concepts demonstrated:
 *   - Lookup tables (static arrays of uint8/16/32)
 *   - Recursion for table initialisation (BuildCrc32Table helper)
 *   - Bit manipulation (shifts, XOR, masking)
 *   - Reflect/reverse bit-order helper (pointer arithmetic on uint32)
 *   - Pure functions (no global side-effects after init)
 *   - Streaming context pattern (struct + state machine)
 *   - const restrict pointers for optimiser hints
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * 
 * @version 1.0.0
 */

#include "services/CrcEngine.h"
#include "services/Logger.h"

#include <string.h>

/* =========================================================================
 * Module state
 * ========================================================================= */
static bool     s_initialised = false;

/* Lookup tables — 256 entries each, computed at runtime during Init(). */
static uint8_t  s_crc8Table[256U];
static uint16_t s_crc16Table[256U];
static uint32_t s_crc32Table[256U];

/* =========================================================================
 * CRC parameters (polynomial definitions)
 * ========================================================================= */
#define CRC8_POLY    0x1DU          /* SAE J1850                          */
#define CRC8_INIT    0xFFU
#define CRC8_XOROUT  0xFFU

#define CRC16_POLY   0x1021U        /* CCITT                              */
#define CRC16_INIT   0xFFFFU
#define CRC16_XOROUT 0x0000U

#define CRC32_POLY   0x04C11DB7UL  /* ISO 3309 / Ethernet                */
#define CRC32_INIT   0xFFFFFFFFUL
#define CRC32_XOROUT 0xFFFFFFFFUL

/* =========================================================================
 * Private: Reflect (reverse) bits in a 32-bit word
 *
 * Demonstrates: recursion (tail-recursion-friendly), bit manipulation.
 * Used by CRC-32 which uses reflected input/output per spec.
 * ========================================================================= */
static uint32_t ReflectBits(uint32_t data, uint8_t nBits)
{
    /* Base case: no bits left to reflect. */
    if (nBits == 0U)
    {
        return 0U;
    }
    /* Recursive case: reflect remaining bits, then place LSB at MSB position. */
    return (ReflectBits(data >> 1U, (uint8_t)(nBits - 1U))
            | ((data & 0x01U) << (nBits - 1U)));
}

/* =========================================================================
 * Private: Table builders
 * ========================================================================= */

/** Build the 256-entry CRC-8 table (SAE J1850, non-reflected). */
static void BuildCrc8Table(void)
{
    for (uint32_t i = 0U; i < 256U; i++)
    {
        uint8_t crc = (uint8_t)i;

        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x80U) != 0U)
            {
                crc = (uint8_t)((crc << 1U) ^ (uint8_t)CRC8_POLY);
            }
            else
            {
                crc = (uint8_t)(crc << 1U);
            }
        }
        s_crc8Table[i] = crc;
    }
}

/** Build the 256-entry CRC-16 table (CCITT, non-reflected). */
static void BuildCrc16Table(void)
{
    for (uint32_t i = 0U; i < 256U; i++)
    {
        uint16_t crc = (uint16_t)(i << 8U);

        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((uint32_t)(crc << 1U) ^ CRC16_POLY);
            }
            else
            {
                crc = (uint16_t)(crc << 1U);
            }
        }
        s_crc16Table[i] = crc;
    }
}

/** Build the 256-entry CRC-32 table (reflected, as per Ethernet / ISO-3309). */
static void BuildCrc32Table(void)
{
    for (uint32_t i = 0U; i < 256U; i++)
    {
        /* Reflect the byte index into the MSB position. */
        uint32_t crc = ReflectBits(i, 8U) << 24U;

        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x80000000UL) != 0U)
            {
                crc = (crc << 1U) ^ CRC32_POLY;
            }
            else
            {
                crc <<= 1U;
            }
        }
        s_crc32Table[i] = ReflectBits(crc, 32U);
    }
}

/* =========================================================================
 * Public API — Init
 * ========================================================================= */
EcusStatus CrcEngine_Init(void)
{
    if (s_initialised)
    {
        return ECUS_ERR_ALREADY_INIT;
    }

    BuildCrc8Table();
    BuildCrc16Table();
    BuildCrc32Table();

    s_initialised = true;
    LOG_DEBUG("CrcEngine initialised (CRC-8/CRC-16/CRC-32 tables built)");
    return ECUS_OK;
}

/* =========================================================================
 * Public API — One-shot helpers
 * ========================================================================= */

uint8_t CrcEngine_Crc8(const uint8_t * restrict data, size_t len)
{
    uint8_t crc = CRC8_INIT;

    ECUS_ASSERT(s_initialised);

    if (data == NULL)
    {
        return 0U;
    }

    /*
     * Table lookup: XOR current CRC with incoming byte, use result as
     * index into precomputed table.  Classic O(n) CRC computation.
     *
     * Pointer arithmetic: data pointer advances through buffer.
     */
    const uint8_t *ptr = data;
    const uint8_t *end = data + len;

    while (ptr < end)
    {
        crc = s_crc8Table[crc ^ (*ptr)];
        ptr++;   /* pointer arithmetic */
    }

    return crc ^ (uint8_t)CRC8_XOROUT;
}

/* -------------------------------------------------------------------------- */

uint16_t CrcEngine_Crc16(const uint8_t * restrict data, size_t len)
{
    uint16_t crc = CRC16_INIT;

    ECUS_ASSERT(s_initialised);

    if (data == NULL)
    {
        return 0U;
    }

    const uint8_t *ptr = data;
    const uint8_t *end = data + len;

    while (ptr < end)
    {
        uint8_t index = (uint8_t)((crc >> 8U) ^ (*ptr));
        crc = (uint16_t)((crc << 8U) ^ s_crc16Table[index]);
        ptr++;
    }

    return crc ^ (uint16_t)CRC16_XOROUT;
}

/* -------------------------------------------------------------------------- */

uint32_t CrcEngine_Crc32(const uint8_t * restrict data, size_t len)
{
    uint32_t crc = CRC32_INIT;

    ECUS_ASSERT(s_initialised);

    if (data == NULL)
    {
        return 0U;
    }

    const uint8_t *ptr = data;
    const uint8_t *end = data + len;

    while (ptr < end)
    {
        uint8_t index = (uint8_t)((crc ^ (*ptr)) & 0xFFU);
        crc = (crc >> 8U) ^ s_crc32Table[index];
        ptr++;
    }

    return crc ^ CRC32_XOROUT;
}

/* =========================================================================
 * Public API — Streaming interface
 * ========================================================================= */

EcusStatus CrcEngine_StreamInit(CrcContext *ctx, CrcAlgorithm alg)
{
    ECUS_CHECK(ctx != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(alg < CRC_ALG_COUNT, ECUS_ERR_INVALID_PARAM,
               return ECUS_ERR_INVALID_PARAM);
    ECUS_ASSERT(s_initialised);

    ctx->algorithm  = alg;
    ctx->finalised  = false;

    /* Seed the accumulator with the algorithm's init value. */
    switch (alg)
    {
        case CRC_ALG_CRC8_SAE_J1850:
            ctx->accumulator = (uint32_t)CRC8_INIT;
            break;
        case CRC_ALG_CRC16_CCITT:
            ctx->accumulator = (uint32_t)CRC16_INIT;
            break;
        case CRC_ALG_CRC32_ISO:
            ctx->accumulator = CRC32_INIT;
            break;
        default:
            return ECUS_ERR_INVALID_PARAM;
    }

    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

EcusStatus CrcEngine_StreamUpdate(CrcContext          *ctx,
                                   const uint8_t * restrict data,
                                   size_t               len)
{
    ECUS_CHECK(ctx  != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(data != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(!ctx->finalised, ECUS_ERR_NOT_SUPPORTED,
               return ECUS_ERR_NOT_SUPPORTED);

    const uint8_t *ptr = data;
    const uint8_t *end = data + len;

    switch (ctx->algorithm)
    {
        case CRC_ALG_CRC8_SAE_J1850:
            while (ptr < end)
            {
                ctx->accumulator =
                    s_crc8Table[(uint8_t)ctx->accumulator ^ (*ptr)];
                ptr++;
            }
            break;

        case CRC_ALG_CRC16_CCITT:
            while (ptr < end)
            {
                uint8_t idx = (uint8_t)(((ctx->accumulator >> 8U) ^ (*ptr)) & 0xFFU);
                ctx->accumulator =
                    (uint32_t)(((ctx->accumulator << 8U) ^ s_crc16Table[idx]) & 0xFFFFU);
                ptr++;
            }
            break;

        case CRC_ALG_CRC32_ISO:
            while (ptr < end)
            {
                uint8_t idx = (uint8_t)((ctx->accumulator ^ (*ptr)) & 0xFFU);
                ctx->accumulator = (ctx->accumulator >> 8U) ^ s_crc32Table[idx];
                ptr++;
            }
            break;

        default:
            return ECUS_ERR_INVALID_PARAM;
    }

    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

EcusStatus CrcEngine_StreamFinalise(CrcContext *ctx, uint32_t *result)
{
    ECUS_CHECK(ctx    != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(result != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(!ctx->finalised, ECUS_ERR_NOT_SUPPORTED,
               return ECUS_ERR_NOT_SUPPORTED);

    switch (ctx->algorithm)
    {
        case CRC_ALG_CRC8_SAE_J1850:
            *result = (ctx->accumulator ^ (uint32_t)CRC8_XOROUT) & 0xFFU;
            break;
        case CRC_ALG_CRC16_CCITT:
            *result = (ctx->accumulator ^ (uint32_t)CRC16_XOROUT) & 0xFFFFU;
            break;
        case CRC_ALG_CRC32_ISO:
            *result = ctx->accumulator ^ CRC32_XOROUT;
            break;
        default:
            return ECUS_ERR_INVALID_PARAM;
    }

    ctx->finalised = true;
    return ECUS_OK;
}
