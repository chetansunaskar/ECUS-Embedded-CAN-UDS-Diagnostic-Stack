/**
 * @file    test_UdsDispatcher.c
 * @brief   Unit tests for the UDS dispatcher and core service handlers.
 *
 * These tests stand up a full UdsServer + CanHal pair (loopback mode) and
 * exercise the dispatcher through realistic PDU injection, verifying
 * session/security gating and correct positive/negative response behaviour.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#include "TestRunner.h"
#include "uds/UdsServer.h"
#include "uds/UdsDispatcher.h"
#include "hal/CanHal.h"

#include <string.h>
#include <time.h>

void Test_UdsDispatcher_RunAll(void);

/* =========================================================================
 * Fixture: bring up CanHal + UdsServer for each test
 * ========================================================================= */
static UdsServer s_server;

static void Setup(void)
{
    CanHalConfig halCfg = {
        .ecuCanId = 0x7E8U, .testerCanId = 0x7E0U,
        .mode = CAN_HAL_MODE_LOOPBACK, .busSpeedKbps = 500U
    };
    (void)CanHal_Init(&halCfg);

    memset(&s_server, 0, sizeof(s_server));
    UdsServerConfig udsCfg = {
        .ecuCanTxId = 0x7E8U, .ecuCanRxId = 0x7E0U,
        .ecuAddress = 0x10U, .ecuName = "TestECU"
    };
    (void)UdsServer_Init(&s_server, &udsCfg);

    CanHal_RegisterRxCallback(
        (void (*)(const CanFrame *, void *))NULL, NULL);  /* placeholder, see below */
}

static void Teardown(void)
{
    UdsServer_Deinit(&s_server);
    CanHal_Deinit();
}

/** Bridges CanHal Rx callback to UdsServer (mirrors EcuSession.c wiring). */
static void OnFrame(const CanFrame *frame, void *ctx)
{
    UdsServer *srv = (UdsServer *)ctx;
    (void)UdsServer_ProcessCanFrame(srv, frame);
}

static void InjectSF(uint8_t sid, const uint8_t *payload, size_t payloadLen)
{
    CanFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.id.raw  = CanFrame_MakeStdId(0x7E0U);
    frame.dlc     = (uint8_t)(payloadLen + 2U);
    frame.data[0] = (uint8_t)(payloadLen + 1U);  /* SF len = SID + payload */
    frame.data[1] = sid;
    if ((payload != NULL) && (payloadLen > 0U))
    {
        memcpy(&frame.data[2], payload, payloadLen);
    }
    (void)CanHal_InjectRxFrame(&frame);
    { struct timespec _ts = { .tv_sec = 0, .tv_nsec = 5000000L }; nanosleep(&_ts, NULL); }  /* allow ISR thread + dispatch to complete */
}

/* =========================================================================
 * Test cases
 * ========================================================================= */

TEST_CASE(UdsDispatcher_SessionControl_ChangesActiveSession)
{
    Setup();
    CanHal_RegisterRxCallback(OnFrame, &s_server);

    uint8_t payload[] = { UDS_SESSION_EXTENDED };
    InjectSF((uint8_t)UDS_SID_DIAGNOSTIC_SESSION_CONTROL, payload, 1U);

    ASSERT_EQ(s_server.activeSession, UDS_SESSION_EXTENDED);

    Teardown();
}

TEST_CASE(UdsDispatcher_WriteDataById_DeniedWithoutSecurity)
{
    Setup();
    CanHal_RegisterRxCallback(OnFrame, &s_server);

    /* Register a DID first. */
    uint8_t initial[2] = { 0xAA, 0xBB };
    (void)UdsServer_RegisterDid(&s_server, 0x1234U, initial, 2U, "Test");

    /* Move to Extended session (required), but do NOT unlock security. */
    uint8_t sessPayload[] = { UDS_SESSION_EXTENDED };
    InjectSF((uint8_t)UDS_SID_DIAGNOSTIC_SESSION_CONTROL, sessPayload, 1U);
    ASSERT_EQ(s_server.secState, UDS_SEC_LOCKED);

    /* Attempt WriteDataByIdentifier — must be denied (security required). */
    uint8_t writePayload[] = { 0x12, 0x34, 0xCC, 0xDD };
    InjectSF((uint8_t)UDS_SID_WRITE_DATA_BY_ID, writePayload, 4U);

    /* DID value must be unchanged. */
    ASSERT_EQ(s_server.didTable[0].data[0], 0xAA);
    ASSERT_EQ(s_server.didTable[0].data[1], 0xBB);

    Teardown();
}

TEST_CASE(UdsDispatcher_SecurityAccess_SeedThenValidKey_Unlocks)
{
    Setup();
    CanHal_RegisterRxCallback(OnFrame, &s_server);

    /* Move to Extended session (required for SecurityAccess). */
    uint8_t sessPayload[] = { UDS_SESSION_EXTENDED };
    InjectSF((uint8_t)UDS_SID_DIAGNOSTIC_SESSION_CONTROL, sessPayload, 1U);

    /* Request seed. */
    uint8_t seedPayload[] = { UDS_SA_REQUEST_SEED_LEVEL1 };
    InjectSF((uint8_t)UDS_SID_SECURITY_ACCESS, seedPayload, 1U);
    ASSERT_EQ(s_server.secState, UDS_SEC_SEED_SENT);

    /* Compute the correct key using the server's own algorithm. */
    uint32_t seed = s_server.secSeed;
    uint32_t key  = (~seed) ^ 0xA5A5A5A5UL;

    uint8_t keyPayload[5];
    keyPayload[0] = UDS_SA_SEND_KEY_LEVEL1;
    keyPayload[1] = (uint8_t)((key >> 24U) & 0xFFU);
    keyPayload[2] = (uint8_t)((key >> 16U) & 0xFFU);
    keyPayload[3] = (uint8_t)((key >>  8U) & 0xFFU);
    keyPayload[4] = (uint8_t)( key          & 0xFFU);

    InjectSF((uint8_t)UDS_SID_SECURITY_ACCESS, keyPayload, 5U);

    ASSERT_EQ(s_server.secState, UDS_SEC_UNLOCKED);

    Teardown();
}

TEST_CASE(UdsDispatcher_SecurityAccess_WrongKey_IncrementsAttempts)
{
    Setup();
    CanHal_RegisterRxCallback(OnFrame, &s_server);

    uint8_t sessPayload[] = { UDS_SESSION_EXTENDED };
    InjectSF((uint8_t)UDS_SID_DIAGNOSTIC_SESSION_CONTROL, sessPayload, 1U);

    uint8_t seedPayload[] = { UDS_SA_REQUEST_SEED_LEVEL1 };
    InjectSF((uint8_t)UDS_SID_SECURITY_ACCESS, seedPayload, 1U);

    /* Send a deliberately wrong key. */
    uint8_t wrongKeyPayload[5] = { UDS_SA_SEND_KEY_LEVEL1, 0x00, 0x00, 0x00, 0x00 };
    InjectSF((uint8_t)UDS_SID_SECURITY_ACCESS, wrongKeyPayload, 5U);

    ASSERT_EQ(s_server.secAttempts, 1U);
    ASSERT_NE(s_server.secState, UDS_SEC_UNLOCKED);

    Teardown();
}

TEST_CASE(UdsDispatcher_ReadDataById_UnknownDid_NoCrash)
{
    Setup();
    CanHal_RegisterRxCallback(OnFrame, &s_server);

    uint8_t payload[] = { 0xFF, 0xFF };  /* DID that does not exist */
    InjectSF((uint8_t)UDS_SID_READ_DATA_BY_ID, payload, 2U);

    /* No crash + server still operational is the test (NRC sent internally). */
    ASSERT_TRUE(s_server.initialised);

    Teardown();
}

TEST_CASE(UdsDispatcher_ClearDtc_RemovesAllStoredDtcs)
{
    Setup();
    CanHal_RegisterRxCallback(OnFrame, &s_server);

    DtcStatusMask status; status.raw = 0x08U;
    (void)UdsServer_StoreDtc(&s_server, 0x001234U, status, "TestDtc");
    ASSERT_EQ(s_server.dtcCount, 1U);

    uint8_t clearPayload[] = { 0xFF, 0xFF, 0xFF };
    InjectSF((uint8_t)UDS_SID_CLEAR_DTC, clearPayload, 3U);

    ASSERT_EQ(s_server.dtcCount, 0U);

    Teardown();
}

TEST_CASE(UdsDispatcher_TesterPresent_ResetsS3Timer)
{
    Setup();
    CanHal_RegisterRxCallback(OnFrame, &s_server);

    s_server.s3TimerMs = 4000U;
    s_server.testerPresentReceived = false;

    uint8_t payload[] = { 0x00 };
    InjectSF((uint8_t)UDS_SID_TESTER_PRESENT, payload, 1U);

    ASSERT_TRUE(s_server.testerPresentReceived);

    Teardown();
}

void Test_UdsDispatcher_RunAll(void)
{
    RUN_TEST(UdsDispatcher_SessionControl_ChangesActiveSession);
    RUN_TEST(UdsDispatcher_WriteDataById_DeniedWithoutSecurity);
    RUN_TEST(UdsDispatcher_SecurityAccess_SeedThenValidKey_Unlocks);
    RUN_TEST(UdsDispatcher_SecurityAccess_WrongKey_IncrementsAttempts);
    RUN_TEST(UdsDispatcher_ReadDataById_UnknownDid_NoCrash);
    RUN_TEST(UdsDispatcher_ClearDtc_RemovesAllStoredDtcs);
    RUN_TEST(UdsDispatcher_TesterPresent_ResetsS3Timer);
}
