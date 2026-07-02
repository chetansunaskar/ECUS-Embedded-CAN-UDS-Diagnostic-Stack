/**
 * @file    test_DtcScenario.c
 * @brief   Integration test: full DTC lifecycle through the live UDS stack.
 *
 * Unlike the unit tests (which test one module in isolation), this test
 * exercises the complete path: CLI-style PDU injection → CanHal → ISO-TP
 * → UDS dispatcher → DTC storage → ReadDTCInformation → ClearDiagnostic.
 *
 * This mirrors how a real tester tool (CANoe, Vector CDD) would interact
 * with the ECU over an actual CAN bus.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#include "TestRunner.h"
#include "uds/UdsServer.h"
#include "hal/CanHal.h"

#include <string.h>
#include <time.h>

void Test_DtcScenario_RunAll(void);

static UdsServer s_server;

static void OnFrame(const CanFrame *frame, void *ctx)
{
    (void)UdsServer_ProcessCanFrame((UdsServer *)ctx, frame);
}

static void InjectSF(uint8_t sid, const uint8_t *payload, size_t len)
{
    CanFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.id.raw  = CanFrame_MakeStdId(0x7E0U);
    frame.dlc     = (uint8_t)(len + 2U);
    frame.data[0] = (uint8_t)(len + 1U);
    frame.data[1] = sid;
    if ((payload != NULL) && (len > 0U)) { memcpy(&frame.data[2], payload, len); }
    (void)CanHal_InjectRxFrame(&frame);
    { struct timespec _ts = { .tv_sec = 0, .tv_nsec = 5000000L }; nanosleep(&_ts, NULL); }
}

TEST_CASE(Integration_FullDtcLifecycle_StoreReadClearVerify)
{
    /* --- Bring up the full stack (mirrors EcuSession_Start) --- */
    CanHalConfig halCfg = {
        .ecuCanId = 0x7E8U, .testerCanId = 0x7E0U,
        .mode = CAN_HAL_MODE_LOOPBACK, .busSpeedKbps = 500U
    };
    (void)CanHal_Init(&halCfg);

    memset(&s_server, 0, sizeof(s_server));
    UdsServerConfig udsCfg = {
        .ecuCanTxId = 0x7E8U, .ecuCanRxId = 0x7E0U,
        .ecuAddress = 0x10U, .ecuName = "IntegrationTestECU"
    };
    (void)UdsServer_Init(&s_server, &udsCfg);
    CanHal_RegisterRxCallback(OnFrame, &s_server);

    /* --- Step 1: Store two DTCs (simulating fault detection) --- */
    DtcStatusMask st1; st1.raw = 0U; st1.bits.confirmedDtc = 1U; st1.bits.testFailed = 1U;
    DtcStatusMask st2; st2.raw = 0U; st2.bits.pendingDtc = 1U;

    (void)UdsServer_StoreDtc(&s_server, 0x001234U, st1, "EngineOverheat");
    (void)UdsServer_StoreDtc(&s_server, 0x005678U, st2, "SensorDrift");

    ASSERT_EQ(s_server.dtcCount, 2U);

    /* --- Step 2: Read DTCs via the real UDS service (0x19, sub 0x02) --- */
    uint8_t readPayload[] = { 0x02U, 0xFFU };
    InjectSF((uint8_t)UDS_SID_READ_DTC_INFO, readPayload, 2U);

    /* The dispatcher should have processed without error — server still up. */
    ASSERT_TRUE(s_server.initialised);
    ASSERT_EQ(s_server.dtcCount, 2U);  /* Read does not clear */

    /* --- Step 3: Clear all DTCs via the real UDS service (0x14) --- */
    uint8_t clearPayload[] = { 0xFFU, 0xFFU, 0xFFU };
    InjectSF((uint8_t)UDS_SID_CLEAR_DTC, clearPayload, 3U);

    ASSERT_EQ(s_server.dtcCount, 0U);

    /* --- Step 4: Confirm a subsequent read shows zero DTCs --- */
    InjectSF((uint8_t)UDS_SID_READ_DTC_INFO, readPayload, 2U);
    ASSERT_EQ(s_server.dtcCount, 0U);

    /* --- Cleanup --- */
    UdsServer_Deinit(&s_server);
    CanHal_Deinit();
}

TEST_CASE(Integration_SessionSecurityWriteFlow_EndToEnd)
{
    CanHalConfig halCfg = {
        .ecuCanId = 0x7E8U, .testerCanId = 0x7E0U,
        .mode = CAN_HAL_MODE_LOOPBACK, .busSpeedKbps = 500U
    };
    (void)CanHal_Init(&halCfg);

    memset(&s_server, 0, sizeof(s_server));
    UdsServerConfig udsCfg = {
        .ecuCanTxId = 0x7E8U, .ecuCanRxId = 0x7E0U,
        .ecuAddress = 0x10U, .ecuName = "IntegrationTestECU2"
    };
    (void)UdsServer_Init(&s_server, &udsCfg);
    CanHal_RegisterRxCallback(OnFrame, &s_server);

    /* Register a writable DID. */
    uint8_t initial[4] = { 0x00, 0x00, 0x00, 0x00 };
    (void)UdsServer_RegisterDid(&s_server, 0xABCDU, initial, 4U, "Calibration");

    /* 1. Enter Extended session. */
    uint8_t sess[] = { UDS_SESSION_EXTENDED };
    InjectSF((uint8_t)UDS_SID_DIAGNOSTIC_SESSION_CONTROL, sess, 1U);
    ASSERT_EQ(s_server.activeSession, UDS_SESSION_EXTENDED);

    /* 2. Request seed, compute and send correct key. */
    uint8_t seedReq[] = { UDS_SA_REQUEST_SEED_LEVEL1 };
    InjectSF((uint8_t)UDS_SID_SECURITY_ACCESS, seedReq, 1U);

    uint32_t key = (~s_server.secSeed) ^ 0xA5A5A5A5UL;
    uint8_t keyMsg[5] = {
        UDS_SA_SEND_KEY_LEVEL1,
        (uint8_t)((key >> 24U) & 0xFFU), (uint8_t)((key >> 16U) & 0xFFU),
        (uint8_t)((key >>  8U) & 0xFFU), (uint8_t)( key          & 0xFFU)
    };
    InjectSF((uint8_t)UDS_SID_SECURITY_ACCESS, keyMsg, 5U);
    ASSERT_EQ(s_server.secState, UDS_SEC_UNLOCKED);

    /* 3. Now WriteDataByIdentifier should succeed. */
    uint8_t writeMsg[6] = { 0xAB, 0xCD, 0x11, 0x22, 0x33, 0x44 };
    InjectSF((uint8_t)UDS_SID_WRITE_DATA_BY_ID, writeMsg, 6U);

    ASSERT_EQ(s_server.didTable[0].data[0], 0x11U);
    ASSERT_EQ(s_server.didTable[0].data[3], 0x44U);

    UdsServer_Deinit(&s_server);
    CanHal_Deinit();
}

void Test_DtcScenario_RunAll(void)
{
    RUN_TEST(Integration_FullDtcLifecycle_StoreReadClearVerify);
    RUN_TEST(Integration_SessionSecurityWriteFlow_EndToEnd);
}
