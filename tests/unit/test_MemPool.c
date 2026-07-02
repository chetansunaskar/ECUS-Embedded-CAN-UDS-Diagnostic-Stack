/**
 * @file    test_MemPool.c
 * @brief   Unit tests for the fixed-size block memory pool.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#include "TestRunner.h"
#include "services/MemPool.h"

void Test_MemPool_RunAll(void);

#include <string.h>

TEST_CASE(MemPool_Init_RejectsBlockSizeTooSmall)
{
    uint8_t buf[64];
    MemPool pool;
    memset(&pool, 0, sizeof(pool));
    /* Block size smaller than sizeof(void*) must be rejected. */
    EcusStatus rc = MemPool_Init(&pool, buf, 2U, 4U);
    ASSERT_EQ(rc, ECUS_ERR_INVALID_PARAM);
}

TEST_CASE(MemPool_Init_Succeeds_WithValidParams)
{
    uint8_t buf[256];
    MemPool pool;
    memset(&pool, 0, sizeof(pool));
    EcusStatus rc = MemPool_Init(&pool, buf, 32U, 8U);
    ASSERT_EQ(rc, ECUS_OK);
    ASSERT_EQ(MemPool_FreeCount(&pool), 8U);
}

TEST_CASE(MemPool_Alloc_ReturnsNonNullUntilExhausted)
{
    uint8_t buf[128];
    MemPool pool;
    memset(&pool, 0, sizeof(pool));
    (void)MemPool_Init(&pool, buf, 32U, 4U);

    void *blocks[4];
    for (int i = 0; i < 4; i++)
    {
        blocks[i] = MemPool_Alloc(&pool);
        ASSERT_NOT_NULL(blocks[i]);
    }

    /* Pool now exhausted — next alloc must return NULL. */
    void *extra = MemPool_Alloc(&pool);
    ASSERT_NULL(extra);
}

TEST_CASE(MemPool_Alloc_ReturnsZeroedMemory)
{
    uint8_t buf[128];
    MemPool pool;
    memset(&pool, 0, sizeof(pool));
    (void)MemPool_Init(&pool, buf, 32U, 4U);

    /* Corrupt the underlying buffer first to prove zeroing happens. */
    memset(buf, 0xFF, sizeof(buf));
    /* Re-init to rebuild free list cleanly after corruption. */
    (void)memset(&pool, 0, sizeof(pool));
    (void)MemPool_Init(&pool, buf, 32U, 4U);

    uint8_t *block = (uint8_t *)MemPool_Alloc(&pool);
    ASSERT_NOT_NULL(block);

    bool allZero = true;
    for (size_t i = 0U; i < 32U; i++)
    {
        if (block[i] != 0U) { allZero = false; break; }
    }
    ASSERT_TRUE(allZero);
}

TEST_CASE(MemPool_Free_ReturnsBlockToPool)
{
    uint8_t buf[128];
    MemPool pool;
    memset(&pool, 0, sizeof(pool));
    (void)MemPool_Init(&pool, buf, 32U, 4U);

    void *block = MemPool_Alloc(&pool);
    ASSERT_EQ(MemPool_FreeCount(&pool), 3U);

    EcusStatus rc = MemPool_Free(&pool, block);
    ASSERT_EQ(rc, ECUS_OK);
    ASSERT_EQ(MemPool_FreeCount(&pool), 4U);
}

TEST_CASE(MemPool_Free_RejectsPointerOutsidePool)
{
    uint8_t buf[128];
    MemPool pool;
    memset(&pool, 0, sizeof(pool));
    (void)MemPool_Init(&pool, buf, 32U, 4U);

    int notFromPool = 42;
    EcusStatus rc = MemPool_Free(&pool, &notFromPool);
    ASSERT_EQ(rc, ECUS_ERR_INVALID_PARAM);
}

TEST_CASE(MemPool_AllocFreeAlloc_ReusesBlock)
{
    uint8_t buf[64];
    MemPool pool;
    memset(&pool, 0, sizeof(pool));
    (void)MemPool_Init(&pool, buf, 32U, 2U);

    void *first = MemPool_Alloc(&pool);
    (void)MemPool_Free(&pool, first);
    void *second = MemPool_Alloc(&pool);

    /* With a LIFO free list, the freed block should be reused immediately. */
    ASSERT_EQ((intptr_t)first, (intptr_t)second);
}

TEST_CASE(MemPool_Reset_ReclaimsAllBlocks)
{
    uint8_t buf[128];
    MemPool pool;
    memset(&pool, 0, sizeof(pool));
    (void)MemPool_Init(&pool, buf, 32U, 4U);

    void *b1 = MemPool_Alloc(&pool);
    void *b2 = MemPool_Alloc(&pool);
    ASSERT_NOT_NULL(b1);
    ASSERT_NOT_NULL(b2);
    ASSERT_EQ(MemPool_FreeCount(&pool), 2U);

    MemPool_Reset(&pool);
    ASSERT_EQ(MemPool_FreeCount(&pool), 4U);
}

void Test_MemPool_RunAll(void)
{
    RUN_TEST(MemPool_Init_RejectsBlockSizeTooSmall);
    RUN_TEST(MemPool_Init_Succeeds_WithValidParams);
    RUN_TEST(MemPool_Alloc_ReturnsNonNullUntilExhausted);
    RUN_TEST(MemPool_Alloc_ReturnsZeroedMemory);
    RUN_TEST(MemPool_Free_ReturnsBlockToPool);
    RUN_TEST(MemPool_Free_RejectsPointerOutsidePool);
    RUN_TEST(MemPool_AllocFreeAlloc_ReusesBlock);
    RUN_TEST(MemPool_Reset_ReclaimsAllBlocks);
}
