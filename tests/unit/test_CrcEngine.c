/**
 * @file    test_CrcEngine.c
 * @brief   Unit tests for CrcEngine (CRC-8/16/32).
 *
 * Test vectors verified against standard CRC reference implementations
 * (crccalc.com / pycrc) for each algorithm variant used.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#include "TestRunner.h"
#include "services/CrcEngine.h"

void Test_CrcEngine_RunAll(void);

/* =========================================================================
 * Test cases
 * ========================================================================= */

TEST_CASE(CrcEngine_Crc8_EmptyBuffer_ReturnsXorOut)
{
    uint8_t crc = CrcEngine_Crc8(NULL, 0U);
    /* NULL input returns 0 per the implementation's defensive check. */
    ASSERT_EQ(crc, 0U);
}

TEST_CASE(CrcEngine_Crc8_KnownVector_Ascii123456789)
{
    const uint8_t data[] = "123456789";
    uint8_t crc = CrcEngine_Crc8(data, 9U);
    /* CRC-8/SAE-J1850 check value for "123456789" is 0x4B (per spec table). */
    ASSERT_EQ(crc, 0x4BU);
}

TEST_CASE(CrcEngine_Crc8_DeterministicAcrossCalls)
{
    const uint8_t data[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    uint8_t crc1 = CrcEngine_Crc8(data, 4U);
    uint8_t crc2 = CrcEngine_Crc8(data, 4U);
    ASSERT_EQ(crc1, crc2);
}

TEST_CASE(CrcEngine_Crc16_KnownVector_Ascii123456789)
{
    const uint8_t data[] = "123456789";
    uint16_t crc = CrcEngine_Crc16(data, 9U);
    /* CRC-16/CCITT-FALSE check value for "123456789" is 0x29B1. */
    ASSERT_EQ(crc, 0x29B1U);
}

TEST_CASE(CrcEngine_Crc32_KnownVector_Ascii123456789)
{
    const uint8_t data[] = "123456789";
    uint32_t crc = CrcEngine_Crc32(data, 9U);
    /* CRC-32/ISO-HDLC check value for "123456789" is 0xCBF43926. */
    ASSERT_EQ(crc, 0xCBF43926UL);
}

TEST_CASE(CrcEngine_Crc32_SingleBitChange_DifferentCrc)
{
    uint8_t data1[] = { 0x01, 0x02, 0x03, 0x04 };
    uint8_t data2[] = { 0x01, 0x02, 0x03, 0x05 };  /* last byte differs */

    uint32_t crc1 = CrcEngine_Crc32(data1, 4U);
    uint32_t crc2 = CrcEngine_Crc32(data2, 4U);

    ASSERT_NE(crc1, crc2);
}

TEST_CASE(CrcEngine_StreamApi_MatchesOneShot)
{
    const uint8_t data[] = "The quick brown fox";
    size_t len = 20U;

    uint32_t oneShot = CrcEngine_Crc32(data, len);

    CrcContext ctx;
    EcusStatus rc = CrcEngine_StreamInit(&ctx, CRC_ALG_CRC32_ISO);
    ASSERT_EQ(rc, ECUS_OK);

    /* Feed in two chunks to verify streaming correctness. */
    rc = CrcEngine_StreamUpdate(&ctx, data, 10U);
    ASSERT_EQ(rc, ECUS_OK);
    rc = CrcEngine_StreamUpdate(&ctx, data + 10, 10U);
    ASSERT_EQ(rc, ECUS_OK);

    uint32_t streamed = 0U;
    rc = CrcEngine_StreamFinalise(&ctx, &streamed);
    ASSERT_EQ(rc, ECUS_OK);

    ASSERT_EQ(streamed, oneShot);
}

TEST_CASE(CrcEngine_StreamApi_RejectsUpdateAfterFinalise)
{
    CrcContext ctx;
    (void)CrcEngine_StreamInit(&ctx, CRC_ALG_CRC8_SAE_J1850);

    uint8_t data[] = { 0x01 };
    uint32_t result = 0U;
    (void)CrcEngine_StreamUpdate(&ctx, data, 1U);
    (void)CrcEngine_StreamFinalise(&ctx, &result);

    /* Further update after finalise must be rejected. */
    EcusStatus rc = CrcEngine_StreamUpdate(&ctx, data, 1U);
    ASSERT_EQ(rc, ECUS_ERR_NOT_SUPPORTED);
}

TEST_CASE(CrcEngine_StreamInit_RejectsNullContext)
{
    EcusStatus rc = CrcEngine_StreamInit(NULL, CRC_ALG_CRC32_ISO);
    ASSERT_EQ(rc, ECUS_ERR_NULL_PTR);
}

/* =========================================================================
 * Public entry point — called from the master test runner.
 * ========================================================================= */
void Test_CrcEngine_RunAll(void)
{
    (void)CrcEngine_Init();

    RUN_TEST(CrcEngine_Crc8_EmptyBuffer_ReturnsXorOut);
    RUN_TEST(CrcEngine_Crc8_KnownVector_Ascii123456789);
    RUN_TEST(CrcEngine_Crc8_DeterministicAcrossCalls);
    RUN_TEST(CrcEngine_Crc16_KnownVector_Ascii123456789);
    RUN_TEST(CrcEngine_Crc32_KnownVector_Ascii123456789);
    RUN_TEST(CrcEngine_Crc32_SingleBitChange_DifferentCrc);
    RUN_TEST(CrcEngine_StreamApi_MatchesOneShot);
    RUN_TEST(CrcEngine_StreamApi_RejectsUpdateAfterFinalise);
    RUN_TEST(CrcEngine_StreamInit_RejectsNullContext);
}
