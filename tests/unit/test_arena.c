/*
 * test_arena.c — Arena allocator unit tests
 */
#include "test_harness.h"
#include "tinyc/arena.h"
#include <string.h>

TEST(arena_create_default) {
    Arena *a = arena_create(0);
    ASSERT(a != NULL);
    ASSERT(a->cap == ARENA_DEFAULT_CAP);
    ASSERT(a->used == 0);
    arena_destroy(a);
}

TEST(arena_basic_alloc) {
    Arena *a = arena_create(0);
    int *p = (int *)arena_alloc(a, sizeof(int), sizeof(int));
    ASSERT(p != NULL);
    *p = 42;
    ASSERT(*p == 42);
    arena_destroy(a);
}

TEST(arena_multiple_allocs) {
    Arena *a = arena_create(0);
    for (int i = 0; i < 1000; i++) {
        int *p = (int *)arena_alloc(a, sizeof(int), sizeof(int));
        ASSERT(p != NULL);
        *p = i;
    }
    arena_destroy(a);
}

TEST(arena_overflow_to_new_page) {
    Arena *a = arena_create(64); /* tiny page */
    char *p1 = (char *)arena_alloc(a, 32, 1);
    char *p2 = (char *)arena_alloc(a, 32, 1);
    char *p3 = (char *)arena_alloc(a, 32, 1); /* should overflow */
    ASSERT(p1 != NULL);
    ASSERT(p2 != NULL);
    ASSERT(p3 != NULL);
    ASSERT(a->next != NULL); /* second page allocated */
    arena_destroy(a);
}

TEST(arena_strdup_works) {
    Arena *a = arena_create(0);
    const char *s = "hello world";
    char *dup = arena_strdup(a, s, strlen(s));
    ASSERT(dup != NULL);
    ASSERT(strcmp(dup, "hello world") == 0);
    ASSERT(dup != s); /* different pointer */
    arena_destroy(a);
}

TEST(arena_reset) {
    Arena *a = arena_create(256);
    arena_alloc(a, 100, 1);
    arena_alloc(a, 100, 1);
    arena_alloc(a, 100, 1); /* overflow */
    ASSERT(a->next != NULL);
    arena_reset(a);
    ASSERT(a->used == 0);
    ASSERT(a->next == NULL);
    arena_destroy(a);
}

TEST(arena_alignment) {
    Arena *a = arena_create(0);
    arena_alloc(a, 1, 1); /* 1 byte, align 1 */
    void *p = arena_alloc(a, 8, 8); /* 8 bytes, align 8 */
    ASSERT(((size_t)p % 8) == 0);
    arena_alloc(a, 3, 1);
    void *p2 = arena_alloc(a, 16, 16);
    ASSERT(((size_t)p2 % 16) == 0);
    arena_destroy(a);
}

int main(void) {
    printf("=== Arena Tests ===\n");
    RUN_TEST(arena_create_default);
    RUN_TEST(arena_basic_alloc);
    RUN_TEST(arena_multiple_allocs);
    RUN_TEST(arena_overflow_to_new_page);
    RUN_TEST(arena_strdup_works);
    RUN_TEST(arena_reset);
    RUN_TEST(arena_alignment);
    TEST_SUMMARY();
}
