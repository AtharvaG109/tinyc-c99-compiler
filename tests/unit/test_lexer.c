/*
 * test_lexer.c — Comprehensive C99 lexer unit tests
 */
#include "test_harness.h"
#include "tinyc/lexer.h"
#include "tinyc/arena.h"
#include "tinyc/strtab.h"
#include "tinyc/diagnostic.h"
#include <string.h>

/* Helper: lex a string and return token array (max 128 tokens) */
static Token toks[128];
static int ntoks;
static Arena *t_arena;
static StringTable t_st;

static void lex(const char *src) {
    if (t_arena) { arena_destroy(t_arena); }
    t_arena = arena_create(0);
    strtab_init(&t_st, t_arena);
    diag_init("<test>", src);
    Lexer l;
    lexer_init(&l, src, strlen(src), &t_st, t_arena, "<test>");
    ntoks = 0;
    do {
        toks[ntoks] = lexer_next(&l);
    } while (toks[ntoks].kind != TOK_EOF && ntoks++ < 127);
    ntoks++; /* include EOF */
}

static void cleanup(void) {
    strtab_free(&t_st);
    if (t_arena) { arena_destroy(t_arena); t_arena = NULL; }
}

/* ── Keyword tests ───────────────────────────────────────────────── */
TEST(keywords_all) {
    lex("auto break case char const continue default do double else "
        "enum extern float for goto if inline int long register "
        "restrict return short signed sizeof static struct switch "
        "typedef union unsigned void volatile while _Bool");
    ASSERT_EQ(toks[0].kind, TOK_AUTO);
    ASSERT_EQ(toks[1].kind, TOK_BREAK);
    ASSERT_EQ(toks[2].kind, TOK_CASE);
    ASSERT_EQ(toks[3].kind, TOK_CHAR);
    ASSERT_EQ(toks[4].kind, TOK_CONST);
    ASSERT_EQ(toks[5].kind, TOK_CONTINUE);
    ASSERT_EQ(toks[6].kind, TOK_DEFAULT);
    ASSERT_EQ(toks[7].kind, TOK_DO);
    ASSERT_EQ(toks[8].kind, TOK_DOUBLE);
    ASSERT_EQ(toks[9].kind, TOK_ELSE);
    ASSERT_EQ(toks[10].kind, TOK_ENUM);
    ASSERT_EQ(toks[11].kind, TOK_EXTERN);
    ASSERT_EQ(toks[12].kind, TOK_FLOAT);
    ASSERT_EQ(toks[13].kind, TOK_FOR);
    ASSERT_EQ(toks[14].kind, TOK_GOTO);
    ASSERT_EQ(toks[15].kind, TOK_IF);
    ASSERT_EQ(toks[16].kind, TOK_INLINE);
    ASSERT_EQ(toks[17].kind, TOK_INT);
    ASSERT_EQ(toks[18].kind, TOK_LONG);
    ASSERT_EQ(toks[19].kind, TOK_REGISTER);
    ASSERT_EQ(toks[20].kind, TOK_RESTRICT);
    ASSERT_EQ(toks[21].kind, TOK_RETURN);
    ASSERT_EQ(toks[22].kind, TOK_SHORT);
    ASSERT_EQ(toks[23].kind, TOK_SIGNED);
    ASSERT_EQ(toks[24].kind, TOK_SIZEOF);
    ASSERT_EQ(toks[25].kind, TOK_STATIC);
    ASSERT_EQ(toks[26].kind, TOK_STRUCT);
    ASSERT_EQ(toks[27].kind, TOK_SWITCH);
    ASSERT_EQ(toks[28].kind, TOK_TYPEDEF);
    ASSERT_EQ(toks[29].kind, TOK_UNION);
    ASSERT_EQ(toks[30].kind, TOK_UNSIGNED);
    ASSERT_EQ(toks[31].kind, TOK_VOID);
    ASSERT_EQ(toks[32].kind, TOK_VOLATILE);
    ASSERT_EQ(toks[33].kind, TOK_WHILE);
    ASSERT_EQ(toks[34].kind, TOK_BOOL);
    ASSERT_EQ(toks[35].kind, TOK_EOF);
    cleanup();
}

/* ── Identifier tests ────────────────────────────────────────────── */
TEST(identifiers) {
    lex("foo _bar baz123 __x");
    ASSERT_EQ(toks[0].kind, TOK_IDENT);
    ASSERT_EQ(toks[1].kind, TOK_IDENT);
    ASSERT_EQ(toks[2].kind, TOK_IDENT);
    ASSERT_EQ(toks[3].kind, TOK_IDENT);
    cleanup();
}

/* ── Integer literal tests ───────────────────────────────────────── */
TEST(int_decimal) {
    lex("0 42 1000");
    ASSERT_EQ(toks[0].kind, TOK_INT_LIT); ASSERT_EQ(toks[0].int_val, 0);
    ASSERT_EQ(toks[1].kind, TOK_INT_LIT); ASSERT_EQ(toks[1].int_val, 42);
    ASSERT_EQ(toks[2].kind, TOK_INT_LIT); ASSERT_EQ(toks[2].int_val, 1000);
    cleanup();
}

TEST(int_hex) {
    lex("0xFF 0XDEAD");
    ASSERT_EQ(toks[0].kind, TOK_INT_LIT); ASSERT_EQ(toks[0].int_val, 0xFF);
    ASSERT_EQ(toks[1].kind, TOK_INT_LIT); ASSERT_EQ(toks[1].int_val, 0xDEAD);
    cleanup();
}

TEST(int_octal) {
    lex("077 0123");
    ASSERT_EQ(toks[0].kind, TOK_INT_LIT); ASSERT_EQ(toks[0].int_val, 077);
    ASSERT_EQ(toks[1].kind, TOK_INT_LIT); ASSERT_EQ(toks[1].int_val, 0123);
    cleanup();
}

TEST(int_suffixes) {
    lex("42u 42ULL 42ll 42LU");
    ASSERT_EQ(toks[0].kind, TOK_INT_LIT);
    ASSERT(toks[0].int_suffix & INT_SUFFIX_U);
    ASSERT_EQ(toks[1].kind, TOK_INT_LIT);
    ASSERT(toks[1].int_suffix & INT_SUFFIX_U);
    ASSERT(toks[1].int_suffix & INT_SUFFIX_LL);
    ASSERT_EQ(toks[2].kind, TOK_INT_LIT);
    ASSERT(toks[2].int_suffix & INT_SUFFIX_LL);
    cleanup();
}

/* ── Float literal tests ─────────────────────────────────────────── */
TEST(float_basic) {
    lex("3.14 .5 1e10 1.5e-3");
    ASSERT_EQ(toks[0].kind, TOK_FLOAT_LIT);
    ASSERT_EQ(toks[1].kind, TOK_FLOAT_LIT);
    ASSERT_EQ(toks[2].kind, TOK_FLOAT_LIT);
    ASSERT_EQ(toks[3].kind, TOK_FLOAT_LIT);
    cleanup();
}

TEST(float_hex) {
    lex("0x1.8p+1");
    ASSERT_EQ(toks[0].kind, TOK_FLOAT_LIT);
    cleanup();
}

/* ── String literal tests ────────────────────────────────────────── */
TEST(string_simple) {
    lex("\"hello\"");
    ASSERT_EQ(toks[0].kind, TOK_STR_LIT);
    ASSERT_STR_EQ(strtab_get(&t_st, toks[0].str_idx), "hello");
    cleanup();
}

TEST(string_escapes) {
    lex("\"a\\nb\\tc\"");
    ASSERT_EQ(toks[0].kind, TOK_STR_LIT);
    const char *s = strtab_get(&t_st, toks[0].str_idx);
    ASSERT_EQ(s[0], 'a'); ASSERT_EQ(s[1], '\n');
    ASSERT_EQ(s[2], 'b'); ASSERT_EQ(s[3], '\t');
    ASSERT_EQ(s[4], 'c');
    cleanup();
}

TEST(string_wide) {
    lex("L\"wide\"");
    ASSERT_EQ(toks[0].kind, TOK_STR_LIT);
    ASSERT_EQ(toks[0].is_wide, 1);
    cleanup();
}

/* ── Char literal tests ──────────────────────────────────────────── */
TEST(char_simple) {
    lex("'a' '0'");
    ASSERT_EQ(toks[0].kind, TOK_CHAR_LIT); ASSERT_EQ(toks[0].int_val, 'a');
    ASSERT_EQ(toks[1].kind, TOK_CHAR_LIT); ASSERT_EQ(toks[1].int_val, '0');
    cleanup();
}

TEST(char_escape) {
    lex("'\\n' '\\t' '\\\\' '\\0'");
    ASSERT_EQ(toks[0].int_val, '\n');
    ASSERT_EQ(toks[1].int_val, '\t');
    ASSERT_EQ(toks[2].int_val, '\\');
    ASSERT_EQ(toks[3].int_val, '\0');
    cleanup();
}

/* ── Operator tests ──────────────────────────────────────────────── */
TEST(operators_single) {
    lex("+ - * / % & | ^ ~ ! < > = . ? :");
    ASSERT_EQ(toks[0].kind, TOK_PLUS);
    ASSERT_EQ(toks[1].kind, TOK_MINUS);
    ASSERT_EQ(toks[2].kind, TOK_STAR);
    ASSERT_EQ(toks[3].kind, TOK_SLASH);
    ASSERT_EQ(toks[4].kind, TOK_PERCENT);
    ASSERT_EQ(toks[5].kind, TOK_AMP);
    ASSERT_EQ(toks[6].kind, TOK_PIPE);
    ASSERT_EQ(toks[7].kind, TOK_CARET);
    ASSERT_EQ(toks[8].kind, TOK_TILDE);
    ASSERT_EQ(toks[9].kind, TOK_BANG);
    ASSERT_EQ(toks[10].kind, TOK_LT);
    ASSERT_EQ(toks[11].kind, TOK_GT);
    ASSERT_EQ(toks[12].kind, TOK_ASSIGN);
    ASSERT_EQ(toks[13].kind, TOK_DOT);
    ASSERT_EQ(toks[14].kind, TOK_QUESTION);
    ASSERT_EQ(toks[15].kind, TOK_COLON);
    cleanup();
}

TEST(operators_double) {
    lex("++ -- && || << >> == != <= >= -> += -= *= /= %= &= |= ^=");
    ASSERT_EQ(toks[0].kind, TOK_PLUSPLUS);
    ASSERT_EQ(toks[1].kind, TOK_MINUSMINUS);
    ASSERT_EQ(toks[2].kind, TOK_AND);
    ASSERT_EQ(toks[3].kind, TOK_OR);
    ASSERT_EQ(toks[4].kind, TOK_LSHIFT);
    ASSERT_EQ(toks[5].kind, TOK_RSHIFT);
    ASSERT_EQ(toks[6].kind, TOK_EQ);
    ASSERT_EQ(toks[7].kind, TOK_NEQ);
    ASSERT_EQ(toks[8].kind, TOK_LE);
    ASSERT_EQ(toks[9].kind, TOK_GE);
    ASSERT_EQ(toks[10].kind, TOK_ARROW);
    ASSERT_EQ(toks[11].kind, TOK_PLUS_ASSIGN);
    ASSERT_EQ(toks[12].kind, TOK_MINUS_ASSIGN);
    ASSERT_EQ(toks[13].kind, TOK_STAR_ASSIGN);
    ASSERT_EQ(toks[14].kind, TOK_SLASH_ASSIGN);
    ASSERT_EQ(toks[15].kind, TOK_PERCENT_ASSIGN);
    ASSERT_EQ(toks[16].kind, TOK_AMP_ASSIGN);
    ASSERT_EQ(toks[17].kind, TOK_PIPE_ASSIGN);
    ASSERT_EQ(toks[18].kind, TOK_CARET_ASSIGN);
    cleanup();
}

TEST(operators_triple) {
    lex("<<= >>= ...");
    ASSERT_EQ(toks[0].kind, TOK_LSHIFT_ASSIGN);
    ASSERT_EQ(toks[1].kind, TOK_RSHIFT_ASSIGN);
    ASSERT_EQ(toks[2].kind, TOK_ELLIPSIS);
    cleanup();
}

/* ── Delimiter tests ─────────────────────────────────────────────── */
TEST(delimiters) {
    lex("( ) { } [ ] ; ,");
    ASSERT_EQ(toks[0].kind, TOK_LPAREN);
    ASSERT_EQ(toks[1].kind, TOK_RPAREN);
    ASSERT_EQ(toks[2].kind, TOK_LBRACE);
    ASSERT_EQ(toks[3].kind, TOK_RBRACE);
    ASSERT_EQ(toks[4].kind, TOK_LBRACKET);
    ASSERT_EQ(toks[5].kind, TOK_RBRACKET);
    ASSERT_EQ(toks[6].kind, TOK_SEMICOLON);
    ASSERT_EQ(toks[7].kind, TOK_COMMA);
    cleanup();
}

/* ── Comment tests ───────────────────────────────────────────────── */
TEST(line_comment) {
    lex("42 // this is a comment\n43");
    ASSERT_EQ(toks[0].kind, TOK_INT_LIT); ASSERT_EQ(toks[0].int_val, 42);
    ASSERT_EQ(toks[1].kind, TOK_INT_LIT); ASSERT_EQ(toks[1].int_val, 43);
    cleanup();
}

TEST(block_comment) {
    lex("42 /* block\ncomment */ 43");
    ASSERT_EQ(toks[0].kind, TOK_INT_LIT); ASSERT_EQ(toks[0].int_val, 42);
    ASSERT_EQ(toks[1].kind, TOK_INT_LIT); ASSERT_EQ(toks[1].int_val, 43);
    cleanup();
}

/* ── Source location tests ───────────────────────────────────────── */
TEST(line_numbers) {
    lex("a\nb\nc");
    ASSERT_EQ(toks[0].line, 1);
    ASSERT_EQ(toks[1].line, 2);
    ASSERT_EQ(toks[2].line, 3);
    cleanup();
}

/* ── Edge cases ──────────────────────────────────────────────────── */
TEST(empty_input) {
    lex("");
    ASSERT_EQ(toks[0].kind, TOK_EOF);
    cleanup();
}

TEST(whitespace_only) {
    lex("   \t\n\r  ");
    ASSERT_EQ(toks[0].kind, TOK_EOF);
    cleanup();
}

/* ── Real C code ─────────────────────────────────────────────────── */
TEST(real_function) {
    lex("int main(void) { return 0; }");
    ASSERT_EQ(toks[0].kind, TOK_INT);
    ASSERT_EQ(toks[1].kind, TOK_IDENT);
    ASSERT_EQ(toks[2].kind, TOK_LPAREN);
    ASSERT_EQ(toks[3].kind, TOK_VOID);
    ASSERT_EQ(toks[4].kind, TOK_RPAREN);
    ASSERT_EQ(toks[5].kind, TOK_LBRACE);
    ASSERT_EQ(toks[6].kind, TOK_RETURN);
    ASSERT_EQ(toks[7].kind, TOK_INT_LIT);
    ASSERT_EQ(toks[8].kind, TOK_SEMICOLON);
    ASSERT_EQ(toks[9].kind, TOK_RBRACE);
    ASSERT_EQ(toks[10].kind, TOK_EOF);
    cleanup();
}

TEST(peek_does_not_consume) {
    Arena *a = arena_create(0);
    StringTable st; strtab_init(&st, a);
    const char *src = "int x";
    diag_init("<test>", src);
    Lexer l; lexer_init(&l, src, strlen(src), &st, a, "<test>");
    Token p = lexer_peek(&l);
    ASSERT_EQ(p.kind, TOK_INT);
    Token n = lexer_next(&l);
    ASSERT_EQ(n.kind, TOK_INT); /* same token */
    Token n2 = lexer_next(&l);
    ASSERT_EQ(n2.kind, TOK_IDENT);
    strtab_free(&st); arena_destroy(a);
}

int main(void) {
    printf("=== Lexer Tests ===\n");
    RUN_TEST(keywords_all);
    RUN_TEST(identifiers);
    RUN_TEST(int_decimal);
    RUN_TEST(int_hex);
    RUN_TEST(int_octal);
    RUN_TEST(int_suffixes);
    RUN_TEST(float_basic);
    RUN_TEST(float_hex);
    RUN_TEST(string_simple);
    RUN_TEST(string_escapes);
    RUN_TEST(string_wide);
    RUN_TEST(char_simple);
    RUN_TEST(char_escape);
    RUN_TEST(operators_single);
    RUN_TEST(operators_double);
    RUN_TEST(operators_triple);
    RUN_TEST(delimiters);
    RUN_TEST(line_comment);
    RUN_TEST(block_comment);
    RUN_TEST(line_numbers);
    RUN_TEST(empty_input);
    RUN_TEST(whitespace_only);
    RUN_TEST(real_function);
    RUN_TEST(peek_does_not_consume);
    TEST_SUMMARY();
}
