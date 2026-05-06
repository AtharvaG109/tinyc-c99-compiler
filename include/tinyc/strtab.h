/*
 * strtab.h — String interning table
 *
 * Deduplicates identifier and string literal storage. Each unique string
 * is stored once in an arena and referenced by integer index. This makes
 * string comparison O(1) (compare indices) after interning.
 */
#ifndef TINYC_STRTAB_H
#define TINYC_STRTAB_H

#include "tinyc/arena.h"
#include <stdint.h>

typedef struct {
    Arena *arena;
    /* Parallel arrays — one slot per interned string */
    char **entries;     /* pointers to NUL-terminated strings in arena */
    int *lengths;       /* length of each string (excluding NUL) */
    uint32_t *hashes;   /* cached FNV-1a hash of each string */
    int count;          /* number of interned strings */
    int cap;            /* allocated capacity of the arrays */
} StringTable;

/* Initialize a string table using the given arena for string storage. */
void strtab_init(StringTable *st, Arena *arena);

/* Intern a string of length `len`. Returns an integer index.
 * If the string was already interned, returns the existing index. */
int strtab_intern(StringTable *st, const char *s, int len);

/* Retrieve a previously interned string by index. */
const char *strtab_get(const StringTable *st, int idx);

/* Get the length of a previously interned string. */
int strtab_len(const StringTable *st, int idx);

/* Free the hash table (not the arena — that's freed separately). */
void strtab_free(StringTable *st);

#endif /* TINYC_STRTAB_H */
