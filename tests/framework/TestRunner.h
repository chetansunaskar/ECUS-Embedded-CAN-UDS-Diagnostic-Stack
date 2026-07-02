/**
 * @file    TestRunner.h
 * @brief   Minimal embedded-style unit test framework (no external deps).
 *
 * Design rationale: embedded projects frequently cannot pull in heavyweight
 * frameworks (Unity, CMock, Google Test) due to toolchain or licensing
 * constraints.  This framework demonstrates the same pattern using nothing
 * but the C standard library — directly portable to a cross-compiled
 * target's host-side test harness.
 *
 * Usage pattern:
 *   TEST_CASE(MyModule_DoesSomething)
 *   {
 *       ASSERT_EQ(2 + 2, 4);
 *       ASSERT_TRUE(some_condition);
 *   }
 *
 *   int main(void)
 *   {
 *       TestRunner_Begin("MyModule Tests");
 *       RUN_TEST(MyModule_DoesSomething);
 *       return TestRunner_Summary();
 *   }
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <stdio.h>
#include <string.h>
#include <math.h>

/* =========================================================================
 * Global test counters (translation-unit-local in TestRunner.c)
 * ========================================================================= */
extern int g_testsRun;
extern int g_testsPassed;
extern int g_testsFailed;
extern int g_assertionsInCurrentTest;

/* =========================================================================
 * Test case declaration macro
 * Defines a function returning void, named after the test.
 * ========================================================================= */
#define TEST_CASE(name)   static void name(void)

/* =========================================================================
 * Test execution macro — runs a test case and tracks pass/fail.
 * ========================================================================= */
#define RUN_TEST(name)                                                   \
    do {                                                                  \
        g_testsRun++;                                                     \
        g_assertionsInCurrentTest = 0;                                    \
        int failuresBefore = g_testsFailed;                               \
        printf("  [RUN ]  %-50s", #name);                                 \
        fflush(stdout);                                                   \
        name();                                                           \
        if (g_testsFailed == failuresBefore) {                            \
            g_testsPassed++;                                              \
            printf("\r  [PASS]  %-50s (%d assertions)\n",                 \
                   #name, g_assertionsInCurrentTest);                     \
        } else {                                                          \
            printf("\n");                                                 \
        }                                                                 \
    } while (0)

/* =========================================================================
 * Assertion macros
 * ========================================================================= */

#define ASSERT_TRUE(cond)                                                \
    do {                                                                  \
        g_assertionsInCurrentTest++;                                      \
        if (!(cond)) {                                                    \
            g_testsFailed++;                                              \
            printf("\n    [FAIL] %s:%d  ASSERT_TRUE(%s) failed\n",        \
                   __FILE__, __LINE__, #cond);                            \
        }                                                                  \
    } while (0)

#define ASSERT_FALSE(cond)        ASSERT_TRUE(!(cond))

#define ASSERT_EQ(actual, expected)                                      \
    do {                                                                  \
        g_assertionsInCurrentTest++;                                      \
        long long _a = (long long)(actual);                              \
        long long _e = (long long)(expected);                            \
        if (_a != _e) {                                                   \
            g_testsFailed++;                                              \
            printf("\n    [FAIL] %s:%d  ASSERT_EQ(%s, %s) failed: "       \
                   "got %lld, expected %lld\n",                           \
                   __FILE__, __LINE__, #actual, #expected, _a, _e);       \
        }                                                                  \
    } while (0)

#define ASSERT_NE(actual, expected)                                      \
    do {                                                                  \
        g_assertionsInCurrentTest++;                                      \
        long long _a = (long long)(actual);                              \
        long long _e = (long long)(expected);                            \
        if (_a == _e) {                                                   \
            g_testsFailed++;                                              \
            printf("\n    [FAIL] %s:%d  ASSERT_NE(%s, %s) failed: "       \
                   "both equal %lld\n",                                   \
                   __FILE__, __LINE__, #actual, #expected, _a);           \
        }                                                                  \
    } while (0)

#define ASSERT_NULL(ptr)                                                 \
    do {                                                                  \
        g_assertionsInCurrentTest++;                                      \
        if ((ptr) != NULL) {                                              \
            g_testsFailed++;                                              \
            printf("\n    [FAIL] %s:%d  ASSERT_NULL(%s) failed: "         \
                   "pointer was not NULL\n",                              \
                   __FILE__, __LINE__, #ptr);                             \
        }                                                                  \
    } while (0)

#define ASSERT_NOT_NULL(ptr)                                             \
    do {                                                                  \
        g_assertionsInCurrentTest++;                                      \
        if ((ptr) == NULL) {                                              \
            g_testsFailed++;                                              \
            printf("\n    [FAIL] %s:%d  ASSERT_NOT_NULL(%s) failed: "     \
                   "pointer was NULL\n",                                  \
                   __FILE__, __LINE__, #ptr);                             \
        }                                                                  \
    } while (0)

#define ASSERT_STR_EQ(actual, expected)                                  \
    do {                                                                  \
        g_assertionsInCurrentTest++;                                      \
        if (strcmp((actual), (expected)) != 0) {                          \
            g_testsFailed++;                                              \
            printf("\n    [FAIL] %s:%d  ASSERT_STR_EQ failed: "           \
                   "got \"%s\", expected \"%s\"\n",                       \
                   __FILE__, __LINE__, (actual), (expected));             \
        }                                                                  \
    } while (0)

#define ASSERT_MEM_EQ(actual, expected, len)                             \
    do {                                                                  \
        g_assertionsInCurrentTest++;                                      \
        if (memcmp((actual), (expected), (len)) != 0) {                   \
            g_testsFailed++;                                              \
            printf("\n    [FAIL] %s:%d  ASSERT_MEM_EQ failed "            \
                   "(%zu bytes differ)\n",                                \
                   __FILE__, __LINE__, (size_t)(len));                    \
        }                                                                  \
    } while (0)

/* =========================================================================
 * Runner lifecycle functions
 * ========================================================================= */

/** Print a banner before a test suite begins. */
void TestRunner_Begin(const char *suiteName);

/** Print the final pass/fail summary and return an exit code (0 = all pass). */
int TestRunner_Summary(void);

#endif /* TEST_RUNNER_H */
