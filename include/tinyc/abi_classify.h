#ifndef TINYC_ABI_CLASSIFY_H
#define TINYC_ABI_CLASSIFY_H

#include "tinyc/type.h"
#include <stdbool.h>

typedef enum {
    ABI_CLASS_NONE,
    ABI_CLASS_MEMORY,
    ABI_CLASS_INTEGER,
    ABI_CLASS_SSE,
    ABI_CLASS_SSEUP,
    ABI_CLASS_X87,
    ABI_CLASS_X87UP,
} ABIClass;

/* This backend supports the SysV AMD64 two-eightbyte aggregate register path. */
#define MAX_EIGHTBYTES 2

typedef struct {
    int       n_eightbytes;            /* how many 8-byte chunks */
    ABIClass  classes[MAX_EIGHTBYTES]; /* class of each chunk */
} ABIClassification;

/* Classify a type for argument/return passing */
ABIClassification abi_classify(const Type *ty);

/* High-level: should this type be passed in registers or memory? */
bool abi_in_memory(const ABIClassification *c);

#endif /* TINYC_ABI_CLASSIFY_H */
