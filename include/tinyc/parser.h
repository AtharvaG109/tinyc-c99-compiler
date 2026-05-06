/*
 * parser.h — C99 recursive-descent parser with Pratt expression parsing
 *
 * Consumes a token stream from the Lexer and produces an AST rooted at
 * AST_TRANSLATION_UNIT. Scope management is embedded for typedef
 * disambiguation (the "typedef name" ambiguity requires a live symbol table
 * during parsing).
 */
#ifndef TINYC_PARSER_H
#define TINYC_PARSER_H

#include "tinyc/lexer.h"
#include "tinyc/arena.h"
#include "tinyc/strtab.h"
#include "tinyc/ast.h"
#include "tinyc/type.h"

/* ── Symbol table ─────────────────────────────────────────────────── */

typedef enum {
    SYM_VAR,
    SYM_FUNC,
    SYM_TYPEDEF,
    SYM_ENUM_CONST,
    SYM_STRUCT_TAG,
    SYM_UNION_TAG,
    SYM_ENUM_TAG,
} SymKind;

typedef struct Symbol {
    const char    *name;   /* interned */
    SymKind        kind;
    Type          *type;
    ASTNode       *decl;   /* declaration node (for diagnostics) */
    struct Symbol *next;   /* hash chain */
} Symbol;

#define SCOPE_BUCKETS 64

typedef struct Scope {
    Symbol       *buckets[SCOPE_BUCKETS];
    struct Scope *parent;
} Scope;

/* ── Parser state ─────────────────────────────────────────────────── */

typedef struct {
    Lexer       *lexer;
    Arena       *arena;
    StringTable *strings;
    TypeTable   *types;
    Scope       *scope;       /* ordinary-identifier scope stack */
    Scope       *tag_scope;   /* tag (struct/union/enum) scope stack */
    int          n_errors;
    /* Loop/switch nesting depth (for break/continue validation) */
    int          loop_depth;
    int          switch_depth;
} Parser;

/* ── Public API ───────────────────────────────────────────────────── */

/* Initialize the parser. Pointers must remain valid for the parse lifetime. */
void parser_init(Parser *p, Lexer *lexer, Arena *arena,
                 StringTable *strings, TypeTable *types);

/* Parse a complete translation unit. Returns NULL on fatal parse error. */
ASTNode *parser_parse(Parser *p);

/* Number of non-fatal errors encountered. */
int parser_error_count(const Parser *p);

#endif /* TINYC_PARSER_H */
