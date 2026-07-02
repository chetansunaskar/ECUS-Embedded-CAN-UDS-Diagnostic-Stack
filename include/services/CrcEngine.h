/**
 * @file    CrcEngine.h
 * @brief   CRC computation engine: CRC-8/SAE-J1850, CRC-16/CCITT, CRC-32/ISO.
 *
 * Design decisions:
 *  - Table-driven implementation for speed (256-entry lookup tables).
 *  - Tables are computed once at init (demonstrates recursion for table fill),
 *    or optionally compile-time constant via ECUS_CRC_STATIC_TABLES.
 *  - All functions are pure (no side-effects, same input → same output).
 *  - Streaming API (Init / Update / Finalize) supports large payloads
 *    processed in chunks — common in embedded bootloader and UDS scenarios.
 *
 * Automotive relevance:
 *  CRC-8  : SAE J1850 used in OBD-II / CAN frame validation.
 *  CRC-16 : ISO 15765-2 (ISO-TP) header integrity.
 *  CRC-32 : ECU firmware image integrity verification.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#ifndef CRC_ENGINE_H
#define CRC_ENGINE_H

#include "hal/Platform.h"
#include "services/ErrorHandler.h"

/* =========================================================================
 * CRC algorithm identifiers
 * ========================================================================= */
typedef enum CrcAlgorithm
{
    CRC_ALG_CRC8_SAE_J1850  = 0U,  /**< Poly=0x1D, Init=0xFF, RefIn=F, RefOut=F, XorOut=0xFF */
    CRC_ALG_CRC16_CCITT     = 1U,  /**< Poly=0x1021, Init=0xFFFF, RefIn=F, RefOut=F, XorOut=0x0000 */
    CRC_ALG_CRC32_ISO        = 2U,  /**< Poly=0x04C11DB7, Init=0xFFFFFFFF, RefIn=T, RefOut=T, XorOut=0xFFFFFFFF */
    CRC_ALG_COUNT            = 3U
} CrcAlgorithm;

/* =========================================================================
 * Streaming context (allows chunked processing)
 * ========================================================================= */
typedef struct CrcContext
{
    CrcAlgorithm  algorithm;    /**< Which CRC variant is active.          */
    uint32_t      accumulator;  /**< Running CRC value (width-dependent).  */
    bool          finalised;    /**< True after Finalise() is called.      */
} CrcContext;

/* =========================================================================
 * Public API — one-shot helpers
 * ========================================================================= */

/**
 * @brief  Initialise CRC lookup tables.  Must be called once at startup.
 * @return ECUS_OK on success.
 */
EcusStatus CrcEngine_Init(void);

/**
 * @brief  Compute CRC-8 (SAE J1850) over a buffer in one call.
 * @param  data  Pointer to input data.
 * @param  len   Number of bytes.
 * @return 8-bit CRC value.
 */
ECUS_ATTR_PURE
uint8_t CrcEngine_Crc8(const uint8_t *data, size_t len);

/**
 * @brief  Compute CRC-16 (CCITT) over a buffer in one call.
 * @param  data  Pointer to input data.
 * @param  len   Number of bytes.
 * @return 16-bit CRC value.
 */
ECUS_ATTR_PURE
uint16_t CrcEngine_Crc16(const uint8_t *data, size_t len);

/**
 * @brief  Compute CRC-32 (ISO) over a buffer in one call.
 * @param  data  Pointer to input data.
 * @param  len   Number of bytes.
 * @return 32-bit CRC value.
 */
ECUS_ATTR_PURE
uint32_t CrcEngine_Crc32(const uint8_t *data, size_t len);

/* =========================================================================
 * Public API — streaming (chunked) interface
 * ========================================================================= */

/**
 * @brief  Initialise a CRC streaming context.
 * @param  ctx  Pointer to caller-allocated context.
 * @param  alg  CRC algorithm to use.
 * @return ECUS_OK on success.
 */
EcusStatus CrcEngine_StreamInit(CrcContext *ctx, CrcAlgorithm alg);

/**
 * @brief  Feed a data chunk into the streaming CRC.
 * @param  ctx   Active streaming context.
 * @param  data  Pointer to data chunk.
 * @param  len   Chunk length in bytes.
 * @return ECUS_OK on success.
 */
EcusStatus CrcEngine_StreamUpdate(CrcContext    *ctx,
                                   const uint8_t *data,
                                   size_t         len);

/**
 * @brief  Finalise the CRC and return the result.
 * @param  ctx     Active streaming context.
 * @param  result  Output: final CRC value (32-bit wide; cast as needed).
 * @return ECUS_OK on success.
 */
EcusStatus CrcEngine_StreamFinalise(CrcContext *ctx, uint32_t *result);

#endif /* CRC_ENGINE_H */
