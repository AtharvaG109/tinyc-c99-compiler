/*
 * codegen.h — x86-64 AT&T-syntax assembly emission
 */
#ifndef TINYC_CODEGEN_H
#define TINYC_CODEGEN_H

#include "tinyc/ir.h"
#include <stdio.h>

/* Emit x86-64 AT&T-syntax assembly for the entire module to `out`. */
void codegen(const IRModule *m, FILE *out);

#endif /* TINYC_CODEGEN_H */
