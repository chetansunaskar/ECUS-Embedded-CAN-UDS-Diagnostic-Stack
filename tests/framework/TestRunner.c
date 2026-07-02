/**
 * @file    TestRunner.c
 * @brief   Test framework runner implementation.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#include "TestRunner.h"

int g_testsRun               = 0;
int g_testsPassed            = 0;
int g_testsFailed            = 0;
int g_assertionsInCurrentTest = 0;

void TestRunner_Begin(const char *suiteName)
{
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  %-62s  ║\n", suiteName);
    printf("╚══════════════════════════════════════════════════════════════╝\n");
}

int TestRunner_Summary(void)
{
    printf("\n");
    printf("──────────────────────────────────────────────────────────────────\n");
    printf("  Results: %d run, %d passed, %d failed\n",
           g_testsRun, g_testsPassed, g_testsFailed);
    printf("──────────────────────────────────────────────────────────────────\n");

    if (g_testsFailed == 0)
    {
        printf("  ✓ ALL TESTS PASSED\n\n");
        return 0;
    }

    printf("  ✗ %d TEST(S) FAILED\n\n", g_testsFailed);
    return 1;
}
