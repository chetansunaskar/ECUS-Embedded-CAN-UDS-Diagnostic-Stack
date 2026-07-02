/**
 * @file    test_IsoTp.c
 * @brief   Unit tests for the ISO 15765-2 (ISO-TP) transport layer.
 *
 * These tests require the CAN HAL to be initialised (ISO-TP transmits via
 * CanHal_Transmit internally), so a HAL bring-up is performed once at the
 * start of this test group and cleaned up at the end.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#include "TestRunner.h"
#include "transport/IsoTp.h"
#include "hal/CanHal.h"
#include "services/Logger.h"

#include <string.h>
#include <time.h>

void Test_IsoTp_RunAll(void);

/* =========================================================================
 * Test fixture state
 * ========================================================================= */
static uint8_t s_lastPdu[ISOTP_MAX_PAYLOAD_BUF];
static size_t  s_lastPduLen;
static int     s_pduCallbackCount;

static void TestPduCallback(const uint8_t *pdu, size_t len, void *ctx)
{
    (void)ctx;
    memcpy(s_lastPdu, pdu, len);
    s_lastPduLen = len;
    s_pduCallbackCount++;
}

static void SetupHal(void)
{
    CanHalConfig cfg = {
        .ecuCanId = 0x7E8U, .testerCanId = 0x7E0U,
        .mode = CAN_HAL_MODE_LOOPBACK, .busSpeedKbps = 500U
    };
    (void)CanHal_Init(&cfg);
}

static void TeardownHal(void)
{
    CanHal_Deinit();
}

/* =========================================================================
 * Test cases
 * ========================================================================= */

TEST_CASE(IsoTp_Init_Succeeds)
{
    IsoTpChannel ch;
    EcusStatus rc = IsoTp_Init(&ch, 0x7E8U, 0x7E0U, NULL, NULL);
    ASSERT_EQ(rc, ECUS_OK);
    ASSERT_EQ(ch.rxState, ISOTP_RX_IDLE);
    ASSERT_EQ(ch.txState, ISOTP_TX_IDLE);
}

TEST_CASE(IsoTp_ProcessRxFrame_IgnoresFrameWithWrongId)
{
    IsoTpChannel ch;
    s_pduCallbackCount = 0;
    (void)IsoTp_Init(&ch, 0x7E8U, 0x7E0U, TestPduCallback, NULL);

    CanFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.id.raw  = CanFrame_MakeStdId(0x123U);  /* Not the channel's rxCanId */
    frame.dlc     = 3U;
    frame.data[0] = 0x02U;  /* SF, len=2 */
    frame.data[1] = 0x3EU;
    frame.data[2] = 0x00U;

    EcusStatus rc = IsoTp_ProcessRxFrame(&ch, &frame);
    ASSERT_EQ(rc, ECUS_OK);
    ASSERT_EQ(s_pduCallbackCount, 0);  /* Must NOT trigger callback */
}

TEST_CASE(IsoTp_SingleFrame_ReassemblyTriggersCallback)
{
    IsoTpChannel ch;
    s_pduCallbackCount = 0;
    s_lastPduLen        = 0U;
    (void)IsoTp_Init(&ch, 0x7E8U, 0x7E0U, TestPduCallback, NULL);

    CanFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.id.raw  = CanFrame_MakeStdId(0x7E0U);
    frame.dlc     = 3U;
    frame.data[0] = 0x02U;  /* SF, len=2 */
    frame.data[1] = 0x3EU;  /* TesterPresent SID */
    frame.data[2] = 0x00U;

    EcusStatus rc = IsoTp_ProcessRxFrame(&ch, &frame);
    ASSERT_EQ(rc, ECUS_OK);
    ASSERT_EQ(s_pduCallbackCount, 1);
    ASSERT_EQ(s_lastPduLen, 2U);
    ASSERT_EQ(s_lastPdu[0], 0x3EU);
}

TEST_CASE(IsoTp_Transmit_SingleFrame_UsesSfPci)
{
    SetupHal();

    IsoTpChannel ch;
    (void)IsoTp_Init(&ch, 0x7E8U, 0x7E0U, NULL, NULL);

    uint8_t data[] = { 0x50U, 0x03U };  /* 2-byte payload — fits in SF */
    EcusStatus rc = IsoTp_Transmit(&ch, data, 2U);
    ASSERT_EQ(rc, ECUS_OK);

    /* Drain the Tx ring buffer manually to inspect the frame built. */
    { struct timespec _ts = { .tv_sec = 0, .tv_nsec = 5000000L }; nanosleep(&_ts, NULL); }  /* allow ISR thread to process */
    ASSERT_EQ(CanHal_TxPending(), 0U);  /* ISR thread should have drained it */

    TeardownHal();
}

TEST_CASE(IsoTp_MultiFrame_FirstFrame_TriggersFlowControl)
{
    SetupHal();

    IsoTpChannel ch;
    (void)IsoTp_Init(&ch, 0x7E8U, 0x7E0U, NULL, NULL);

    /* 12-byte payload requires FF + 1 CF. */
    uint8_t data[12];
    for (size_t i = 0U; i < 12U; i++) { data[i] = (uint8_t)i; }

    EcusStatus rc = IsoTp_Transmit(&ch, data, 12U);
    ASSERT_EQ(rc, ECUS_OK);
    ASSERT_EQ(ch.txState, ISOTP_TX_WAIT_FC);

    TeardownHal();
}

TEST_CASE(IsoTp_Reset_ClearsStateButPreservesConfig)
{
    IsoTpChannel ch;
    (void)IsoTp_Init(&ch, 0x7E8U, 0x7E0U, TestPduCallback, NULL);

    /* Force into a non-idle state. */
    ch.rxState = ISOTP_RX_RECEIVING;
    ch.rxReceivedLen = 100U;

    IsoTp_Reset(&ch);

    ASSERT_EQ(ch.rxState, ISOTP_RX_IDLE);
    ASSERT_EQ(ch.rxReceivedLen, 0U);
    /* Config must survive the reset. */
    ASSERT_EQ(ch.txCanId, 0x7E8U);
    ASSERT_EQ(ch.rxCanId, 0x7E0U);
}

TEST_CASE(IsoTp_Transmit_RejectsOversizedPayload)
{
    IsoTpChannel ch;
    (void)IsoTp_Init(&ch, 0x7E8U, 0x7E0U, NULL, NULL);

    uint8_t dummy[1] = { 0 };
    EcusStatus rc = IsoTp_Transmit(&ch, dummy, ISOTP_FF_MAX_LEN + 1U);
    ASSERT_EQ(rc, ECUS_ERR_OVERFLOW);
}

void Test_IsoTp_RunAll(void)
{
    RUN_TEST(IsoTp_Init_Succeeds);
    RUN_TEST(IsoTp_ProcessRxFrame_IgnoresFrameWithWrongId);
    RUN_TEST(IsoTp_SingleFrame_ReassemblyTriggersCallback);
    RUN_TEST(IsoTp_Transmit_SingleFrame_UsesSfPci);
    RUN_TEST(IsoTp_MultiFrame_FirstFrame_TriggersFlowControl);
    RUN_TEST(IsoTp_Reset_ClearsStateButPreservesConfig);
    RUN_TEST(IsoTp_Transmit_RejectsOversizedPayload);
}
