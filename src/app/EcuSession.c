/**
 * @file    EcuSession.c
 * @brief   ECU bootstrap implementation — wires all subsystems together.
 *
 * C concepts demonstrated:
 *   Module composition root pattern, POSIX thread for periodic tick,
 *   callback wiring (CanHal Rx → UdsServer), static singleton instance,
 *   designated initializers for struct literals, string literals as data.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#include "app/EcuSession.h"
#include "hal/CanHal.h"
#include "services/Logger.h"

#include <string.h>
#include <time.h>
#include <pthread.h>

/* =========================================================================
 * Module state (singleton — one simulated ECU per process)
 * ========================================================================= */
static UdsServer       s_server;
static pthread_t       s_tickThread;
static volatile bool   s_tickRunning = false;
static bool            s_started     = false;

/* =========================================================================
 * Private: CanHal Rx callback — bridges HAL to UDS server
 * ========================================================================= */
static void OnCanFrameReceived(const CanFrame *frame, void *userCtx)
{
    UdsServer *server = (UdsServer *)userCtx;
    (void)UdsServer_ProcessCanFrame(server, frame);
}

/* =========================================================================
 * Private: periodic tick thread
 *
 * Drives UdsServer_Tick() every 10 ms — simulates the periodic task that
 * would run in a real RTOS (e.g. a FreeRTOS task with vTaskDelay(10)).
 * ========================================================================= */
static void *TickThreadFn(void *arg)
{
    ECUS_UNUSED_PARAM(arg);

    struct timespec sleepTime = { .tv_sec = 0, .tv_nsec = 10000000L }; /* 10 ms */

    LOG_DEBUG("EcuSession: tick thread started (10 ms period)");

    while (s_tickRunning)
    {
        UdsServer_Tick(&s_server, 10U);
        (void)nanosleep(&sleepTime, NULL);
    }

    LOG_DEBUG("EcuSession: tick thread stopped");
    return NULL;
}

/* =========================================================================
 * Private: register demonstration DIDs and DTCs
 *
 * Mirrors realistic automotive DIDs: VIN, software version, supply voltage.
 * ========================================================================= */
static void RegisterDemoData(UdsServer *server)
{
    /* DID 0xF190: VIN (Vehicle Identification Number) — 17 ASCII bytes. */
    const uint8_t vin[17] = "ECUS00SIMULATOR1";
    (void)UdsServer_RegisterDid(server, 0xF190U, vin, 17U, "VIN");

    /* DID 0xF1A0: Software Version (3-byte BCD: Major.Minor.Patch). */
    const uint8_t swVer[3] = { ECUS_VERSION_MAJOR, ECUS_VERSION_MINOR, ECUS_VERSION_PATCH };
    (void)UdsServer_RegisterDid(server, 0xF1A0U, swVer, 3U, "SoftwareVersion");

    /* DID 0xF1A1: Supply voltage in mV (2-byte big-endian, e.g. 12000 mV). */
    const uint8_t voltage[2] = { 0x2EU, 0xE0U };   /* 0x2EE0 = 12000 mV */
    (void)UdsServer_RegisterDid(server, 0xF1A1U, voltage, 2U, "SupplyVoltage_mV");

    /* DID 0xF1A2: ECU Serial Number. */
    const uint8_t serial[8] = { 0x45U, 0x43U, 0x55U, 0x53U, 0x00U, 0x00U, 0x00U, 0x01U };
    (void)UdsServer_RegisterDid(server, 0xF1A2U, serial, 8U, "ECUSerialNumber");

    /* Pre-load one demonstration DTC: P0A1F (Battery voltage low). */
    DtcStatusMask status;
    status.raw = 0U;
    status.bits.testFailed   = 1U;
    status.bits.confirmedDtc = 1U;
    status.bits.warningIndicator = 1U;

    (void)UdsServer_StoreDtc(server, 0x00A1FUL, status, "BatteryVoltageLow");

    LOG_INFO("EcuSession: demo data registered (4 DIDs, 1 DTC)");
}

/* =========================================================================
 * Public API
 * ========================================================================= */

EcusStatus EcuSession_Start(void)
{
    if (s_started)
    {
        return ECUS_ERR_ALREADY_INIT;
    }

    (void)memset(&s_server, 0, sizeof(s_server));

    /* --- Initialise CAN HAL (loopback mode for simulation) --- */
    CanHalConfig halConfig = {
        .ecuCanId     = 0x7E8U,   /* ECU response ID (physical addressing) */
        .testerCanId  = 0x7E0U,   /* Tester request ID                     */
        .mode         = CAN_HAL_MODE_LOOPBACK,
        .busSpeedKbps = 500U
    };
    ECUS_PROPAGATE(CanHal_Init(&halConfig));

    /* --- Initialise UDS Server --- */
    UdsServerConfig udsConfig = {
        .ecuCanTxId = 0x7E8U,   /* ECU → Tester */
        .ecuCanRxId = 0x7E0U,   /* Tester → ECU */
        .ecuAddress = 0x10U,
        .ecuName    = "ECUS-DemoECU"
    };
    ECUS_PROPAGATE(UdsServer_Init(&s_server, &udsConfig));

    /* --- Wire CAN HAL Rx callback to UDS server --- */
    CanHal_RegisterRxCallback(OnCanFrameReceived, &s_server);

    /* --- Register demonstration DIDs and DTCs --- */
    RegisterDemoData(&s_server);

    /* --- Start the periodic tick thread --- */
    s_tickRunning = true;
    if (pthread_create(&s_tickThread, NULL, TickThreadFn, NULL) != 0)
    {
        s_tickRunning = false;
        CanHal_Deinit();
        return ECUS_ERR_GENERIC;
    }

    s_started = true;

    LOG_INFO("EcuSession started successfully");
    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

void EcuSession_Stop(void)
{
    if (!s_started)
    {
        return;
    }

    s_tickRunning = false;
    (void)pthread_join(s_tickThread, NULL);

    UdsServer_Deinit(&s_server);
    CanHal_Deinit();

    s_started = false;
    LOG_INFO("EcuSession stopped");
}

/* -------------------------------------------------------------------------- */

UdsServer *EcuSession_GetServer(void)
{
    return s_started ? &s_server : NULL;
}
