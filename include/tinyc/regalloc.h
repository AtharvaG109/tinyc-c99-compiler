/*
 * regalloc.h — Stack-slot register allocation
 *
 * Every virtual register (temp) gets an 8-byte slot on the stack.
 * IR_ALLOCA regions are packed above the temp slots.
 */
#ifndef TINYC_REGALLOC_H
#define TINYC_REGALLOC_H

#include "tinyc/ir.h"
#include "tinyc/arena.h"

typedef struct {
    int *slot;          /* slot[temp_id] = signed offset from %rbp (e.g. -8, -16, ...) */
    int *alloca_off;    /* alloca_off[temp_id] = signed offset from %rbp for alloca storage */
    int  n_temps;
    int  frame_size;    /* total frame bytes, already aligned to 16 */
} RegAlloc;

void regalloc_run(const IRFunc *f, RegAlloc *ra, Arena *arena);

#endif /* TINYC_REGALLOC_H */
