/*
 * test_parser.c — Unit tests for the C99 parser
 */
#include "test_harness.h"
#include "tinyc/arena.h"
#include "tinyc/strtab.h"
#include "tinyc/diagnostic.h"
#include "tinyc/lexer.h"
#include "tinyc/type.h"
#include "tinyc/abi_classify.h"
#include "tinyc/ast.h"
#include "tinyc/parser.h"
#include <string.h>

/* Helper: parse a string and return the AST root.
 * Reuses a single global arena + strtab that is reset between tests. */
static Arena *g_arena;
static StringTable g_strings;
static TypeTable g_types;

static ASTNode *parse_str(const char *src) {
    arena_reset(g_arena);
    strtab_free(&g_strings);
    strtab_init(&g_strings, g_arena);
    type_table_init(&g_types, g_arena);
    diag_init("<test>", src);

    Lexer lex;
    lexer_init(&lex, src, strlen(src), &g_strings, g_arena, "<test>");

    Parser p;
    parser_init(&p, &lex, g_arena, &g_strings, &g_types);
    return parser_parse(&p);
}

/* Return the number of errors from the last parse_str call.
 * We check diag_error_count because parser may emit both. */
static int last_errors(void) { return diag_error_count(); }

/* ── Helpers ────────────────────────────────────────────────────── */

/* Get the n-th (0-indexed) top-level declaration from a translation unit. */
static ASTNode *get_decl(ASTNode *root, int n) {
    if (!root || root->kind != AST_TRANSLATION_UNIT) return NULL;
    ASTNode *d = root->tu.decls;
    for (int i = 0; i < n && d; i++) d = d->next;
    return d;
}

/* ═══════════════════════════════════════════════════════════════════
 * Type system tests
 * ═══════════════════════════════════════════════════════════════════ */

TEST(type_singletons_are_identical) {
    Arena *a = arena_create(0);
    TypeTable tt;
    type_table_init(&tt, a);
    ASSERT(type_int() == type_int());
    ASSERT(type_void() != type_int());
    ASSERT(type_long() != type_int());
    arena_destroy(a);
}

TEST(type_pointer_interning) {
    Arena *a = arena_create(0);
    TypeTable tt;
    type_table_init(&tt, a);
    Type *p1 = type_pointer(&tt, type_int(), QUAL_NONE);
    Type *p2 = type_pointer(&tt, type_int(), QUAL_NONE);
    Type *p3 = type_pointer(&tt, type_int(), QUAL_CONST);
    ASSERT(p1 == p2);   /* same pointer — interned */
    ASSERT(p1 != p3);   /* different qualifier */
    arena_destroy(a);
}

TEST(type_sizeof_basic) {
    ASSERT_EQ(type_sizeof(type_char()),   1);
    ASSERT_EQ(type_sizeof(type_short()),  2);
    ASSERT_EQ(type_sizeof(type_int()),    4);
    ASSERT_EQ(type_sizeof(type_long()),   8);
    ASSERT_EQ(type_sizeof(type_llong()),  8);
    ASSERT_EQ(type_sizeof(type_float()),  4);
    ASSERT_EQ(type_sizeof(type_double()), 8);
}

TEST(type_sizeof_pointer) {
    Arena *a = arena_create(0);
    TypeTable tt;
    type_table_init(&tt, a);
    Type *p = type_pointer(&tt, type_int(), QUAL_NONE);
    ASSERT_EQ(type_sizeof(p), 8);
    arena_destroy(a);
}

TEST(type_sizeof_array) {
    Arena *a = arena_create(0);
    TypeTable tt;
    type_table_init(&tt, a);
    Type *arr = type_array(&tt, type_int(), 10);
    ASSERT_EQ(type_sizeof(arr), 40);
    arena_destroy(a);
}

TEST(type_struct_layout) {
    Arena *a = arena_create(0);
    TypeTable tt;
    type_table_init(&tt, a);

    Type *s = type_struct(&tt, "Point");
    /* Build fields manually */
    StructField *fx = (StructField *)arena_alloc(a, sizeof(StructField), _Alignof(StructField));
    memset(fx, 0, sizeof(*fx)); fx->bit_width = -1;
    fx->name = "x"; fx->type = type_int(); fx->offset = 0; fx->next = NULL;
    StructField *fy = (StructField *)arena_alloc(a, sizeof(StructField), _Alignof(StructField));
    memset(fy, 0, sizeof(*fy)); fy->bit_width = -1;
    fy->name = "y"; fy->type = type_int(); fy->offset = 0; fy->next = NULL;
    fx->next = fy;
    s->agg.fields = fx;
    type_seal_struct(s);

    ASSERT_EQ(type_sizeof(s), 8);
    ASSERT_EQ(fx->offset, 0);
    ASSERT_EQ(fy->offset, 4);

    arena_destroy(a);
}

TEST(type_struct_padding_layout) {
    Arena *a = arena_create(0);
    TypeTable tt;
    type_table_init(&tt, a);

    Type *s = type_struct(&tt, "Padded");
    StructField *fc = (StructField *)arena_alloc(a, sizeof(StructField), _Alignof(StructField));
    memset(fc, 0, sizeof(*fc)); fc->bit_width = -1;
    fc->name = "c"; fc->type = type_char(); fc->offset = 0; fc->next = NULL;
    StructField *fi = (StructField *)arena_alloc(a, sizeof(StructField), _Alignof(StructField));
    memset(fi, 0, sizeof(*fi)); fi->bit_width = -1;
    fi->name = "i"; fi->type = type_int(); fi->offset = 0; fi->next = NULL;
    fc->next = fi;
    s->agg.fields = fc;
    type_seal_struct(s);

    ASSERT_EQ(type_sizeof(s), 8);
    ASSERT_EQ(type_alignof(s), 4);
    ASSERT_EQ(fc->offset, 0);
    ASSERT_EQ(fi->offset, 4);

    arena_destroy(a);
}

TEST(type_union_layout) {
    Arena *a = arena_create(0);
    TypeTable tt;
    type_table_init(&tt, a);

    Type *u = type_union(&tt, "Value");
    StructField *fc = (StructField *)arena_alloc(a, sizeof(StructField), _Alignof(StructField));
    memset(fc, 0, sizeof(*fc)); fc->bit_width = -1;
    fc->name = "c"; fc->type = type_char(); fc->offset = 0; fc->next = NULL;
    StructField *fi = (StructField *)arena_alloc(a, sizeof(StructField), _Alignof(StructField));
    memset(fi, 0, sizeof(*fi)); fi->bit_width = -1;
    fi->name = "i"; fi->type = type_int(); fi->offset = 0; fi->next = NULL;
    fc->next = fi;
    u->agg.fields = fc;
    type_seal_union(u);

    ASSERT_EQ(type_sizeof(u), 4);
    ASSERT_EQ(type_alignof(u), 4);
    ASSERT_EQ(fc->offset, 0);
    ASSERT_EQ(fi->offset, 0);

    arena_destroy(a);
}

TEST(type_compatible_same_kind) {
    ASSERT(type_compatible(type_int(), type_int()));
    ASSERT(!type_compatible(type_int(), type_long()));
}

TEST(type_compatible_pointers) {
    Arena *a = arena_create(0);
    TypeTable tt;
    type_table_init(&tt, a);
    Type *pi1 = type_pointer(&tt, type_int(), QUAL_NONE);
    Type *pi2 = type_pointer(&tt, type_int(), QUAL_NONE);
    Type *pf  = type_pointer(&tt, type_float(), QUAL_NONE);
    ASSERT(type_compatible(pi1, pi2));
    ASSERT(!type_compatible(pi1, pf));
    arena_destroy(a);
}

TEST(abi_classifies_struct_registers_and_memory) {
    Arena *a = arena_create(0);
    TypeTable tt;
    type_table_init(&tt, a);

    Type *point = type_struct(&tt, "Point");
    StructField *fx = (StructField *)arena_alloc(a, sizeof(StructField), _Alignof(StructField));
    memset(fx, 0, sizeof(*fx)); fx->bit_width = -1;
    fx->name = "x"; fx->type = type_int(); fx->offset = 0; fx->next = NULL;
    StructField *fy = (StructField *)arena_alloc(a, sizeof(StructField), _Alignof(StructField));
    memset(fy, 0, sizeof(*fy)); fy->bit_width = -1;
    fy->name = "y"; fy->type = type_int(); fy->offset = 0; fy->next = NULL;
    fx->next = fy;
    point->agg.fields = fx;
    type_seal_struct(point);
    ABIClassification pc = abi_classify(point);
    ASSERT_EQ(pc.n_eightbytes, 1);
    ASSERT_EQ(pc.classes[0], ABI_CLASS_INTEGER);
    ASSERT(!abi_in_memory(&pc));

    Type *mixed = type_struct(&tt, "Mixed");
    StructField *fn = (StructField *)arena_alloc(a, sizeof(StructField), _Alignof(StructField));
    memset(fn, 0, sizeof(*fn)); fn->bit_width = -1;
    fn->name = "n"; fn->type = type_int(); fn->offset = 0; fn->next = NULL;
    StructField *fd = (StructField *)arena_alloc(a, sizeof(StructField), _Alignof(StructField));
    memset(fd, 0, sizeof(*fd)); fd->bit_width = -1;
    fd->name = "d"; fd->type = type_double(); fd->offset = 0; fd->next = NULL;
    fn->next = fd;
    mixed->agg.fields = fn;
    type_seal_struct(mixed);
    ABIClassification mc = abi_classify(mixed);
    ASSERT_EQ(mc.n_eightbytes, 2);
    ASSERT_EQ(mc.classes[0], ABI_CLASS_INTEGER);
    ASSERT_EQ(mc.classes[1], ABI_CLASS_SSE);
    ASSERT(!abi_in_memory(&mc));

    Type *big = type_struct(&tt, "Big");
    StructField *fa = (StructField *)arena_alloc(a, sizeof(StructField), _Alignof(StructField));
    memset(fa, 0, sizeof(*fa)); fa->bit_width = -1;
    fa->name = "a"; fa->type = type_array(&tt, type_long(), 9); fa->offset = 0; fa->next = NULL;
    big->agg.fields = fa;
    type_seal_struct(big);
    ABIClassification bc = abi_classify(big);
    ASSERT(abi_in_memory(&bc));

    arena_destroy(a);
}

/* ═══════════════════════════════════════════════════════════════════
 * Parser tests
 * ═══════════════════════════════════════════════════════════════════ */

TEST(parse_empty) {
    ASTNode *root = parse_str("");
    ASSERT(root != NULL);
    ASSERT_EQ(root->kind, AST_TRANSLATION_UNIT);
    ASSERT(root->tu.decls == NULL);
    ASSERT_EQ(last_errors(), 0);
}

TEST(parse_int_global) {
    ASTNode *root = parse_str("int x;");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *d = get_decl(root, 0);
    ASSERT(d != NULL);
    ASSERT_EQ(d->kind, AST_VAR_DECL);
    ASSERT_STR_EQ(d->var_decl.name, "x");
    ASSERT(d->var_decl.decl_type == type_int());
    ASSERT(d->var_decl.init == NULL);
}

TEST(parse_int_global_with_init) {
    ASTNode *root = parse_str("int x = 42;");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *d = get_decl(root, 0);
    ASSERT(d != NULL);
    ASSERT_EQ(d->kind, AST_VAR_DECL);
    ASSERT(d->var_decl.init != NULL);
    ASSERT_EQ(d->var_decl.init->kind, AST_INT_LIT);
    ASSERT_EQ((long long)d->var_decl.init->int_lit.val, 42);
}

TEST(parse_pointer_decl) {
    ASTNode *root = parse_str("int *p;");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *d = get_decl(root, 0);
    ASSERT(d != NULL);
    ASSERT_EQ(d->kind, AST_VAR_DECL);
    ASSERT(d->var_decl.decl_type != NULL);
    ASSERT_EQ(d->var_decl.decl_type->kind, TY_POINTER);
    ASSERT(d->var_decl.decl_type->ptr.pointee == type_int());
}

TEST(parse_pointer_to_pointer) {
    ASTNode *root = parse_str("int **pp;");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *d = get_decl(root, 0);
    ASSERT(d != NULL);
    ASSERT_EQ(d->var_decl.decl_type->kind, TY_POINTER);
    ASSERT_EQ(d->var_decl.decl_type->ptr.pointee->kind, TY_POINTER);
}

TEST(parse_const_pointer) {
    ASTNode *root = parse_str("const int *p;");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *d = get_decl(root, 0);
    ASSERT(d != NULL);
    ASSERT_EQ(d->var_decl.decl_type->kind, TY_POINTER);
    /* pointee is const int */
    ASSERT(d->var_decl.decl_type->ptr.pointee->qual & QUAL_CONST);
}

TEST(parse_array_decl) {
    ASTNode *root = parse_str("int arr[10];");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *d = get_decl(root, 0);
    ASSERT(d != NULL);
    ASSERT_EQ(d->var_decl.decl_type->kind, TY_ARRAY);
    ASSERT_EQ(d->var_decl.decl_type->arr.count, 10);
    ASSERT(d->var_decl.decl_type->arr.elem == type_int());
}

TEST(parse_function_declaration) {
    ASTNode *root = parse_str("int add(int a, int b);");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *d = get_decl(root, 0);
    ASSERT(d != NULL);
    ASSERT_EQ(d->kind, AST_VAR_DECL);
    ASSERT_EQ(d->var_decl.decl_type->kind, TY_FUNC);
    ASSERT(d->var_decl.decl_type->func.ret == type_int());
    ASSERT_EQ(d->var_decl.decl_type->func.n_params, 2);
}

TEST(parse_function_definition) {
    ASTNode *root = parse_str("int main(void) { return 0; }");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *d = get_decl(root, 0);
    ASSERT(d != NULL);
    ASSERT_EQ(d->kind, AST_FUNC_DEF);
    ASSERT_STR_EQ(d->func_def.name, "main");
    ASSERT(d->func_def.body != NULL);
    ASSERT_EQ(d->func_def.body->kind, AST_COMPOUND);
}

TEST(parse_function_return_stmt) {
    ASTNode *root = parse_str("int f(void) { return 42; }");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *fn = get_decl(root, 0);
    ASSERT(fn != NULL && fn->kind == AST_FUNC_DEF);
    ASTNode *stmt = fn->func_def.body->compound.stmts;
    ASSERT(stmt != NULL);
    ASSERT_EQ(stmt->kind, AST_RETURN);
    ASSERT(stmt->ret.expr != NULL);
    ASSERT_EQ(stmt->ret.expr->kind, AST_INT_LIT);
    ASSERT_EQ((long long)stmt->ret.expr->int_lit.val, 42);
}

TEST(parse_typedef) {
    ASTNode *root = parse_str("typedef int MyInt; MyInt x;");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *td = get_decl(root, 0);
    ASSERT(td != NULL);
    ASSERT_EQ(td->kind, AST_TYPEDEF_DECL);
    ASSERT_STR_EQ(td->typedef_decl.name, "MyInt");

    /* x should have type int (via the typedef) */
    ASTNode *var = get_decl(root, 1);
    ASSERT(var != NULL);
    ASSERT_EQ(var->kind, AST_VAR_DECL);
    ASSERT(var->var_decl.decl_type == type_int());
}

TEST(parse_struct_decl) {
    ASTNode *root = parse_str("struct Point { int x; int y; }; struct Point p;");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *sd = get_decl(root, 0);
    ASSERT(sd != NULL);
    ASSERT_EQ(sd->kind, AST_STRUCT_DECL);

    ASTNode *var = get_decl(root, 1);
    ASSERT(var != NULL);
    ASSERT_EQ(var->kind, AST_VAR_DECL);
    ASSERT(var->var_decl.decl_type->kind == TY_STRUCT);
}

TEST(parse_enum_decl) {
    ASTNode *root = parse_str("enum Color { RED, GREEN, BLUE }; enum Color c;");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *ed = get_decl(root, 0);
    ASSERT(ed != NULL);
    /* First decl should be the struct/enum type decl */
}

TEST(parse_if_stmt) {
    ASTNode *root = parse_str("void f(void) { if (1) { } else { } }");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *fn = get_decl(root, 0);
    ASSERT(fn != NULL && fn->kind == AST_FUNC_DEF);
    ASTNode *stmt = fn->func_def.body->compound.stmts;
    ASSERT(stmt != NULL);
    ASSERT_EQ(stmt->kind, AST_IF);
    ASSERT(stmt->if_stmt.cond != NULL);
    ASSERT(stmt->if_stmt.then_stmt != NULL);
    ASSERT(stmt->if_stmt.else_stmt != NULL);
}

TEST(parse_while_stmt) {
    ASTNode *root = parse_str("void f(void) { while (x) { break; } }");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *fn = get_decl(root, 0);
    ASTNode *stmt = fn->func_def.body->compound.stmts;
    ASSERT(stmt != NULL);
    ASSERT_EQ(stmt->kind, AST_WHILE);
}

TEST(parse_for_stmt) {
    ASTNode *root = parse_str("void f(void) { for (int i = 0; i < 10; i++) {} }");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *fn = get_decl(root, 0);
    ASTNode *stmt = fn->func_def.body->compound.stmts;
    ASSERT(stmt != NULL);
    ASSERT_EQ(stmt->kind, AST_FOR);
    ASSERT(stmt->for_stmt.init != NULL);
    ASSERT(stmt->for_stmt.cond != NULL);
    ASSERT(stmt->for_stmt.incr != NULL);
}

TEST(parse_do_while) {
    ASTNode *root = parse_str("void f(void) { do { } while (0); }");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *fn = get_decl(root, 0);
    ASTNode *stmt = fn->func_def.body->compound.stmts;
    ASSERT(stmt != NULL);
    ASSERT_EQ(stmt->kind, AST_DO_WHILE);
}

TEST(parse_switch_stmt) {
    ASTNode *root = parse_str(
        "void f(void) {"
        "  switch (x) {"
        "  case 1: break;"
        "  default: break;"
        "  }"
        "}");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *fn = get_decl(root, 0);
    ASTNode *stmt = fn->func_def.body->compound.stmts;
    ASSERT(stmt != NULL);
    ASSERT_EQ(stmt->kind, AST_SWITCH);
}

TEST(parse_goto_label) {
    ASTNode *root = parse_str(
        "void f(void) { goto done; done: return; }");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *fn = get_decl(root, 0);
    ASTNode *s1 = fn->func_def.body->compound.stmts;
    ASSERT(s1 != NULL);
    ASSERT_EQ(s1->kind, AST_GOTO);
    ASSERT_STR_EQ(s1->go.label, "done");
}

TEST(parse_expr_binop) {
    ASTNode *root = parse_str("int x = 1 + 2 * 3;");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *d = get_decl(root, 0);
    ASTNode *init = d->var_decl.init;
    ASSERT(init != NULL);
    /* 1 + (2 * 3): top node is + */
    ASSERT_EQ(init->kind, AST_BINOP);
    ASSERT_EQ(init->binop.op, TOK_PLUS);
    /* Right child is 2 * 3 */
    ASSERT_EQ(init->binop.right->kind, AST_BINOP);
    ASSERT_EQ(init->binop.right->binop.op, TOK_STAR);
}

TEST(parse_expr_assign) {
    ASTNode *root = parse_str("void f(void) { x = y = 5; }");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *fn = get_decl(root, 0);
    ASTNode *stmt = fn->func_def.body->compound.stmts;
    ASSERT_EQ(stmt->kind, AST_EXPR_STMT);
    ASTNode *e = stmt->expr_stmt.expr;
    ASSERT_EQ(e->kind, AST_ASSIGN);
    /* Right side is another assign (right-associative) */
    ASSERT_EQ(e->assign.right->kind, AST_ASSIGN);
}

TEST(parse_expr_ternary) {
    ASTNode *root = parse_str("int x = a ? b : c;");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *d = get_decl(root, 0);
    ASTNode *init = d->var_decl.init;
    ASSERT(init != NULL);
    ASSERT_EQ(init->kind, AST_TERNARY);
}

TEST(parse_expr_call) {
    ASTNode *root = parse_str("void f(void) { foo(1, 2, 3); }");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *fn = get_decl(root, 0);
    ASTNode *stmt = fn->func_def.body->compound.stmts;
    ASSERT_EQ(stmt->kind, AST_EXPR_STMT);
    ASTNode *e = stmt->expr_stmt.expr;
    ASSERT_EQ(e->kind, AST_CALL);
    ASSERT_EQ(e->call.n_args, 3);
}

TEST(parse_expr_subscript) {
    ASTNode *root = parse_str("void f(void) { x = arr[i]; }");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *fn = get_decl(root, 0);
    ASTNode *stmt = fn->func_def.body->compound.stmts;
    ASTNode *e = stmt->expr_stmt.expr;
    ASSERT_EQ(e->kind, AST_ASSIGN);
    ASSERT_EQ(e->assign.right->kind, AST_SUBSCRIPT);
}

TEST(parse_expr_member) {
    ASTNode *root = parse_str("void f(void) { x = s.field; }");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *fn = get_decl(root, 0);
    ASTNode *stmt = fn->func_def.body->compound.stmts;
    ASTNode *e = stmt->expr_stmt.expr;
    ASSERT_EQ(e->assign.right->kind, AST_MEMBER);
    ASSERT_STR_EQ(e->assign.right->member.field, "field");
}

TEST(parse_expr_arrow) {
    ASTNode *root = parse_str("void f(void) { x = p->field; }");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *fn = get_decl(root, 0);
    ASTNode *stmt = fn->func_def.body->compound.stmts;
    ASTNode *e = stmt->expr_stmt.expr;
    ASSERT_EQ(e->assign.right->kind, AST_MEMBER_PTR);
}

TEST(parse_expr_cast) {
    ASTNode *root = parse_str("void f(void) { x = (int)3.14; }");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *fn = get_decl(root, 0);
    ASTNode *stmt = fn->func_def.body->compound.stmts;
    ASTNode *e = stmt->expr_stmt.expr;
    ASSERT_EQ(e->assign.right->kind, AST_CAST);
    ASSERT(e->assign.right->cast.cast_type == type_int());
}

TEST(parse_sizeof_type) {
    ASTNode *root = parse_str("int x = sizeof(int);");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *d = get_decl(root, 0);
    ASSERT_EQ(d->var_decl.init->kind, AST_SIZEOF_TYPE);
    ASSERT(d->var_decl.init->sizeof_type.sizeof_type == type_int());
}

TEST(parse_sizeof_expr) {
    ASTNode *root = parse_str("void f(void) { int x = sizeof x; }");
    ASSERT(root != NULL);
    ASSERT_EQ(last_errors(), 0);
}

TEST(parse_unop_prefix) {
    ASTNode *root = parse_str("void f(void) { x = -y; }");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *fn = get_decl(root, 0);
    ASTNode *stmt = fn->func_def.body->compound.stmts;
    ASTNode *e = stmt->expr_stmt.expr->assign.right;
    ASSERT_EQ(e->kind, AST_UNOP);
    ASSERT_EQ(e->unop.op, TOK_MINUS);
}

TEST(parse_postfix_inc) {
    ASTNode *root = parse_str("void f(void) { i++; }");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *fn = get_decl(root, 0);
    ASTNode *stmt = fn->func_def.body->compound.stmts;
    ASTNode *e = stmt->expr_stmt.expr;
    ASSERT_EQ(e->kind, AST_POSTFIX);
    ASSERT_EQ(e->postfix.op, TOK_PLUSPLUS);
}

TEST(parse_prefix_inc) {
    ASTNode *root = parse_str("void f(void) { ++i; }");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *fn = get_decl(root, 0);
    ASTNode *stmt = fn->func_def.body->compound.stmts;
    ASTNode *e = stmt->expr_stmt.expr;
    ASSERT_EQ(e->kind, AST_UNOP);
    ASSERT_EQ(e->unop.op, TOK_PLUSPLUS);
}

TEST(parse_addr_of_deref) {
    ASTNode *root = parse_str("void f(void) { int *p = &x; int y = *p; }");
    ASSERT(root != NULL);
    ASSERT_EQ(last_errors(), 0);
}

TEST(parse_init_list) {
    ASTNode *root = parse_str("int arr[3] = {1, 2, 3};");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *d = get_decl(root, 0);
    ASSERT(d->var_decl.init != NULL);
    ASSERT_EQ(d->var_decl.init->kind, AST_INIT_LIST);
    ASSERT_EQ(d->var_decl.init->init_list.n_items, 3);
}

TEST(parse_designated_init) {
    ASTNode *root = parse_str("struct { int x; int y; } s = { .x = 1, .y = 2 };");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *d = get_decl(root, 0);
    ASSERT(d->var_decl.init != NULL);
    ASSERT_EQ(d->var_decl.init->kind, AST_INIT_LIST);
    ASTNode *first = d->var_decl.init->init_list.items;
    ASSERT(first != NULL);
    ASSERT_EQ(first->kind, AST_DESIGNATOR);
    ASSERT_EQ(first->designator.is_field, 1);
    ASSERT_STR_EQ(first->designator.field, "x");
}

TEST(parse_compound_literal) {
    ASTNode *root = parse_str("void f(void) { p = (struct Point){1, 2}; }");
    ASSERT(root != NULL);
    ASSERT_EQ(last_errors(), 0);
}

TEST(parse_variadic_func) {
    ASTNode *root = parse_str("int printf(const char *fmt, ...);");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *d = get_decl(root, 0);
    ASSERT(d != NULL);
    ASSERT_EQ(d->var_decl.decl_type->kind, TY_FUNC);
    ASSERT_EQ(d->var_decl.decl_type->func.variadic, 1);
}

TEST(parse_multi_decl) {
    ASTNode *root = parse_str("int a, b, c;");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *d1 = get_decl(root, 0);
    ASTNode *d2 = get_decl(root, 1);
    ASTNode *d3 = get_decl(root, 2);
    ASSERT(d1 != NULL && d2 != NULL && d3 != NULL);
    ASSERT_EQ(d1->kind, AST_VAR_DECL);
    ASSERT_STR_EQ(d1->var_decl.name, "a");
    ASSERT_STR_EQ(d2->var_decl.name, "b");
    ASSERT_STR_EQ(d3->var_decl.name, "c");
}

TEST(parse_static_storage) {
    ASTNode *root = parse_str("static int x;");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *d = get_decl(root, 0);
    ASSERT_EQ(d->var_decl.storage, SC_STATIC);
}

TEST(parse_extern_decl) {
    ASTNode *root = parse_str("extern int errno;");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *d = get_decl(root, 0);
    ASSERT_EQ(d->var_decl.storage, SC_EXTERN);
}

TEST(parse_fib_function) {
    /* Larger integration-style test */
    const char *src =
        "int fib(int n) {\n"
        "    if (n <= 1) return n;\n"
        "    return fib(n-1) + fib(n-2);\n"
        "}\n";
    ASTNode *root = parse_str(src);
    ASSERT_EQ(last_errors(), 0);
    ASTNode *fn = get_decl(root, 0);
    ASSERT(fn != NULL);
    ASSERT_EQ(fn->kind, AST_FUNC_DEF);
    ASSERT_STR_EQ(fn->func_def.name, "fib");
}

TEST(parse_long_long_type) {
    ASTNode *root = parse_str("long long x;");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *d = get_decl(root, 0);
    ASSERT(d->var_decl.decl_type == type_llong());
}

TEST(parse_unsigned_long) {
    ASTNode *root = parse_str("unsigned long x;");
    ASSERT_EQ(last_errors(), 0);
    ASTNode *d = get_decl(root, 0);
    ASSERT(d->var_decl.decl_type == type_ulong());
}

int main(void) {
    g_arena = arena_create(256 * 1024);
    strtab_init(&g_strings, g_arena);
    type_table_init(&g_types, g_arena);

    printf("=== Type system tests ===\n");
    RUN_TEST(type_singletons_are_identical);
    RUN_TEST(type_pointer_interning);
    RUN_TEST(type_sizeof_basic);
    RUN_TEST(type_sizeof_pointer);
    RUN_TEST(type_sizeof_array);
    RUN_TEST(type_struct_layout);
    RUN_TEST(type_struct_padding_layout);
    RUN_TEST(type_union_layout);
    RUN_TEST(type_compatible_same_kind);
    RUN_TEST(type_compatible_pointers);
    RUN_TEST(abi_classifies_struct_registers_and_memory);

    printf("\n=== Parser tests ===\n");
    RUN_TEST(parse_empty);
    RUN_TEST(parse_int_global);
    RUN_TEST(parse_int_global_with_init);
    RUN_TEST(parse_pointer_decl);
    RUN_TEST(parse_pointer_to_pointer);
    RUN_TEST(parse_const_pointer);
    RUN_TEST(parse_array_decl);
    RUN_TEST(parse_function_declaration);
    RUN_TEST(parse_function_definition);
    RUN_TEST(parse_function_return_stmt);
    RUN_TEST(parse_typedef);
    RUN_TEST(parse_struct_decl);
    RUN_TEST(parse_enum_decl);
    RUN_TEST(parse_if_stmt);
    RUN_TEST(parse_while_stmt);
    RUN_TEST(parse_for_stmt);
    RUN_TEST(parse_do_while);
    RUN_TEST(parse_switch_stmt);
    RUN_TEST(parse_goto_label);
    RUN_TEST(parse_expr_binop);
    RUN_TEST(parse_expr_assign);
    RUN_TEST(parse_expr_ternary);
    RUN_TEST(parse_expr_call);
    RUN_TEST(parse_expr_subscript);
    RUN_TEST(parse_expr_member);
    RUN_TEST(parse_expr_arrow);
    RUN_TEST(parse_expr_cast);
    RUN_TEST(parse_sizeof_type);
    RUN_TEST(parse_sizeof_expr);
    RUN_TEST(parse_unop_prefix);
    RUN_TEST(parse_postfix_inc);
    RUN_TEST(parse_prefix_inc);
    RUN_TEST(parse_addr_of_deref);
    RUN_TEST(parse_init_list);
    RUN_TEST(parse_designated_init);
    RUN_TEST(parse_compound_literal);
    RUN_TEST(parse_variadic_func);
    RUN_TEST(parse_multi_decl);
    RUN_TEST(parse_static_storage);
    RUN_TEST(parse_extern_decl);
    RUN_TEST(parse_fib_function);
    RUN_TEST(parse_long_long_type);
    RUN_TEST(parse_unsigned_long);

    strtab_free(&g_strings);
    arena_destroy(g_arena);

    TEST_SUMMARY();
}
