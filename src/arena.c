/*
 * arena.c — Bump-pointer arena allocator implementation
 */
#include "tinyc/arena.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static size_t align_up(size_t n, size_t align) {
    return (n + align - 1) & ~(align - 1);
}

Arena *arena_create(size_t initial_cap) {
    if (initial_cap == 0) {
        initial_cap = ARENA_DEFAULT_CAP;
    }
    Arena *a = (Arena *)malloc(sizeof(Arena) + initial_cap);
    if (!a) {
        fprintf(stderr, "fatal: arena_create: out of memory\n");
        exit(1);
    }
    a->base = (char *)(a + 1); /* memory starts right after the Arena struct */
    a->used = 0;
    a->cap = initial_cap;
    a->next = NULL;
    return a;
}

void *arena_alloc(Arena *a, size_t size, size_t align) {
    /* Walk to the last page in the chain */
    Arena *cur = a;
    while (cur->next) {
        cur = cur->next;
    }

    size_t offset = align_up(cur->used, align);
    if (offset > SIZE_MAX - size) return NULL;
    if (offset + size <= cur->cap) {
        cur->used = offset + size;
        return cur->base + offset;
    }

    /* Current page is full — allocate a new one.
     * New page is at least as big as the default, or big enough for this
     * allocation (whichever is larger). */
    size_t new_cap = ARENA_DEFAULT_CAP;
    if (size > SIZE_MAX - align) return NULL;
    if (size + align > new_cap) {
        new_cap = size + align;
    }

    Arena *page = (Arena *)malloc(sizeof(Arena) + new_cap);
    if (!page) {
        fprintf(stderr, "fatal: arena_alloc: out of memory\n");
        exit(1);
    }
    page->base = (char *)(page + 1);
    page->used = 0;
    page->cap = new_cap;
    page->next = NULL;
    cur->next = page;

    offset = align_up(page->used, align);
    page->used = offset + size;
    return page->base + offset;
}

char *arena_strdup(Arena *a, const char *s, size_t len) {
    char *dup = (char *)arena_alloc(a, len + 1, 1);
    memcpy(dup, s, len);
    dup[len] = '\0';
    return dup;
}

void arena_destroy(Arena *a) {
    Arena *cur = a;
    while (cur) {
        Arena *next = cur->next;
        free(cur);
        cur = next;
    }
}

void arena_reset(Arena *a) {
    /* Free all overflow pages, keep the first one */
    Arena *page = a->next;
    while (page) {
        Arena *next = page->next;
        free(page);
        page = next;
    }
    a->next = NULL;
    a->used = 0;
}
