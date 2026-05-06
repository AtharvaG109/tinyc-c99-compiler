/*
 * strtab.c — String interning table implementation
 *
 * Uses FNV-1a hashing with open addressing (linear probing).
 * Strings are stored in the arena — the hash table itself uses malloc
 * for the index arrays but those are small.
 */
#include "tinyc/strtab.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define STRTAB_INITIAL_CAP 256
#define STRTAB_LOAD_FACTOR 0.7
#define STRTAB_EMPTY (-1)

/* FNV-1a hash */
static uint32_t fnv1a(const char *s, int len) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < len; i++) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

/*
 * The hash table uses a separate "slot → entry index" mapping.
 * slots[i] == STRTAB_EMPTY means the slot is free.
 * slots[i] >= 0 means the slot holds entry index slots[i].
 */

void strtab_init(StringTable *st, Arena *arena) {
    st->arena = arena;
    st->count = 0;
    st->cap = STRTAB_INITIAL_CAP;
    st->entries = (char **)calloc((size_t)st->cap, sizeof(char *));
    st->lengths = (int *)calloc((size_t)st->cap, sizeof(int));
    st->hashes = (uint32_t *)calloc((size_t)st->cap, sizeof(uint32_t));
    if (!st->entries || !st->lengths || !st->hashes) {
        fprintf(stderr, "fatal: strtab_init: out of memory\n");
        exit(1);
    }
}

static int find_slot(const StringTable *st, const char *s, int len, uint32_t h) {
    /*
     * Simple linear search through existing entries.
     * For the number of strings in a typical translation unit (< 10K),
     * this is fast enough. We optimize with the cached hash to skip
     * most strcmp calls.
     */
    for (int i = 0; i < st->count; i++) {
        if (st->hashes[i] == h && st->lengths[i] == len &&
            memcmp(st->entries[i], s, (size_t)len) == 0) {
            return i;
        }
    }
    return -1;
}

static void grow_if_needed(StringTable *st) {
    if (st->count < st->cap) {
        return;
    }
    int new_cap = st->cap * 2;
    st->entries = (char **)realloc(st->entries, (size_t)new_cap * sizeof(char *));
    st->lengths = (int *)realloc(st->lengths, (size_t)new_cap * sizeof(int));
    st->hashes = (uint32_t *)realloc(st->hashes, (size_t)new_cap * sizeof(uint32_t));
    if (!st->entries || !st->lengths || !st->hashes) {
        fprintf(stderr, "fatal: strtab grow: out of memory\n");
        exit(1);
    }
    st->cap = new_cap;
}

int strtab_intern(StringTable *st, const char *s, int len) {
    uint32_t h = fnv1a(s, len);

    /* Check if already interned */
    int existing = find_slot(st, s, len, h);
    if (existing >= 0) {
        return existing;
    }

    /* New entry */
    grow_if_needed(st);
    int idx = st->count;
    st->entries[idx] = arena_strdup(st->arena, s, (size_t)len);
    st->lengths[idx] = len;
    st->hashes[idx] = h;
    st->count++;
    return idx;
}

const char *strtab_get(const StringTable *st, int idx) {
    if (idx < 0 || idx >= st->count) {
        return NULL;
    }
    return st->entries[idx];
}

int strtab_len(const StringTable *st, int idx) {
    if (idx < 0 || idx >= st->count) {
        return 0;
    }
    return st->lengths[idx];
}

void strtab_free(StringTable *st) {
    free(st->entries);
    free(st->lengths);
    free(st->hashes);
    st->entries = NULL;
    st->lengths = NULL;
    st->hashes = NULL;
    st->count = 0;
    st->cap = 0;
}
