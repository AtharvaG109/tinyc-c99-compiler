/*
 * test_harness.h — Minimal unit test framework (no dependencies)
 */
#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int test_failures = 0;
static int test_count = 0;

#define TEST(name) static void test_##name(void)

#define RUN_TEST(name) do { \
    test_count++; \
    printf("  %-50s", #name); \
    fflush(stdout); \
    test_##name(); \
    printf("\033[1;32mPASS\033[0m\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("\033[1;31mFAIL\033[0m\n"); \
        fprintf(stderr, "    %s:%d: assertion failed: %s\n", __FILE__, __LINE__, #cond); \
        test_failures++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
        printf("\033[1;31mFAIL\033[0m\n"); \
        fprintf(stderr, "    %s:%d: expected %lld, got %lld\n", __FILE__, __LINE__, _b, _a); \
        test_failures++; \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    const char *_a = (a), *_b = (b); \
    if (!_a || !_b || strcmp(_a, _b) != 0) { \
        printf("\033[1;31mFAIL\033[0m\n"); \
        fprintf(stderr, "    %s:%d: expected \"%s\", got \"%s\"\n", \
                __FILE__, __LINE__, _b ? _b : "(null)", _a ? _a : "(null)"); \
        test_failures++; \
        return; \
    } \
} while(0)

#define TEST_SUMMARY() do { \
    printf("\n%d test(s), %d failure(s)\n", test_count, test_failures); \
    return test_failures > 0 ? 1 : 0; \
} while(0)

#endif /* TEST_HARNESS_H */
