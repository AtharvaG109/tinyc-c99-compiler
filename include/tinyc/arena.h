/*
 * arena.h — Bump-pointer arena allocator
 *
 * All AST nodes, types, and interned strings are allocated from arenas.
 * No per-object free — the entire arena is freed at once after a
 * translation unit is compiled.
 */
#ifndef TINYC_ARENA_H
#define TINYC_ARENA_H

#include <stddef.h>

#define ARENA_DEFAULT_CAP (64 * 1024) /* 64 KB pages */

typedef struct Arena {
    char *base;
    size_t used;
    size_t cap;
    struct Arena *next; /* chain of overflow pages */
} Arena;

/* Create a new arena with the given initial capacity (0 = default 64KB). */
Arena *arena_create(size_t initial_cap);

/* Allocate `size` bytes aligned to `align` from the arena.
 * Grows to a new page if the current page is full. */
void *arena_alloc(Arena *a, size_t size, size_t align);

/* Convenience: allocate and copy `len` bytes from `s`, NUL-terminate. */
char *arena_strdup(Arena *a, const char *s, size_t len);

/* Free all pages in the arena chain (including the arena struct itself). */
void arena_destroy(Arena *a);

/* Reset the arena to empty without freeing memory (for reuse). */
void arena_reset(Arena *a);

#endif /* TINYC_ARENA_H */
