/*
 * lexer.h — C99 lexer interface
 *
 * Scans preprocessed C99 source into a stream of tokens. Identifiers
 * and string literals are interned into a shared StringTable. All
 * source positions reference the original source buffer — zero-copy.
 */
#ifndef TINYC_LEXER_H
#define TINYC_LEXER_H

#include "tinyc/token.h"
#include "tinyc/strtab.h"
#include "tinyc/arena.h"

typedef struct {
    const char *src;       /* full source buffer (not owned) */
    const char *cur;       /* current scan position */
    const char *end;       /* one past end of source */
    int line;              /* current line (1-indexed) */
    int col;               /* current column (1-indexed) */
    StringTable *strings;  /* shared string table for interning */
    Arena *arena;          /* arena for temporary allocations */
    const char *filename;  /* source filename for diagnostics */
    /* Lookahead buffer: if has_peek is true, peeked contains the peeked token */
    int has_peek;
    Token peeked;
} Lexer;

/* Initialize the lexer to scan `src` of `len` bytes. */
void lexer_init(Lexer *l, const char *src, size_t len,
                StringTable *strings, Arena *arena, const char *filename);

/* Advance and return the next token. Returns TOK_EOF at end of input. */
Token lexer_next(Lexer *l);

/* Peek at the next token without consuming it. */
Token lexer_peek(Lexer *l);

#endif /* TINYC_LEXER_H */
