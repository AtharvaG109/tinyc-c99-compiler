/*
 * regalloc.c — Trivial stack-slot allocation
 *
 * Frame layout (addresses decrease upward in the diagram):
 *   rbp+0  : saved rbp
 *   rbp-8  : temp 1 value  (8 bytes)
 *   rbp-16 : temp 2 value
 *   ...
 *   rbp - 8*(n_temps-1) : temp (n_temps-1) value
 *   --- below: alloca storage regions (packed, aligned per request) ---
 *
 * alloca_off[t] is the rbp-relative offset of the allocated storage for
 * an IR_ALLOCA instruction whose dst temp is t (negative, e.g. -136).
 */
#include "tinyc/regalloc.h"
#include <string.h>

void regalloc_run(const IRFunc *f, RegAlloc *ra, Arena *arena) {
    int n = f->next_temp;   /* temps 0..n-1; temp 0 unused */

    int *slot      = (int *)arena_alloc(arena, sizeof(int) * (n + 1), _Alignof(int));
    int *alloca_off = (int *)arena_alloc(arena, sizeof(int) * (n + 1), _Alignof(int));
    memset(slot,      0, sizeof(int) * (n + 1));
    memset(alloca_off, 0, sizeof(int) * (n + 1));

    /* Phase 1: assign 8-byte value slots for all temps */
    for (int t = 1; t < n; t++)
        slot[t] = -8 * t;

    int temp_area = 8 * n;  /* total bytes used by temp slots */

    /* Phase 2: assign alloca storage regions below the temp area */
    int alloca_cur = temp_area;  /* growing downward from rbp */

    for (int i = 0; i < f->ninstr; i++) {
        const IRInstr *ins = &f->instrs[i];
        if (ins->op == IR_ALLOCA && ins->dst.kind == IRV_TEMP) {
            int tid = ins->dst.temp_id;
            int sz  = ins->alloca_size  > 0 ? ins->alloca_size  : 8;
            int al  = ins->alloca_align > 0 ? ins->alloca_align : 8;
            /* align alloca_cur to al */
            alloca_cur = (alloca_cur + al - 1) & ~(al - 1);
            alloca_cur += sz;
            alloca_off[tid] = -alloca_cur;
        }
    }

    /* Total frame size, aligned to 16 bytes */
    int frame_raw  = alloca_cur;
    int frame_size = (frame_raw + 15) & ~15;
    /* Ensure at least 16 bytes so we can always emit 'sub $N, %rsp' */
    if (frame_size == 0) frame_size = 16;

    ra->slot       = slot;
    ra->alloca_off  = alloca_off;
    ra->n_temps    = n;
    ra->frame_size = frame_size;
}
