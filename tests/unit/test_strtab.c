/*
 * test_strtab.c — String table unit tests
 */
#include "test_harness.h"
#include "tinyc/strtab.h"
#include <string.h>

TEST(strtab_basic_intern) {
    Arena *a = arena_create(0);
    StringTable st; strtab_init(&st, a);
    int idx = strtab_intern(&st, "hello", 5);
    ASSERT(idx == 0);
    ASSERT_STR_EQ(strtab_get(&st, idx), "hello");
    ASSERT_EQ(strtab_len(&st, idx), 5);
    strtab_free(&st); arena_destroy(a);
}

TEST(strtab_dedup) {
    Arena *a = arena_create(0);
    StringTable st; strtab_init(&st, a);
    int i1 = strtab_intern(&st, "foo", 3);
    int i2 = strtab_intern(&st, "foo", 3);
    ASSERT_EQ(i1, i2); /* same string → same index */
    strtab_free(&st); arena_destroy(a);
}

TEST(strtab_different_strings) {
    Arena *a = arena_create(0);
    StringTable st; strtab_init(&st, a);
    int i1 = strtab_intern(&st, "abc", 3);
    int i2 = strtab_intern(&st, "def", 3);
    ASSERT(i1 != i2);
    ASSERT_STR_EQ(strtab_get(&st, i1), "abc");
    ASSERT_STR_EQ(strtab_get(&st, i2), "def");
    strtab_free(&st); arena_destroy(a);
}

TEST(strtab_many_strings) {
    Arena *a = arena_create(0);
    StringTable st; strtab_init(&st, a);
    char buf[32];
    for (int i = 0; i < 500; i++) {
        int len = snprintf(buf, sizeof(buf), "str_%d", i);
        int idx = strtab_intern(&st, buf, len);
        ASSERT_EQ(idx, i);
    }
    /* verify all retrievable */
    for (int i = 0; i < 500; i++) {
        int len = snprintf(buf, sizeof(buf), "str_%d", i);
        ASSERT_STR_EQ(strtab_get(&st, i), buf);
        (void)len;
    }
    strtab_free(&st); arena_destroy(a);
}

TEST(strtab_empty_string) {
    Arena *a = arena_create(0);
    StringTable st; strtab_init(&st, a);
    int idx = strtab_intern(&st, "", 0);
    ASSERT_STR_EQ(strtab_get(&st, idx), "");
    ASSERT_EQ(strtab_len(&st, idx), 0);
    strtab_free(&st); arena_destroy(a);
}

TEST(strtab_null_get) {
    Arena *a = arena_create(0);
    StringTable st; strtab_init(&st, a);
    ASSERT(strtab_get(&st, -1) == NULL);
    ASSERT(strtab_get(&st, 999) == NULL);
    strtab_free(&st); arena_destroy(a);
}

int main(void) {
    printf("=== StringTable Tests ===\n");
    RUN_TEST(strtab_basic_intern);
    RUN_TEST(strtab_dedup);
    RUN_TEST(strtab_different_strings);
    RUN_TEST(strtab_many_strings);
    RUN_TEST(strtab_empty_string);
    RUN_TEST(strtab_null_get);
    TEST_SUMMARY();
}
