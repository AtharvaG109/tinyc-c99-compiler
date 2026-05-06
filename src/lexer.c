/*
 * lexer.c — C99 lexer implementation
 */
#include "tinyc/lexer.h"
#include "tinyc/diagnostic.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* ── Keyword table (sorted for binary search) ───────────────────── */
typedef struct { const char *name; TokenKind kind; } KW;
#define NUM_KW 59

static TokenKind lookup_keyword(const char *s, int len) {
    const KW kw_table[NUM_KW] = {
    {"_Alignof",TOK_ALIGNOF},{"_Bool",TOK_BOOL},{"_Complex",TOK_COMPLEX},
    {"_Imaginary",TOK_IMAGINARY},{"_Nonnull",TOK_NONNULL},
    {"_Noreturn",TOK_NORETURN},{"_Nullable",TOK_NULLABLE},
    {"_Pragma",TOK_PRAGMA},{"__asm",TOK_ASM},
    {"__asm__",TOK_ASM},{"__attribute__",TOK_ATTRIBUTE},
    {"__builtin_va_list",TOK_BUILTIN_VA_LIST},{"__const",TOK_CONST},
    {"__const__",TOK_CONST},{"__extension__",TOK_EXTENSION},
    {"__inline",TOK_INLINE},{"__inline__",TOK_INLINE},
    {"__restrict",TOK_RESTRICT},{"__restrict__",TOK_RESTRICT},
    {"__signed",TOK_SIGNED},{"__signed__",TOK_SIGNED},
    {"__typeof__",TOK_TYPEOF},{"__volatile",TOK_VOLATILE},
    {"__volatile__",TOK_VOLATILE},{"asm",TOK_ASM},
    {"auto",TOK_AUTO},{"break",TOK_BREAK},
    {"case",TOK_CASE},{"char",TOK_CHAR},{"const",TOK_CONST},
    {"continue",TOK_CONTINUE},{"default",TOK_DEFAULT},{"do",TOK_DO},
    {"double",TOK_DOUBLE},{"else",TOK_ELSE},{"enum",TOK_ENUM},
    {"extern",TOK_EXTERN},{"float",TOK_FLOAT},{"for",TOK_FOR},
    {"goto",TOK_GOTO},{"if",TOK_IF},{"inline",TOK_INLINE},
    {"int",TOK_INT},{"long",TOK_LONG},{"register",TOK_REGISTER},
    {"restrict",TOK_RESTRICT},{"return",TOK_RETURN},{"short",TOK_SHORT},
    {"signed",TOK_SIGNED},{"sizeof",TOK_SIZEOF},{"static",TOK_STATIC},
    {"struct",TOK_STRUCT},{"switch",TOK_SWITCH},{"typedef",TOK_TYPEDEF},
    {"union",TOK_UNION},{"unsigned",TOK_UNSIGNED},{"void",TOK_VOID},
    {"volatile",TOK_VOLATILE},{"while",TOK_WHILE},
    };
    int lo = 0, hi = (int)NUM_KW - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = strncmp(s, kw_table[mid].name, (size_t)len);
        if (cmp == 0) {
            if (kw_table[mid].name[len] == '\0') return kw_table[mid].kind;
            cmp = -1;
        }
        if (cmp < 0) hi = mid - 1; else lo = mid + 1;
    }
    return TOK_IDENT;
}

/* ── token_kind_name / token_describe ────────────────────────────── */
static const char *tok_names[TOK_COUNT];
static int tok_names_init = 0;

static void init_tok_names(void) {
    if (tok_names_init) return;
    tok_names_init = 1;
    tok_names[TOK_INT_LIT]="INT_LIT"; tok_names[TOK_FLOAT_LIT]="FLOAT_LIT";
    tok_names[TOK_CHAR_LIT]="CHAR_LIT"; tok_names[TOK_STR_LIT]="STR_LIT";
    tok_names[TOK_IDENT]="IDENT";
    tok_names[TOK_AUTO]="auto"; tok_names[TOK_BREAK]="break";
    tok_names[TOK_CASE]="case"; tok_names[TOK_CHAR]="char";
    tok_names[TOK_CONST]="const"; tok_names[TOK_CONTINUE]="continue";
    tok_names[TOK_DEFAULT]="default"; tok_names[TOK_DO]="do";
    tok_names[TOK_DOUBLE]="double"; tok_names[TOK_ELSE]="else";
    tok_names[TOK_ENUM]="enum"; tok_names[TOK_EXTERN]="extern";
    tok_names[TOK_FLOAT]="float"; tok_names[TOK_FOR]="for";
    tok_names[TOK_GOTO]="goto"; tok_names[TOK_IF]="if";
    tok_names[TOK_INLINE]="inline"; tok_names[TOK_INT]="int";
    tok_names[TOK_LONG]="long"; tok_names[TOK_REGISTER]="register";
    tok_names[TOK_RESTRICT]="restrict"; tok_names[TOK_RETURN]="return";
    tok_names[TOK_SHORT]="short"; tok_names[TOK_SIGNED]="signed";
    tok_names[TOK_SIZEOF]="sizeof"; tok_names[TOK_STATIC]="static";
    tok_names[TOK_STRUCT]="struct"; tok_names[TOK_SWITCH]="switch";
    tok_names[TOK_TYPEDEF]="typedef"; tok_names[TOK_UNION]="union";
    tok_names[TOK_UNSIGNED]="unsigned"; tok_names[TOK_VOID]="void";
    tok_names[TOK_VOLATILE]="volatile"; tok_names[TOK_WHILE]="while";
    tok_names[TOK_BOOL]="_Bool"; tok_names[TOK_COMPLEX]="_Complex";
    tok_names[TOK_IMAGINARY]="_Imaginary"; tok_names[TOK_PRAGMA]="_Pragma";
    tok_names[TOK_ATTRIBUTE]="__attribute__"; tok_names[TOK_ASM]="__asm__";
    tok_names[TOK_BUILTIN_VA_LIST]="__builtin_va_list";
    tok_names[TOK_EXTENSION]="__extension__"; tok_names[TOK_TYPEOF]="__typeof__";
    tok_names[TOK_NONNULL]="_Nonnull"; tok_names[TOK_NORETURN]="_Noreturn";
    tok_names[TOK_NULLABLE]="_Nullable";
    tok_names[TOK_PLUS]="+"; tok_names[TOK_MINUS]="-";
    tok_names[TOK_STAR]="*"; tok_names[TOK_SLASH]="/";
    tok_names[TOK_PERCENT]="%"; tok_names[TOK_AMP]="&";
    tok_names[TOK_PIPE]="|"; tok_names[TOK_CARET]="^";
    tok_names[TOK_TILDE]="~"; tok_names[TOK_BANG]="!";
    tok_names[TOK_AND]="&&"; tok_names[TOK_OR]="||";
    tok_names[TOK_LT]="<"; tok_names[TOK_GT]=">";
    tok_names[TOK_LE]="<="; tok_names[TOK_GE]=">=";
    tok_names[TOK_EQ]="=="; tok_names[TOK_NEQ]="!=";
    tok_names[TOK_LSHIFT]="<<"; tok_names[TOK_RSHIFT]=">>";
    tok_names[TOK_ASSIGN]="=";
    tok_names[TOK_PLUS_ASSIGN]="+="; tok_names[TOK_MINUS_ASSIGN]="-=";
    tok_names[TOK_STAR_ASSIGN]="*="; tok_names[TOK_SLASH_ASSIGN]="/=";
    tok_names[TOK_PERCENT_ASSIGN]="%="; tok_names[TOK_AMP_ASSIGN]="&=";
    tok_names[TOK_PIPE_ASSIGN]="|="; tok_names[TOK_CARET_ASSIGN]="^=";
    tok_names[TOK_LSHIFT_ASSIGN]="<<="; tok_names[TOK_RSHIFT_ASSIGN]=">>=";
    tok_names[TOK_PLUSPLUS]="++"; tok_names[TOK_MINUSMINUS]="--";
    tok_names[TOK_ARROW]="->"; tok_names[TOK_DOT]=".";
    tok_names[TOK_QUESTION]="?"; tok_names[TOK_COLON]=":";
    tok_names[TOK_LPAREN]="("; tok_names[TOK_RPAREN]=")";
    tok_names[TOK_LBRACE]="{"; tok_names[TOK_RBRACE]="}";
    tok_names[TOK_LBRACKET]="["; tok_names[TOK_RBRACKET]="]";
    tok_names[TOK_SEMICOLON]=";"; tok_names[TOK_COMMA]=",";
    tok_names[TOK_ELLIPSIS]="..."; tok_names[TOK_HASH]="#";
    tok_names[TOK_EOF]="EOF"; tok_names[TOK_ERROR]="ERROR";
}

const char *token_kind_name(TokenKind kind) {
    init_tok_names();
    if (kind >= 0 && kind < TOK_COUNT && tok_names[kind]) return tok_names[kind];
    return "UNKNOWN";
}

const char *token_keyword_str(TokenKind kind) {
    if (kind >= TOK_AUTO && kind <= TOK_PRAGMA) return token_kind_name(kind);
    return NULL;
}

static char desc_buf[256];
const char *token_describe(const Token *tok) {
    if (tok->kind == TOK_INT_LIT)
        snprintf(desc_buf, sizeof(desc_buf), "INT_LIT(%llu)", tok->int_val);
    else if (tok->kind == TOK_FLOAT_LIT)
        snprintf(desc_buf, sizeof(desc_buf), "FLOAT_LIT(%g)", tok->float_val);
    else if (tok->kind == TOK_IDENT || tok->kind == TOK_STR_LIT || tok->kind == TOK_CHAR_LIT)
        snprintf(desc_buf, sizeof(desc_buf), "%s('%.*s')",
                 token_kind_name(tok->kind), tok->len, tok->start);
    else
        snprintf(desc_buf, sizeof(desc_buf), "%s", token_kind_name(tok->kind));
    return desc_buf;
}

/* ── Lexer core ──────────────────────────────────────────────────── */

void lexer_init(Lexer *l, const char *src, size_t len,
                StringTable *strings, Arena *arena, const char *filename) {
    l->src = src; l->cur = src; l->end = src + len;
    l->line = 1; l->col = 1;
    l->strings = strings; l->arena = arena;
    l->filename = filename; l->has_peek = 0;
}

static int at_end(const Lexer *l) { return l->cur >= l->end; }
static char peekc(const Lexer *l) { return at_end(l) ? '\0' : *l->cur; }
static char peekc2(const Lexer *l) { return (l->cur+1 < l->end) ? l->cur[1] : '\0'; }

static char adv(Lexer *l) {
    if (at_end(l)) return '\0';
    char c = *l->cur++;
    if (c == '\n') { l->line++; l->col = 1; } else { l->col++; }
    return c;
}

static Token mktok(const Lexer *l, TokenKind k, const char *s, int ln, int co) {
    Token t; memset(&t, 0, sizeof(t));
    t.kind = k; t.start = s; t.len = (int)(l->cur - s);
    t.line = ln; t.col = co;
    return t;
}

static Token errtok(const Lexer *l, const char *s, int ln, int co, const char *msg) {
    diag_emit(DIAG_ERROR, ln, co, "%s", msg);
    return mktok(l, TOK_ERROR, s, ln, co);
}

/* ── Skip whitespace and comments ────────────────────────────────── */
static void skip_ws(Lexer *l) {
    for (;;) {
        if (at_end(l)) return;
        char c = peekc(l);
        if (c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f'||c=='\v') { adv(l); continue; }
        if (c=='/' && peekc2(l)=='/') {
            adv(l); adv(l);
            while (!at_end(l) && peekc(l)!='\n') adv(l);
            continue;
        }
        if (c=='/' && peekc2(l)=='*') {
            int sl=l->line, sc=l->col;
            adv(l); adv(l);
            while (!at_end(l)) {
                if (peekc(l)=='*' && peekc2(l)=='/') { adv(l); adv(l); goto cont; }
                adv(l);
            }
            diag_emit(DIAG_ERROR, sl, sc, "unterminated block comment");
            return;
            cont: continue;
        }
        if (c=='#') {
            adv(l);
            while (!at_end(l) && peekc(l)!='\n') adv(l);
            continue;
        }
        return;
    }
}

/* ── Hex digit check ─────────────────────────────────────────────── */
static int ishex(char c) {
    return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');
}

/* ── Scan number ─────────────────────────────────────────────────── */
static Token scan_num(Lexer *l, const char *s, int ln, int co) {
    int is_flt = 0, base = 10;
    if (peekc(l)=='0' && (peekc2(l)=='x'||peekc2(l)=='X')) {
        adv(l); adv(l); base = 16;
        while (!at_end(l) && ishex(peekc(l))) adv(l);
        if (peekc(l)=='.') { is_flt=1; adv(l); while (!at_end(l)&&ishex(peekc(l))) adv(l); }
        if (peekc(l)=='p'||peekc(l)=='P') {
            is_flt=1; adv(l);
            if (peekc(l)=='+'||peekc(l)=='-') adv(l);
            while (!at_end(l)&&isdigit((unsigned char)peekc(l))) adv(l);
        }
    } else if (peekc(l)=='0' && peekc2(l)>='0' && peekc2(l)<='7') {
        base = 8;
        while (!at_end(l) && peekc(l)>='0' && peekc(l)<='7') adv(l);
    } else {
        while (!at_end(l)&&isdigit((unsigned char)peekc(l))) adv(l);
        if (peekc(l)=='.') { is_flt=1; adv(l); while(!at_end(l)&&isdigit((unsigned char)peekc(l))) adv(l); }
        if (peekc(l)=='e'||peekc(l)=='E') {
            is_flt=1; adv(l);
            if (peekc(l)=='+'||peekc(l)=='-') adv(l);
            while (!at_end(l)&&isdigit((unsigned char)peekc(l))) adv(l);
        }
    }
    if (is_flt) {
        Token t = mktok(l, TOK_FLOAT_LIT, s, ln, co);
        t.float_val = strtod(s, NULL); t.float_suffix = 0;
        if (peekc(l)=='f'||peekc(l)=='F') { t.float_suffix='f'; adv(l); }
        else if (peekc(l)=='l'||peekc(l)=='L') { t.float_suffix='l'; adv(l); }
        t.len = (int)(l->cur - s);
        return t;
    }
    Token t = mktok(l, TOK_INT_LIT, s, ln, co);
    t.int_val = strtoull(s, NULL, base);
    t.int_suffix = INT_SUFFIX_NONE;
    for (;;) {
        char c = peekc(l);
        if (c=='u'||c=='U') { t.int_suffix |= INT_SUFFIX_U; adv(l); }
        else if ((c=='l'||c=='L') && (peekc2(l)=='l'||peekc2(l)=='L')) {
            t.int_suffix |= INT_SUFFIX_LL; adv(l); adv(l);
        } else if (c=='l'||c=='L') { t.int_suffix |= INT_SUFFIX_L; adv(l); }
        else break;
    }
    t.len = (int)(l->cur - s);
    return t;
}

/* ── Scan escape ─────────────────────────────────────────────────── */
static int scan_esc(Lexer *l) {
    char c = adv(l);
    switch (c) {
    case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r';
    case '\\': return '\\'; case '\'': return '\''; case '"': return '"';
    case '0': return '\0'; case 'a': return '\a'; case 'b': return '\b';
    case 'f': return '\f'; case 'v': return '\v';
    case 'x': {
        int v=0;
        while (!at_end(l) && ishex(peekc(l))) {
            char h=adv(l); v=v*16+(h<='9'?h-'0':(h|32)-'a'+10);
        }
        return v;
    }
    default:
        if (c>='0'&&c<='7') {
            int v=c-'0';
            for (int i=0;i<2&&!at_end(l)&&peekc(l)>='0'&&peekc(l)<='7';i++)
                v=v*8+(adv(l)-'0');
            return v;
        }
        diag_emit(DIAG_WARNING, l->line, l->col, "unknown escape '\\%c'", c);
        return c;
    }
}

/* ── Scan string literal ─────────────────────────────────────────── */
static Token scan_str(Lexer *l, const char *s, int ln, int co, int wide) {
    adv(l); /* opening " */
    char buf[4096]; int bl=0;
    while (!at_end(l) && peekc(l)!='"') {
        if (peekc(l)=='\n') return errtok(l,s,ln,co,"unterminated string literal");
        if (peekc(l)=='\\') { adv(l); if(bl<4095) buf[bl++]=(char)scan_esc(l); }
        else { if(bl<4095) buf[bl++]=peekc(l); adv(l); }
    }
    if (at_end(l)) return errtok(l,s,ln,co,"unterminated string literal");
    adv(l); /* closing " */
    Token t = mktok(l, TOK_STR_LIT, s, ln, co);
    t.str_idx = strtab_intern(l->strings, buf, bl);
    t.is_wide = wide;
    return t;
}

/* ── Scan char literal ───────────────────────────────────────────── */
static Token scan_chr(Lexer *l, const char *s, int ln, int co, int wide) {
    adv(l); /* opening ' */
    if (at_end(l)||peekc(l)=='\'') return errtok(l,s,ln,co,"empty character literal");
    int val;
    if (peekc(l)=='\\') { adv(l); val=scan_esc(l); }
    else { val=(unsigned char)adv(l); }
    if (at_end(l)||peekc(l)!='\'') return errtok(l,s,ln,co,"unterminated character literal");
    adv(l); /* closing ' */
    Token t = mktok(l, TOK_CHAR_LIT, s, ln, co);
    t.int_val = (unsigned long long)val; t.is_wide = wide;
    return t;
}

/* ── Main dispatch ───────────────────────────────────────────────── */
static Token scan_token(Lexer *l) {
    skip_ws(l);
    if (at_end(l)) return mktok(l, TOK_EOF, l->cur, l->line, l->col);
    const char *s = l->cur;
    int ln=l->line, co=l->col;
    char c = adv(l);

    /* Identifiers / keywords / L"" / L'' */
    if (isalpha((unsigned char)c) || c=='_') {
        if (c=='L' && !at_end(l)) {
            if (peekc(l)=='"') return scan_str(l,s,ln,co,1);
            if (peekc(l)=='\'') return scan_chr(l,s,ln,co,1);
        }
        while (!at_end(l) && (isalnum((unsigned char)peekc(l))||peekc(l)=='_')) adv(l);
        int len=(int)(l->cur-s);
        TokenKind k=lookup_keyword(s,len);
        Token t=mktok(l,k,s,ln,co);
        if (k==TOK_IDENT) { t.str_idx=strtab_intern(l->strings,s,len); t.is_wide=0; }
        return t;
    }
    /* Numbers */
    if (isdigit((unsigned char)c)) { l->cur=s; l->col=co; return scan_num(l,s,ln,co); }
    /* String / char */
    if (c=='"') { l->cur=s; l->col=co; return scan_str(l,s,ln,co,0); }
    if (c=='\'') { l->cur=s; l->col=co; return scan_chr(l,s,ln,co,0); }

    /* Operators and punctuation */
    switch (c) {
    case '(': return mktok(l,TOK_LPAREN,s,ln,co);
    case ')': return mktok(l,TOK_RPAREN,s,ln,co);
    case '{': return mktok(l,TOK_LBRACE,s,ln,co);
    case '}': return mktok(l,TOK_RBRACE,s,ln,co);
    case '[': return mktok(l,TOK_LBRACKET,s,ln,co);
    case ']': return mktok(l,TOK_RBRACKET,s,ln,co);
    case ';': return mktok(l,TOK_SEMICOLON,s,ln,co);
    case ',': return mktok(l,TOK_COMMA,s,ln,co);
    case '~': return mktok(l,TOK_TILDE,s,ln,co);
    case '?': return mktok(l,TOK_QUESTION,s,ln,co);
    case ':': return mktok(l,TOK_COLON,s,ln,co);
    case '#': return mktok(l,TOK_HASH,s,ln,co);
    case '+':
        if (peekc(l)=='+') { adv(l); return mktok(l,TOK_PLUSPLUS,s,ln,co); }
        if (peekc(l)=='=') { adv(l); return mktok(l,TOK_PLUS_ASSIGN,s,ln,co); }
        return mktok(l,TOK_PLUS,s,ln,co);
    case '-':
        if (peekc(l)=='-') { adv(l); return mktok(l,TOK_MINUSMINUS,s,ln,co); }
        if (peekc(l)=='=') { adv(l); return mktok(l,TOK_MINUS_ASSIGN,s,ln,co); }
        if (peekc(l)=='>') { adv(l); return mktok(l,TOK_ARROW,s,ln,co); }
        return mktok(l,TOK_MINUS,s,ln,co);
    case '*':
        if (peekc(l)=='=') { adv(l); return mktok(l,TOK_STAR_ASSIGN,s,ln,co); }
        return mktok(l,TOK_STAR,s,ln,co);
    case '/':
        if (peekc(l)=='=') { adv(l); return mktok(l,TOK_SLASH_ASSIGN,s,ln,co); }
        return mktok(l,TOK_SLASH,s,ln,co);
    case '%':
        if (peekc(l)=='=') { adv(l); return mktok(l,TOK_PERCENT_ASSIGN,s,ln,co); }
        return mktok(l,TOK_PERCENT,s,ln,co);
    case '&':
        if (peekc(l)=='&') { adv(l); return mktok(l,TOK_AND,s,ln,co); }
        if (peekc(l)=='=') { adv(l); return mktok(l,TOK_AMP_ASSIGN,s,ln,co); }
        return mktok(l,TOK_AMP,s,ln,co);
    case '|':
        if (peekc(l)=='|') { adv(l); return mktok(l,TOK_OR,s,ln,co); }
        if (peekc(l)=='=') { adv(l); return mktok(l,TOK_PIPE_ASSIGN,s,ln,co); }
        return mktok(l,TOK_PIPE,s,ln,co);
    case '^':
        if (peekc(l)=='=') { adv(l); return mktok(l,TOK_CARET_ASSIGN,s,ln,co); }
        return mktok(l,TOK_CARET,s,ln,co);
    case '!':
        if (peekc(l)=='=') { adv(l); return mktok(l,TOK_NEQ,s,ln,co); }
        return mktok(l,TOK_BANG,s,ln,co);
    case '=':
        if (peekc(l)=='=') { adv(l); return mktok(l,TOK_EQ,s,ln,co); }
        return mktok(l,TOK_ASSIGN,s,ln,co);
    case '<':
        if (peekc(l)=='<') { adv(l);
            if (peekc(l)=='=') { adv(l); return mktok(l,TOK_LSHIFT_ASSIGN,s,ln,co); }
            return mktok(l,TOK_LSHIFT,s,ln,co);
        }
        if (peekc(l)=='=') { adv(l); return mktok(l,TOK_LE,s,ln,co); }
        return mktok(l,TOK_LT,s,ln,co);
    case '>':
        if (peekc(l)=='>') { adv(l);
            if (peekc(l)=='=') { adv(l); return mktok(l,TOK_RSHIFT_ASSIGN,s,ln,co); }
            return mktok(l,TOK_RSHIFT,s,ln,co);
        }
        if (peekc(l)=='=') { adv(l); return mktok(l,TOK_GE,s,ln,co); }
        return mktok(l,TOK_GT,s,ln,co);
    case '.':
        if (peekc(l)=='.' && peekc2(l)=='.') { adv(l); adv(l); return mktok(l,TOK_ELLIPSIS,s,ln,co); }
        if (isdigit((unsigned char)peekc(l))) { l->cur=s; l->col=co; return scan_num(l,s,ln,co); }
        return mktok(l,TOK_DOT,s,ln,co);
    }
    return errtok(l,s,ln,co,"unexpected character");
}

Token lexer_next(Lexer *l) {
    if (l->has_peek) { l->has_peek=0; return l->peeked; }
    return scan_token(l);
}

Token lexer_peek(Lexer *l) {
    if (!l->has_peek) { l->peeked=scan_token(l); l->has_peek=1; }
    return l->peeked;
}
