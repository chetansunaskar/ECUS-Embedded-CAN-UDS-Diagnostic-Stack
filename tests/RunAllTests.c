/**
 * @file    RunAllTests.c
 * @brief   Master entry point for the ECUS unit + integration test suite.
 *
 * Links together all *_RunAll() functions from individual test files and
 * executes them in a sensible dependency order: foundational services
 * first (CRC, RingBuffer, MemPool), then protocol layers (ISO-TP), then
 * the UDS dispatcher, then full-stack integration scenarios.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#include "TestRunner.h"
#include "services/ErrorHandler.h"
#include "services/Logger.h"

/* Forward declarations of each test file's entry point. */
void Test_CrcEngine_RunAll(void);
void Test_RingBuffer_RunAll(void);
void Test_MemPool_RunAll(void);
void Test_IsoTp_RunAll(void);
void Test_UdsDispatcher_RunAll(void);
void Test_DtcScenario_RunAll(void);

int main(void)
{
    /* Initialise core services required by the modules under test.
     * Logger is set to WARN level to keep test output focused on
     * PASS/FAIL lines rather than module debug noise.                    */
    (void)ErrorHandler_Init();

    LoggerConfig logCfg = {
        .minLevel         = LOG_LEVEL_WARN,
        .logFilePath      = NULL,
        .echoToStdout     = true,
        .colorEnabled     = false,
        .timestampEnabled = false
    };
    (void)Logger_Init(&logCfg);

    TestRunner_Begin("ECUS Unit + Integration Test Suite");

    printf("\n-- Service Layer --\n");
    Test_CrcEngine_RunAll();
    Test_MemPool_RunAll();

    printf("\n-- Transport Layer --\n");
    Test_RingBuffer_RunAll();
    Test_IsoTp_RunAll();

    printf("\n-- UDS Layer --\n");
    Test_UdsDispatcher_RunAll();

    printf("\n-- Integration Scenarios --\n");
    Test_DtcScenario_RunAll();

    Logger_Deinit();

    return TestRunner_Summary();
}
