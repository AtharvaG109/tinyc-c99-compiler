/*
 * sema.h — Semantic analysis and type-checking pass
 *
 * Walks the AST produced by the parser, fills every node's `type` field,
 * inserts implicit AST_CAST nodes where C requires conversions, validates
 * break/continue/goto targets, and checks function return types.
 */
#ifndef TINYC_SEMA_H
#define TINYC_SEMA_H

#include "tinyc/ast.h"
#include "tinyc/type.h"
#include "tinyc/arena.h"
#include "tinyc/strtab.h"

/* ── Symbol table (same layout as parser's, separate instance) ───── */

typedef enum {
    SSYM_VAR,
    SSYM_FUNC,
    SSYM_TYPEDEF,
    SSYM_ENUM_CONST,
} SSym_kind;

typedef struct SSymbol {
    const char    *name;
    SSym_kind      kind;
    Type          *type;
    int            offset;    /* stack offset from rbp (locals), 0 = global */
    int            is_global;
    long long      enum_val;
    struct SSymbol *next;
} SSymbol;

#define SSCOPE_BUCKETS 64

typedef struct SScope {
    SSymbol       *buckets[SSCOPE_BUCKETS];
    struct SScope *parent;
} SScope;

/* ── Sema context ────────────────────────────────────────────────── */

typedef struct {
    Arena       *arena;
    StringTable *strings;
    TypeTable   *types;
    SScope      *scope;
    Type        *cur_func_ret;  /* return type of function being checked */
    ASTNode     *cur_func_body; /* current function body for label lookup */
    int          n_errors;
    int          loop_depth;
    int          switch_depth;
} Sema;

/* ── Public API ──────────────────────────────────────────────────── */

void sema_init(Sema *s, Arena *arena, StringTable *strings, TypeTable *types);

/* Analyse a translation unit in-place (mutates node->type fields).
 * Returns the number of errors. */
int sema_check(Sema *s, ASTNode *root);

#endif /* TINYC_SEMA_H */
