/*
 * token.h — Token types and Token struct for the C99 lexer
 *
 * Every C99 keyword, operator, and punctuator has its own TokenKind.
 * The Token struct uses start/len pointers into the source buffer —
 * zero allocation during lexing.
 */
#ifndef TINYC_TOKEN_H
#define TINYC_TOKEN_H

#include <stdint.h>

typedef enum {
    /* ── Literals ─────────────────────────────────────────────── */
    TOK_INT_LIT,        /* 42, 0xFF, 077, 42ULL */
    TOK_FLOAT_LIT,      /* 3.14, 1e10, 0x1.8p+1 */
    TOK_CHAR_LIT,       /* 'a', '\n', L'x' */
    TOK_STR_LIT,        /* "hello", L"wide" */

    /* ── Identifier ───────────────────────────────────────────── */
    TOK_IDENT,          /* variable/function/type names */

    /* ── C99 Keywords (33 total) ──────────────────────────────── */
    TOK_AUTO,
    TOK_BREAK,
    TOK_CASE,
    TOK_CHAR,
    TOK_CONST,
    TOK_CONTINUE,
    TOK_DEFAULT,
    TOK_DO,
    TOK_DOUBLE,
    TOK_ELSE,
    TOK_ENUM,
    TOK_EXTERN,
    TOK_FLOAT,
    TOK_FOR,
    TOK_GOTO,
    TOK_IF,
    TOK_INLINE,
    TOK_INT,
    TOK_LONG,
    TOK_REGISTER,
    TOK_RESTRICT,       /* parsed, semantics ignored */
    TOK_RETURN,
    TOK_SHORT,
    TOK_SIGNED,
    TOK_SIZEOF,
    TOK_STATIC,
    TOK_STRUCT,
    TOK_SWITCH,
    TOK_TYPEDEF,
    TOK_UNION,
    TOK_UNSIGNED,
    TOK_VOID,
    TOK_VOLATILE,
    TOK_WHILE,
    TOK_BOOL,           /* _Bool */
    TOK_COMPLEX,        /* _Complex */
    TOK_IMAGINARY,      /* _Imaginary */
    TOK_PRAGMA,         /* _Pragma */
    TOK_ALIGNOF,        /* _Alignof */

    /* ── Extensions ───────────────────────────────────────────── */
    TOK_ATTRIBUTE,
    TOK_ASM,
    TOK_BUILTIN_VA_LIST,
    TOK_EXTENSION,
    TOK_NONNULL,
    TOK_NORETURN,
    TOK_NULLABLE,
    TOK_TYPEOF,

    /* ── Arithmetic Operators ─────────────────────────────────── */
    TOK_PLUS,           /* + */
    TOK_MINUS,          /* - */
    TOK_STAR,           /* * */
    TOK_SLASH,          /* / */
    TOK_PERCENT,        /* % */

    /* ── Bitwise Operators ────────────────────────────────────── */
    TOK_AMP,            /* & */
    TOK_PIPE,           /* | */
    TOK_CARET,          /* ^ */
    TOK_TILDE,          /* ~ */

    /* ── Logical Operators ────────────────────────────────────── */
    TOK_BANG,           /* ! */
    TOK_AND,            /* && */
    TOK_OR,             /* || */

    /* ── Comparison Operators ─────────────────────────────────── */
    TOK_LT,             /* < */
    TOK_GT,             /* > */
    TOK_LE,             /* <= */
    TOK_GE,             /* >= */
    TOK_EQ,             /* == */
    TOK_NEQ,            /* != */

    /* ── Shift Operators ──────────────────────────────────────── */
    TOK_LSHIFT,         /* << */
    TOK_RSHIFT,         /* >> */

    /* ── Assignment Operators ─────────────────────────────────── */
    TOK_ASSIGN,         /* = */
    TOK_PLUS_ASSIGN,    /* += */
    TOK_MINUS_ASSIGN,   /* -= */
    TOK_STAR_ASSIGN,    /* *= */
    TOK_SLASH_ASSIGN,   /* /= */
    TOK_PERCENT_ASSIGN, /* %= */
    TOK_AMP_ASSIGN,     /* &= */
    TOK_PIPE_ASSIGN,    /* |= */
    TOK_CARET_ASSIGN,   /* ^= */
    TOK_LSHIFT_ASSIGN,  /* <<= */
    TOK_RSHIFT_ASSIGN,  /* >>= */

    /* ── Increment / Decrement ────────────────────────────────── */
    TOK_PLUSPLUS,        /* ++ */
    TOK_MINUSMINUS,     /* -- */

    /* ── Member Access ────────────────────────────────────────── */
    TOK_ARROW,          /* -> */
    TOK_DOT,            /* . */

    /* ── Ternary ──────────────────────────────────────────────── */
    TOK_QUESTION,       /* ? */
    TOK_COLON,          /* : */

    /* ── Delimiters ───────────────────────────────────────────── */
    TOK_LPAREN,         /* ( */
    TOK_RPAREN,         /* ) */
    TOK_LBRACE,         /* { */
    TOK_RBRACE,         /* } */
    TOK_LBRACKET,       /* [ */
    TOK_RBRACKET,       /* ] */

    /* ── Punctuation ──────────────────────────────────────────── */
    TOK_SEMICOLON,      /* ; */
    TOK_COMMA,          /* , */
    TOK_ELLIPSIS,       /* ... */
    TOK_HASH,           /* # (for preprocessor leftovers, e.g. # line directives) */

    /* ── Meta ─────────────────────────────────────────────────── */
    TOK_EOF,
    TOK_ERROR,

    TOK_COUNT           /* total number of token kinds */
} TokenKind;

/*
 * Integer literal suffix flags — track what the user wrote.
 * Combined via bitwise OR: e.g., 42ULL → INT_SUFFIX_U | INT_SUFFIX_LL
 */
typedef enum {
    INT_SUFFIX_NONE = 0,
    INT_SUFFIX_U    = 1,  /* unsigned */
    INT_SUFFIX_L    = 2,  /* long */
    INT_SUFFIX_LL   = 4,  /* long long */
} IntSuffix;

typedef struct {
    TokenKind kind;
    const char *start;     /* pointer into source buffer — NOT owned */
    int len;               /* length of raw token text */
    int line;              /* 1-indexed line number */
    int col;               /* 1-indexed column number */
    /* Parsed value — which field is valid depends on `kind` */
    unsigned long long int_val;  /* TOK_INT_LIT, TOK_CHAR_LIT */
    double float_val;            /* TOK_FLOAT_LIT */
    int int_suffix;              /* IntSuffix flags (TOK_INT_LIT) */
    int float_suffix;            /* 0=double, 'f'=float, 'l'=long double */
    int str_idx;                 /* index into StringTable (TOK_IDENT, TOK_STR_LIT) */
    int is_wide;                 /* 1 if L"..." or L'...' prefix */
} Token;

/* Return a human-readable name for a token kind (e.g., "TOK_INT" or "'+'"). */
const char *token_kind_name(TokenKind kind);

/* Return the keyword string for a keyword token (e.g., "while" for TOK_WHILE).
 * Returns NULL for non-keyword tokens. */
const char *token_keyword_str(TokenKind kind);

/* Return a short string representation of the token for debugging.
 * Uses a static buffer — not reentrant. */
const char *token_describe(const Token *tok);

#endif /* TINYC_TOKEN_H */
