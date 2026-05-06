/*
 * irgen.h — AST → TAC IR generation
 */
#ifndef TINYC_IRGEN_H
#define TINYC_IRGEN_H

#include "tinyc/ast.h"
#include "tinyc/ir.h"
#include "tinyc/type.h"
#include "tinyc/strtab.h"

/* Generate IR for a complete translation unit. */
IRModule *irgen(ASTNode *root, Arena *arena, StringTable *strings, TypeTable *types);

#endif /* TINYC_IRGEN_H */
