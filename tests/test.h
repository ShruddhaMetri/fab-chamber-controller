/*
 * test.h
 * A minimal unit-test framework (about 40 lines). Industry projects often use
 * Unity or GoogleTest; the idea is identical:
 *   - each test is a function that sets something up and CHECKs the result
 *   - a failed CHECK prints file, line and the condition, and the test fails
 *   - the program's exit code is 0 only if every test passed, so a script or
 *     CI server (GitHub Actions) can tell pass from fail automatically
 */
#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <stdlib.h>

extern int g_tests_run;
extern int g_tests_failed;
extern int g_current_failed;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            g_current_failed = 1;                                            \
        }                                                                    \
    } while (0)

/* Like CHECK, but also prints the two values: easier to see what went wrong */
#define CHECK_EQ(actual, expected)                                           \
    do {                                                                     \
        long long a_ = (long long)(actual), e_ = (long long)(expected);      \
        if (a_ != e_) {                                                      \
            printf("    FAIL %s:%d: %s == %lld, expected %lld\n",            \
                   __FILE__, __LINE__, #actual, a_, e_);                     \
            g_current_failed = 1;                                            \
        }                                                                    \
    } while (0)

#define CHECK_RANGE(actual, lo, hi)                                          \
    do {                                                                     \
        double v_ = (double)(actual);                                        \
        if (v_ < (double)(lo) || v_ > (double)(hi)) {                        \
            printf("    FAIL %s:%d: %s == %.2f, expected %.2f .. %.2f\n",    \
                   __FILE__, __LINE__, #actual, v_, (double)(lo), (double)(hi)); \
            g_current_failed = 1;                                            \
        }                                                                    \
    } while (0)

#define RUN_TEST(fn)                                                         \
    do {                                                                     \
        g_current_failed = 0;                                                \
        g_tests_run++;                                                       \
        fn();                                                                \
        printf("  %s  %s\n", g_current_failed ? "FAIL" : "ok  ", #fn);       \
        if (g_current_failed) { g_tests_failed++; }                          \
    } while (0)

#endif /* TEST_H */
