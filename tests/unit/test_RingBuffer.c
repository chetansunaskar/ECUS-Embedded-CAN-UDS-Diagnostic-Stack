/**
 * @file    test_RingBuffer.c
 * @brief   Unit tests for the SPSC lock-free ring buffer.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#include "TestRunner.h"
#include "transport/RingBuffer.h"

void Test_RingBuffer_RunAll(void);

#include <string.h>

TEST_CASE(RingBuffer_Init_RejectsNonPowerOfTwoCapacity)
{
    uint32_t storage[10];
    RingBuffer rb;
    EcusStatus rc = RingBuffer_Init(&rb, storage, sizeof(uint32_t), 10U);
    ASSERT_EQ(rc, ECUS_ERR_INVALID_PARAM);
}

TEST_CASE(RingBuffer_Init_AcceptsPowerOfTwoCapacity)
{
    uint32_t storage[8];
    RingBuffer rb;
    EcusStatus rc = RingBuffer_Init(&rb, storage, sizeof(uint32_t), 8U);
    ASSERT_EQ(rc, ECUS_OK);
}

TEST_CASE(RingBuffer_EmptyAfterInit)
{
    uint32_t storage[8];
    RingBuffer rb;
    (void)RingBuffer_Init(&rb, storage, sizeof(uint32_t), 8U);
    ASSERT_TRUE(RingBuffer_IsEmpty(&rb));
    ASSERT_FALSE(RingBuffer_IsFull(&rb));
    ASSERT_EQ(RingBuffer_Count(&rb), 0U);
}

TEST_CASE(RingBuffer_PushPop_SingleElement_RoundTrips)
{
    uint32_t storage[4];
    RingBuffer rb;
    (void)RingBuffer_Init(&rb, storage, sizeof(uint32_t), 4U);

    uint32_t in = 0xCAFEBABEUL;
    EcusStatus rc = RingBuffer_Push(&rb, &in);
    ASSERT_EQ(rc, ECUS_OK);
    ASSERT_EQ(RingBuffer_Count(&rb), 1U);

    uint32_t out = 0U;
    rc = RingBuffer_Pop(&rb, &out);
    ASSERT_EQ(rc, ECUS_OK);
    ASSERT_EQ(out, in);
    ASSERT_TRUE(RingBuffer_IsEmpty(&rb));
}

TEST_CASE(RingBuffer_FIFO_Order_Preserved)
{
    uint32_t storage[4];
    RingBuffer rb;
    (void)RingBuffer_Init(&rb, storage, sizeof(uint32_t), 4U);

    for (uint32_t i = 0U; i < 4U; i++)
    {
        EcusStatus rc = RingBuffer_Push(&rb, &i);
        ASSERT_EQ(rc, ECUS_OK);
    }

    for (uint32_t i = 0U; i < 4U; i++)
    {
        uint32_t out = 999U;
        EcusStatus rc = RingBuffer_Pop(&rb, &out);
        ASSERT_EQ(rc, ECUS_OK);
        ASSERT_EQ(out, i);   /* FIFO: must come out in push order */
    }
}

TEST_CASE(RingBuffer_Push_FailsWhenFull)
{
    uint32_t storage[2];
    RingBuffer rb;
    (void)RingBuffer_Init(&rb, storage, sizeof(uint32_t), 2U);

    uint32_t a = 1U, b = 2U, c = 3U;
    ASSERT_EQ(RingBuffer_Push(&rb, &a), ECUS_OK);
    ASSERT_EQ(RingBuffer_Push(&rb, &b), ECUS_OK);
    ASSERT_TRUE(RingBuffer_IsFull(&rb));

    /* Third push must fail — buffer is full. */
    EcusStatus rc = RingBuffer_Push(&rb, &c);
    ASSERT_EQ(rc, ECUS_ERR_OVERFLOW);
}

TEST_CASE(RingBuffer_Pop_FailsWhenEmpty)
{
    uint32_t storage[4];
    RingBuffer rb;
    (void)RingBuffer_Init(&rb, storage, sizeof(uint32_t), 4U);

    uint32_t out;
    EcusStatus rc = RingBuffer_Pop(&rb, &out);
    ASSERT_EQ(rc, ECUS_ERR_UNDERFLOW);
}

TEST_CASE(RingBuffer_WrapAround_WorksCorrectly)
{
    /* Capacity 4: push 3, pop 2, push 3 more — forces index wraparound. */
    uint32_t storage[4];
    RingBuffer rb;
    (void)RingBuffer_Init(&rb, storage, sizeof(uint32_t), 4U);

    for (uint32_t i = 0U; i < 3U; i++) { (void)RingBuffer_Push(&rb, &i); }

    uint32_t out;
    (void)RingBuffer_Pop(&rb, &out);  ASSERT_EQ(out, 0U);
    (void)RingBuffer_Pop(&rb, &out);  ASSERT_EQ(out, 1U);

    /* Now push 3 more — this wraps the internal index past capacity. */
    for (uint32_t i = 10U; i < 13U; i++)
    {
        EcusStatus rc = RingBuffer_Push(&rb, &i);
        ASSERT_EQ(rc, ECUS_OK);
    }

    /* Expected remaining order: [2, 10, 11, 12] */
    uint32_t expected[] = { 2U, 10U, 11U, 12U };
    for (size_t i = 0U; i < 4U; i++)
    {
        (void)RingBuffer_Pop(&rb, &out);
        ASSERT_EQ(out, expected[i]);
    }
}

TEST_CASE(RingBuffer_Peek_DoesNotRemoveElement)
{
    uint32_t storage[4];
    RingBuffer rb;
    (void)RingBuffer_Init(&rb, storage, sizeof(uint32_t), 4U);

    uint32_t in = 42U;
    (void)RingBuffer_Push(&rb, &in);

    uint32_t peeked = 0U;
    EcusStatus rc = RingBuffer_Peek(&rb, &peeked);
    ASSERT_EQ(rc, ECUS_OK);
    ASSERT_EQ(peeked, 42U);

    /* Count must be unchanged after peek. */
    ASSERT_EQ(RingBuffer_Count(&rb), 1U);
}

TEST_CASE(RingBuffer_Flush_ResetsToEmpty)
{
    uint32_t storage[4];
    RingBuffer rb;
    (void)RingBuffer_Init(&rb, storage, sizeof(uint32_t), 4U);

    uint32_t in = 1U;
    (void)RingBuffer_Push(&rb, &in);
    (void)RingBuffer_Push(&rb, &in);

    RingBuffer_Flush(&rb);
    ASSERT_TRUE(RingBuffer_IsEmpty(&rb));
    ASSERT_EQ(RingBuffer_Count(&rb), 0U);
}

void Test_RingBuffer_RunAll(void)
{
    RUN_TEST(RingBuffer_Init_RejectsNonPowerOfTwoCapacity);
    RUN_TEST(RingBuffer_Init_AcceptsPowerOfTwoCapacity);
    RUN_TEST(RingBuffer_EmptyAfterInit);
    RUN_TEST(RingBuffer_PushPop_SingleElement_RoundTrips);
    RUN_TEST(RingBuffer_FIFO_Order_Preserved);
    RUN_TEST(RingBuffer_Push_FailsWhenFull);
    RUN_TEST(RingBuffer_Pop_FailsWhenEmpty);
    RUN_TEST(RingBuffer_WrapAround_WorksCorrectly);
    RUN_TEST(RingBuffer_Peek_DoesNotRemoveElement);
    RUN_TEST(RingBuffer_Flush_ResetsToEmpty);
}
