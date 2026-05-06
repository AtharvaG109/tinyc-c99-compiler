/*
 * main.c — tinyc compiler entry point
 */
#include "tinyc/arena.h"
#include "tinyc/strtab.h"
#include "tinyc/diagnostic.h"
#include "tinyc/lexer.h"
#include "tinyc/type.h"
#include "tinyc/ast.h"
#include "tinyc/parser.h"
#include "tinyc/sema.h"
#include "tinyc/ir.h"
#include "tinyc/irgen.h"
#include "tinyc/codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path, size_t *out_len) {
    fprintf(stderr, "read_file: %s\n", path);
    FILE *f;
    if (strcmp(path, "-") == 0) {
        f = stdin;
    } else {
        f = fopen(path, "rb");
        if (!f) { fprintf(stderr, "error: cannot open '%s'\n", path); exit(1); }
    }
    size_t cap = 4096, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) { fprintf(stderr, "error: out of memory\n"); exit(1); }
    for (;;) {
        size_t n = fread(buf + len, 1, cap - len, f);
        len += n;
        if (n == 0) break;
        if (len == cap) { cap *= 2; buf = (char *)realloc(buf, cap); }
    }
    if (f != stdin) fclose(f);
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

static void dump_tokens(const char *filename, const char *src, size_t len) {
    Arena *arena = arena_create(0);
    StringTable strings;
    strtab_init(&strings, arena);
    diag_init(filename, src);

    Lexer lex;
    lexer_init(&lex, src, len, &strings, arena, filename);

    for (;;) {
        Token t = lexer_next(&lex);
        printf("%4d:%-3d  %-18s  '%.*s'", t.line, t.col,
               token_kind_name(t.kind), t.len, t.start);
        if (t.kind == TOK_INT_LIT) printf("  val=%llu", t.int_val);
        if (t.kind == TOK_FLOAT_LIT) printf("  val=%g", t.float_val);
        if (t.kind == TOK_IDENT) printf("  str='%s'", strtab_get(&strings, t.str_idx));
        if (t.kind == TOK_STR_LIT) printf("  str='%s'", strtab_get(&strings, t.str_idx));
        printf("\n");
        if (t.kind == TOK_EOF) break;
    }

    if (diag_error_count() > 0)
        fprintf(stderr, "%d error(s) generated.\n", diag_error_count());

    strtab_free(&strings);
    arena_destroy(arena);
}

static void dump_ast(const char *filename, const char *src, size_t len) {
    Arena *arena = arena_create(0);
    StringTable strings;
    strtab_init(&strings, arena);
    diag_init(filename, src);

    Lexer lex;
    lexer_init(&lex, src, len, &strings, arena, filename);

    TypeTable types;
    type_table_init(&types, arena);

    Parser parser;
    parser_init(&parser, &lex, arena, &strings, &types);

    ASTNode *root = parser_parse(&parser);

    if (parser_error_count(&parser) == 0 && root)
        ast_print(root, 0);

    if (diag_error_count() > 0)
        fprintf(stderr, "%d error(s) generated.\n", diag_error_count());

    strtab_free(&strings);
    arena_destroy(arena);
}

static void dump_ir(const char *filename, const char *src, size_t len) {
    Arena *arena = arena_create(0);
    StringTable strings;
    strtab_init(&strings, arena);
    diag_init(filename, src);

    Lexer lex;
    lexer_init(&lex, src, len, &strings, arena, filename);

    TypeTable types;
    type_table_init(&types, arena);

    Parser parser;
    parser_init(&parser, &lex, arena, &strings, &types);
    ASTNode *root = parser_parse(&parser);

    if (parser_error_count(&parser) > 0 || !root) {
        fprintf(stderr, "%d error(s) in parse.\n", parser_error_count(&parser));
        strtab_free(&strings);
        arena_destroy(arena);
        return;
    }

    Sema sema;
    sema_init(&sema, arena, &strings, &types);
    sema_check(&sema, root);

    IRModule *m = irgen(root, arena, &strings, &types);
    if (m) ir_print(m, stdout);

    if (diag_error_count() > 0)
        fprintf(stderr, "%d error(s) generated.\n", diag_error_count());

    strtab_free(&strings);
    arena_destroy(arena);
}

/* Full compilation pipeline: source → assembly */
static int compile(const char *filename, const char *src, size_t len,
                   const char *output) {
    Arena *arena = arena_create(0);
    StringTable strings;
    strtab_init(&strings, arena);
    diag_init(filename, src);

    Lexer lex;
    lexer_init(&lex, src, len, &strings, arena, filename);

    TypeTable types;
    type_table_init(&types, arena);

    Parser parser;
    parser_init(&parser, &lex, arena, &strings, &types);
    ASTNode *root = parser_parse(&parser);

    if (parser_error_count(&parser) > 0 || !root) {
        fprintf(stderr, "%d error(s)\n", parser_error_count(&parser));
        strtab_free(&strings);
        arena_destroy(arena);
        return 1;
    }

    Sema sema;
    sema_init(&sema, arena, &strings, &types);
    sema_check(&sema, root);

    IRModule *m = irgen(root, arena, &strings, &types);
    if (!m) {
        strtab_free(&strings);
        arena_destroy(arena);
        return 1;
    }

    FILE *out = NULL;
    if (!output || strcmp(output, "-") == 0) {
        out = stdout;
    } else {
        out = fopen(output, "w");
        if (!out) {
            fprintf(stderr, "error: cannot open output '%s'\n", output);
            strtab_free(&strings);
            arena_destroy(arena);
            return 1;
        }
    }

    codegen(m, out);

    if (out != stdout) fclose(out);

    strtab_free(&strings);
    arena_destroy(arena);
    return diag_error_count() > 0 ? 1 : 0;
}

static void usage(void) {
    fprintf(stderr, "Usage: tinyc [options] <file.c>\nOptions:\n  --dump-tokens   Lex only, print token stream\n  --dump-ast      Parse and print AST\n  --dump-ir       Parse, sema, and print TAC IR\n  -S              Compile to assembly (default output: stdout)\n  -o <file>       Output file for -S\n  -               Read source from stdin\n");
    exit(1);
}

int main(int argc, char **argv) {
    const char *input  = NULL;
    const char *output = NULL;
    int dump_tok  = 0;
    int dump_ast_flag = 0;
    int dump_ir_flag  = 0;
    int do_compile    = 0;

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--dump-tokens") == 0) { dump_tok = 1; }
        else if (strcmp(argv[i], "--dump-ast")    == 0) { dump_ast_flag = 1; }
        else if (strcmp(argv[i], "--dump-ir")     == 0) { dump_ir_flag = 1; }
        else if (strcmp(argv[i], "-S")            == 0) { do_compile = 1; }
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) { output = argv[++i]; }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { usage(); }
        else if (argv[i][0] != '-' || strcmp(argv[i], "-") == 0) { input = argv[i]; }
        else { fprintf(stderr, "unknown option: %s\n", argv[i]); usage(); }
    }

    if (!input) usage();

    fprintf(stderr, "reading input\n");
    size_t src_len;
    char *src = read_file(input, &src_len);
    fprintf(stderr, "read %zu bytes\n", src_len);
    const char *fname = (strcmp(input, "-") == 0) ? "<stdin>" : input;

    int rc = 0;
    if (dump_tok) {
        dump_tokens(fname, src, src_len);
    } else if (dump_ast_flag) {
        dump_ast(fname, src, src_len);
    } else if (dump_ir_flag) {
        dump_ir(fname, src, src_len);
    } else if (do_compile) {
        rc = compile(fname, src, src_len, output);
    } else {
        /* Default: compile to stdout */
        rc = compile(fname, src, src_len, output ? output : "-");
    }

    free(src);
    return rc != 0 ? 1 : (diag_error_count() > 0 ? 1 : 0);
}
