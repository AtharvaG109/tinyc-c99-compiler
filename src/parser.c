/*
 * parser.c — C99 recursive-descent + Pratt expression parser
 */
#include "tinyc/parser.h"
#include "tinyc/diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════
 * AST helpers (ast_alloc, ast_list_append, ast_list_len, ast_print)
 * ═══════════════════════════════════════════════════════════════════ */

ASTNode *ast_alloc(Arena *arena, ASTNodeKind kind, int line, int col) {
    ASTNode *n = (ASTNode *)arena_alloc(arena, sizeof(ASTNode), _Alignof(ASTNode));
    memset(n, 0, sizeof(*n));
    n->kind = kind;
    n->line = line;
    n->col  = col;
    return n;
}

void ast_list_append(ASTNode **head, ASTNode **tail_inout, ASTNode *node) {
    node->next = NULL;
    if (!*head) {
        *head = node;
        if (tail_inout) *tail_inout = node;
    } else if (tail_inout && *tail_inout) {
        (*tail_inout)->next = node;
        *tail_inout = node;
    } else {
        /* O(n) fallback — walk to end */
        ASTNode *cur = *head;
        while (cur->next) cur = cur->next;
        cur->next = node;
    }
}

int ast_list_len(const ASTNode *head) {
    int n = 0;
    for (; head; head = head->next) n++;
    return n;
}

/* ── AST printer ─────────────────────────────────────────────────── */

static void print_indent(int indent) {
    for (int i = 0; i < indent * 2; i++) putchar(' ');
}

static const char *ast_kind_name(ASTNodeKind k) {
    switch (k) {
    case AST_TRANSLATION_UNIT: return "TRANSLATION_UNIT";
    case AST_FUNC_DEF:    return "FUNC_DEF";
    case AST_VAR_DECL:    return "VAR_DECL";
    case AST_TYPEDEF_DECL: return "TYPEDEF_DECL";
    case AST_STRUCT_DECL: return "STRUCT_DECL";
    case AST_ENUM_DECL:   return "ENUM_DECL";
    case AST_PARAM_DECL:  return "PARAM_DECL";
    case AST_COMPOUND:    return "COMPOUND";
    case AST_EXPR_STMT:   return "EXPR_STMT";
    case AST_DECL_STMT:   return "DECL_STMT";
    case AST_IF:          return "IF";
    case AST_WHILE:       return "WHILE";
    case AST_DO_WHILE:    return "DO_WHILE";
    case AST_FOR:         return "FOR";
    case AST_RETURN:      return "RETURN";
    case AST_BREAK:       return "BREAK";
    case AST_CONTINUE:    return "CONTINUE";
    case AST_GOTO:        return "GOTO";
    case AST_LABEL:       return "LABEL";
    case AST_SWITCH:      return "SWITCH";
    case AST_CASE:        return "CASE";
    case AST_DEFAULT:     return "DEFAULT";
    case AST_INT_LIT:     return "INT_LIT";
    case AST_FLOAT_LIT:   return "FLOAT_LIT";
    case AST_CHAR_LIT:    return "CHAR_LIT";
    case AST_STR_LIT:     return "STR_LIT";
    case AST_IDENT:       return "IDENT";
    case AST_UNOP:        return "UNOP";
    case AST_POSTFIX:     return "POSTFIX";
    case AST_BINOP:       return "BINOP";
    case AST_ASSIGN:      return "ASSIGN";
    case AST_TERNARY:     return "TERNARY";
    case AST_CALL:        return "CALL";
    case AST_SUBSCRIPT:   return "SUBSCRIPT";
    case AST_MEMBER:      return "MEMBER";
    case AST_MEMBER_PTR:  return "MEMBER_PTR";
    case AST_CAST:        return "CAST";
    case AST_SIZEOF_EXPR: return "SIZEOF_EXPR";
    case AST_SIZEOF_TYPE: return "SIZEOF_TYPE";
    case AST_COMMA:       return "COMMA";
    case AST_INIT_LIST:   return "INIT_LIST";
    case AST_DESIGNATOR:  return "DESIGNATOR";
    case AST_COMPOUND_LIT: return "COMPOUND_LIT";
    }
    return "?";
}

void ast_print(const ASTNode *node, int indent) {
    if (!node) return;
    print_indent(indent);
    printf("%s", ast_kind_name(node->kind));

    switch (node->kind) {
    case AST_FUNC_DEF: {
        char tbuf[128]; type_print(node->func_def.func_type, tbuf, sizeof(tbuf));
        printf(" %s : %s\n", node->func_def.name, tbuf);
        for (ASTNode *p = node->func_def.params; p; p = p->next)
            ast_print(p, indent + 1);
        ast_print(node->func_def.body, indent + 1);
        break;
    }
    case AST_VAR_DECL: {
        char tbuf[128]; type_print(node->var_decl.decl_type, tbuf, sizeof(tbuf));
        printf(" %s : %s\n", node->var_decl.name, tbuf);
        if (node->var_decl.init) ast_print(node->var_decl.init, indent + 1);
        break;
    }
    case AST_TYPEDEF_DECL: {
        char tbuf[128]; type_print(node->typedef_decl.aliased, tbuf, sizeof(tbuf));
        printf(" %s = %s\n", node->typedef_decl.name, tbuf);
        break;
    }
    case AST_PARAM_DECL: {
        char tbuf[128]; type_print(node->param_decl.param_type, tbuf, sizeof(tbuf));
        printf(" %s : %s\n",
               node->param_decl.name ? node->param_decl.name : "<abstract>", tbuf);
        break;
    }
    case AST_STRUCT_DECL:
    case AST_ENUM_DECL: {
        char tbuf[128]; type_print(node->type_decl.the_type, tbuf, sizeof(tbuf));
        printf(" %s\n", tbuf);
        break;
    }
    case AST_INT_LIT:
        printf(" %llu\n", node->int_lit.val);
        break;
    case AST_CHAR_LIT:
        printf(" '%c' (%llu)\n", (int)node->int_lit.val, node->int_lit.val);
        break;
    case AST_FLOAT_LIT:
        printf(" %g\n", node->float_lit.val);
        break;
    case AST_STR_LIT:
        printf(" \"%s\"\n", node->str_lit.str);
        break;
    case AST_IDENT:
        printf(" %s\n", node->ident.name);
        break;
    case AST_BINOP:
        printf(" %s\n", token_kind_name(node->binop.op));
        ast_print(node->binop.left,  indent + 1);
        ast_print(node->binop.right, indent + 1);
        break;
    case AST_ASSIGN:
        printf(" %s\n", token_kind_name(node->assign.op));
        ast_print(node->assign.left,  indent + 1);
        ast_print(node->assign.right, indent + 1);
        break;
    case AST_UNOP:
        printf(" %s\n", token_kind_name(node->unop.op));
        ast_print(node->unop.operand, indent + 1);
        break;
    case AST_POSTFIX:
        printf(" %s\n", token_kind_name(node->postfix.op));
        ast_print(node->postfix.operand, indent + 1);
        break;
    case AST_MEMBER:
    case AST_MEMBER_PTR:
        printf(" .%s\n", node->member.field);
        ast_print(node->member.base, indent + 1);
        break;
    case AST_GOTO:
        printf(" %s\n", node->go.label);
        break;
    case AST_LABEL:
        printf(" %s\n", node->label.label);
        ast_print(node->label.stmt, indent + 1);
        break;
    case AST_CAST: {
        char tbuf[128]; type_print(node->cast.cast_type, tbuf, sizeof(tbuf));
        printf(" (%s)\n", tbuf);
        ast_print(node->cast.expr, indent + 1);
        break;
    }
    case AST_SIZEOF_TYPE: {
        char tbuf[128]; type_print(node->sizeof_type.sizeof_type, tbuf, sizeof(tbuf));
        printf(" (%s)\n", tbuf);
        break;
    }
    case AST_COMPOUND_LIT: {
        char tbuf[128]; type_print(node->compound_lit.lit_type, tbuf, sizeof(tbuf));
        printf(" (%s)\n", tbuf);
        ast_print(node->compound_lit.init, indent + 1);
        break;
    }
    case AST_DESIGNATOR:
        if (node->designator.is_field)
            printf(" .%s\n", node->designator.field);
        else {
            printf(" [idx]\n");
            ast_print(node->designator.index, indent + 1);
        }
        ast_print(node->designator.value, indent + 1);
        break;
    default:
        printf("\n");
        /* Print children generically */
        switch (node->kind) {
        case AST_COMPOUND:
            for (ASTNode *s = node->compound.stmts; s; s = s->next)
                ast_print(s, indent + 1);
            break;
        case AST_EXPR_STMT: ast_print(node->expr_stmt.expr, indent + 1); break;
        case AST_DECL_STMT: ast_print(node->decl_stmt.decl, indent + 1); break;
        case AST_IF:
            ast_print(node->if_stmt.cond, indent + 1);
            ast_print(node->if_stmt.then_stmt, indent + 1);
            if (node->if_stmt.else_stmt) ast_print(node->if_stmt.else_stmt, indent + 1);
            break;
        case AST_WHILE:
        case AST_DO_WHILE:
            ast_print(node->while_stmt.cond, indent + 1);
            ast_print(node->while_stmt.body, indent + 1);
            break;
        case AST_FOR:
            if (node->for_stmt.init) ast_print(node->for_stmt.init, indent + 1);
            if (node->for_stmt.cond) ast_print(node->for_stmt.cond, indent + 1);
            if (node->for_stmt.incr) ast_print(node->for_stmt.incr, indent + 1);
            ast_print(node->for_stmt.body, indent + 1);
            break;
        case AST_RETURN:
            if (node->ret.expr) ast_print(node->ret.expr, indent + 1);
            break;
        case AST_SWITCH:
            ast_print(node->switch_stmt.expr, indent + 1);
            ast_print(node->switch_stmt.body, indent + 1);
            break;
        case AST_CASE:
            ast_print(node->case_stmt.expr, indent + 1);
            ast_print(node->case_stmt.stmt, indent + 1);
            break;
        case AST_DEFAULT:
            ast_print(node->default_stmt.stmt, indent + 1);
            break;
        case AST_TERNARY:
            ast_print(node->ternary.cond, indent + 1);
            ast_print(node->ternary.then_expr, indent + 1);
            ast_print(node->ternary.else_expr, indent + 1);
            break;
        case AST_CALL:
            ast_print(node->call.func, indent + 1);
            for (ASTNode *a = node->call.args; a; a = a->next)
                ast_print(a, indent + 1);
            break;
        case AST_SUBSCRIPT:
            ast_print(node->subscript.base, indent + 1);
            ast_print(node->subscript.index, indent + 1);
            break;
        case AST_SIZEOF_EXPR:
            ast_print(node->sizeof_expr.expr, indent + 1);
            break;
        case AST_COMMA:
            ast_print(node->comma.left, indent + 1);
            ast_print(node->comma.right, indent + 1);
            break;
        case AST_INIT_LIST:
            for (ASTNode *it = node->init_list.items; it; it = it->next)
                ast_print(it, indent + 1);
            break;
        case AST_TRANSLATION_UNIT:
            for (ASTNode *d = node->tu.decls; d; d = d->next)
                ast_print(d, indent + 1);
            break;
        default: break;
        }
        break;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Scope management
 * ═══════════════════════════════════════════════════════════════════ */

static Scope *scope_new(Arena *arena, Scope *parent) {
    Scope *s = (Scope *)arena_alloc(arena, sizeof(Scope), _Alignof(Scope));
    memset(s, 0, sizeof(*s));
    s->parent = parent;
    return s;
}

static unsigned int sym_hash(const char *name) {
    unsigned int h = 2166136261u;
    for (const char *p = name; *p; p++)
        h = (h ^ (unsigned char)*p) * 16777619u;
    return h & (SCOPE_BUCKETS - 1);
}

static void scope_define_in(Scope *s, Arena *arena,
                             const char *name, SymKind kind,
                             Type *type, ASTNode *decl) {
    unsigned int slot = sym_hash(name);
    Symbol *sym = (Symbol *)arena_alloc(arena, sizeof(Symbol), _Alignof(Symbol));
    sym->name = name;
    sym->kind = kind;
    sym->type = type;
    sym->decl = decl;
    sym->next = s->buckets[slot];
    s->buckets[slot] = sym;
}

static Symbol *scope_lookup_in(Scope *s, const char *name) {
    unsigned int slot = sym_hash(name);
    for (Symbol *sym = s->buckets[slot]; sym; sym = sym->next)
        if (sym->name == name) return sym;  /* interned: pointer equality */
    return NULL;
}

static Symbol *scope_lookup_chain(Scope *s, const char *name) {
    for (; s; s = s->parent) {
        Symbol *found = scope_lookup_in(s, name);
        if (found) return found;
    }
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════
 * Parser init
 * ═══════════════════════════════════════════════════════════════════ */

void parser_init(Parser *p, Lexer *lexer, Arena *arena,
                 StringTable *strings, TypeTable *types) {
    memset(p, 0, sizeof(*p));
    p->lexer   = lexer;
    p->arena   = arena;
    p->strings = strings;
    p->types   = types;
    /* Push global scope */
    p->scope     = scope_new(arena, NULL);
    p->tag_scope = scope_new(arena, NULL);
}

int parser_error_count(const Parser *p) { return p->n_errors; }

/* ═══════════════════════════════════════════════════════════════════
 * Token helpers
 * ═══════════════════════════════════════════════════════════════════ */

static Token peek(Parser *p)    { return lexer_peek(p->lexer); }
static Token advance(Parser *p) { return lexer_next(p->lexer); }

static int check(Parser *p, TokenKind k) { return peek(p).kind == k; }

static int match(Parser *p, TokenKind k) {
    if (peek(p).kind == k) { advance(p); return 1; }
    return 0;
}

static Token expect(Parser *p, TokenKind k) {
    Token t = peek(p);
    if (t.kind == k) return advance(p);
    diag_emit(DIAG_ERROR, t.line, t.col,
              "expected '%s', got '%s'",
              token_kind_name(k), token_describe(&t));
    p->n_errors++;
    if (t.kind != TOK_EOF)
        advance(p);
    /* Return a synthetic token so callers that need a location can still
     * continue after basic error recovery. */
    Token syn;
    memset(&syn, 0, sizeof(syn));
    syn.kind = k;
    syn.line = t.line;
    syn.col  = t.col;
    return syn;
}

/* Intern a token's identifier text into the string table. */
static const char *tok_intern(Parser *p, Token t) {
    return strtab_get(p->strings, t.str_idx);
}

/* ═══════════════════════════════════════════════════════════════════
 * Typedef / type-start disambiguation
 * ═══════════════════════════════════════════════════════════════════ */

static int is_typedef_name(Parser *p, const char *name) {
    Symbol *s = scope_lookup_chain(p->scope, name);
    return s != NULL && s->kind == SYM_TYPEDEF;
}

/* Returns 1 if the current token begins a declaration-specifier. */
static int is_type_start(Parser *p) {
    Token t = peek(p);
    switch (t.kind) {
    case TOK_VOID: case TOK_BOOL: case TOK_CHAR: case TOK_SHORT:
    case TOK_INT: case TOK_LONG: case TOK_FLOAT: case TOK_DOUBLE:
    case TOK_COMPLEX: case TOK_IMAGINARY:
    case TOK_SIGNED: case TOK_UNSIGNED:
    case TOK_STRUCT: case TOK_UNION: case TOK_ENUM:
    case TOK_CONST: case TOK_VOLATILE: case TOK_RESTRICT:
    case TOK_INLINE:
    case TOK_EXTERN: case TOK_STATIC: case TOK_AUTO:
    case TOK_REGISTER: case TOK_TYPEDEF:
    case TOK_BUILTIN_VA_LIST: case TOK_TYPEOF:
    case TOK_ATTRIBUTE: case TOK_ASM: case TOK_EXTENSION:
    case TOK_NONNULL: case TOK_NORETURN: case TOK_NULLABLE:
        return 1;
    case TOK_IDENT: {
        const char *name = strtab_get(p->strings, t.str_idx);
        return is_typedef_name(p, name);
    }
    default: return 0;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Type specifier accumulation
 * ═══════════════════════════════════════════════════════════════════ */

typedef enum {
    TS_VOID      = 1 << 0,
    TS_BOOL      = 1 << 1,
    TS_CHAR      = 1 << 2,
    TS_SHORT     = 1 << 3,
    TS_INT       = 1 << 4,
    TS_LONG      = 1 << 5,
    TS_LONG2     = 1 << 6,  /* second 'long' (long long) */
    TS_FLOAT     = 1 << 7,
    TS_DOUBLE    = 1 << 8,
    TS_SIGNED    = 1 << 9,
    TS_UNSIGNED  = 1 << 10,
    TS_NAMED     = 1 << 11, /* struct/union/enum/typedef */
    TS_COMPLEX   = 1 << 12,
    TS_IMAGINARY = 1 << 13,
} TypeSpecFlag;

typedef struct {
    StorageClass storage;
    FuncSpec     func_spec;
    TypeQual     qual;
    Type        *base;       /* non-NULL when TS_NAMED */
    unsigned int spec_flags; /* TypeSpecFlag bits */
} DeclSpec;

static Type *resolve_type_spec(Parser *p, DeclSpec *ds, Token loc) {
    if (ds->base) return ds->base;  /* struct/union/enum/typedef */

    unsigned int f = ds->spec_flags;
    unsigned int scalar_f = f & ~(TS_COMPLEX | TS_IMAGINARY);
    int has_sign  = (f & (TS_SIGNED | TS_UNSIGNED)) != 0;
    int is_uns    = (f & TS_UNSIGNED) != 0;

    if (scalar_f == 0 && (f & (TS_COMPLEX | TS_IMAGINARY)))
        return type_double();

    if (scalar_f & TS_VOID)   return type_void();
    if (scalar_f & TS_BOOL)   return type_bool();

    if (scalar_f & TS_FLOAT)  return type_float();
    if (scalar_f & TS_DOUBLE) {
        if (scalar_f & TS_LONG) return type_ldouble();
        return type_double();
    }
    if (scalar_f & TS_CHAR)   return is_uns ? type_uchar()
                             : (has_sign ? type_schar() : type_char());
    if (scalar_f & TS_SHORT)  return is_uns ? type_ushort() : type_short();
    if (scalar_f & TS_LONG2)  return is_uns ? type_ullong() : type_llong();
    if (scalar_f & TS_LONG)   return is_uns ? type_ulong()  : type_long();
    /* Default: int (with optional sign) */
    if (scalar_f == 0 || scalar_f == TS_SIGNED) return type_int();
    if (scalar_f == TS_UNSIGNED)         return type_uint();
    if (scalar_f == (TS_INT | TS_SIGNED) || scalar_f == TS_INT) return type_int();
    if (scalar_f == (TS_INT | TS_UNSIGNED)) return type_uint();

    diag_emit(DIAG_ERROR, loc.line, loc.col, "invalid type specifier combination");
    p->n_errors++;
    return type_int();
}

/* Forward declarations for mutually recursive parse functions */
static int      tok_lbp(TokenKind k);
static ASTNode *nud(Parser *p, Token t);
static ASTNode *led(Parser *p, ASTNode *left, Token t);
static ASTNode *parse_statement(Parser *p);
static ASTNode *parse_expr(Parser *p, int min_bp);
static DeclSpec parse_decl_spec(Parser *p);
static ASTNode *parse_decl_list(Parser *p, DeclSpec *ds, int top_level);

typedef struct {
    const char *name;
    int         line;
    int         col;
} DeclName;

static DeclName parse_declarator(Parser *p, Type *base, Type **out_type);
static DeclName parse_abstract_declarator(Parser *p, Type *base, Type **out_type);

/* ── Extensions skipping ─────────────────────────────────────────── */

static void skip_attributes(Parser *p) {
    while (check(p, TOK_ATTRIBUTE) || check(p, TOK_ASM) || check(p, TOK_EXTENSION) ||
           check(p, TOK_NONNULL) || check(p, TOK_NORETURN) || check(p, TOK_NULLABLE)) {
        advance(p);
        if (match(p, TOK_LPAREN)) {
            int depth = 1;
            while (depth > 0 && !check(p, TOK_EOF)) {
                if (check(p, TOK_LPAREN)) depth++;
                else if (check(p, TOK_RPAREN)) depth--;
                advance(p);
            }
        }
    }
}

/* ── Struct / union parsing ──────────────────────────────────────── */

static Type *parse_struct_union(Parser *p, int is_union) {
    Token kw_tok = advance(p);  /* consume 'struct' or 'union' */
    (void)kw_tok;
    const char *tag = NULL;

    if (check(p, TOK_IDENT)) {
        Token t = advance(p);
        tag = tok_intern(p, t);
    }

    /* Look up existing tag */
    Type *ty = NULL;
    if (tag) {
        Symbol *existing = scope_lookup_chain(p->tag_scope, tag);
        if (existing &&
            existing->kind == (is_union ? SYM_UNION_TAG : SYM_STRUCT_TAG)) {
            ty = existing->type;
        }
    }
    if (!ty) {
        ty = is_union ? type_union(p->types, tag) : type_struct(p->types, tag);
        if (tag) {
            scope_define_in(p->tag_scope, p->arena, tag,
                            is_union ? SYM_UNION_TAG : SYM_STRUCT_TAG, ty, NULL);
        }
    }

    if (!check(p, TOK_LBRACE)) return ty;  /* forward reference */

    advance(p); /* { */

    StructField *head = NULL, *tail = NULL;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        DeclSpec ds = parse_decl_spec(p);
        Type *base = resolve_type_spec(p, &ds, peek(p));
        if (ds.qual != QUAL_NONE) base = type_qualify(p->types, base, ds.qual);

        /* One or more declarators */
        do {
            if (check(p, TOK_SEMICOLON)) {
                if (base && (base->kind == TY_STRUCT || base->kind == TY_UNION)) {
                    StructField *f = (StructField *)arena_alloc(
                        p->arena, sizeof(StructField), _Alignof(StructField));
                    memset(f, 0, sizeof(*f));
                    f->name = NULL;
                    f->type = base;
                    f->offset = 0;
                    f->bit_width = -1;
                    f->bit_offset = 0;
                    f->next = NULL;
                    if (!head) { head = tail = f; }
                    else       { tail->next = f; tail = f; }
                }
                break;
            }

            Type *mem_type = NULL;
            DeclName dn = parse_declarator(p, base, &mem_type);
            const char *fname = dn.name;

            int bit_width = -1;
            if (match(p, TOK_COLON)) {
                ASTNode *bw = parse_expr(p, 10);
                bit_width = bw && bw->kind == AST_INT_LIT ? (int)bw->int_lit.val : 0;
            }

            StructField *f = (StructField *)arena_alloc(
                p->arena, sizeof(StructField), _Alignof(StructField));
            memset(f, 0, sizeof(*f));
            f->name = fname;
            f->type = mem_type;
            f->offset = 0;
            f->bit_width = bit_width;
            f->bit_offset = 0;
            f->next = NULL;

            if (!head) { head = tail = f; }
            else       { tail->next = f; tail = f; }

        } while (match(p, TOK_COMMA));

        expect(p, TOK_SEMICOLON);
    }

    expect(p, TOK_RBRACE);
    skip_attributes(p);

    ty->agg.fields = head;
    if (is_union) type_seal_union(ty);
    else          type_seal_struct(ty);

    return ty;
}

/* ── Enum parsing ────────────────────────────────────────────────── */

static long long parser_const_eval(ASTNode *expr) {
    if (!expr) return 0;
    switch (expr->kind) {
    case AST_INT_LIT:
    case AST_CHAR_LIT:
        return (long long)expr->int_lit.val;
    case AST_UNOP:
        switch (expr->unop.op) {
        case TOK_MINUS: return -parser_const_eval(expr->unop.operand);
        case TOK_PLUS:  return parser_const_eval(expr->unop.operand);
        case TOK_TILDE: return ~parser_const_eval(expr->unop.operand);
        case TOK_BANG:  return !parser_const_eval(expr->unop.operand);
        default: return parser_const_eval(expr->unop.operand);
        }
    case AST_BINOP: {
        long long l = parser_const_eval(expr->binop.left);
        long long r = parser_const_eval(expr->binop.right);
        switch (expr->binop.op) {
        case TOK_PLUS:   return l + r;
        case TOK_MINUS:  return l - r;
        case TOK_STAR:   return l * r;
        case TOK_SLASH:  return r ? l / r : 0;
        case TOK_PERCENT:return r ? l % r : 0;
        case TOK_LSHIFT: return l << r;
        case TOK_RSHIFT: return l >> r;
        case TOK_AMP:    return l & r;
        case TOK_PIPE:   return l | r;
        case TOK_CARET:  return l ^ r;
        default: return 0;
        }
    }
    default:
        return 0;
    }
}

static Type *parse_enum(Parser *p) {
    advance(p); /* consume 'enum' */
    const char *tag = NULL;

    if (check(p, TOK_IDENT)) {
        Token t = advance(p);
        tag = tok_intern(p, t);
    }

    Type *ty = NULL;
    if (tag) {
        Symbol *existing = scope_lookup_chain(p->tag_scope, tag);
        if (existing && existing->kind == SYM_ENUM_TAG)
            ty = existing->type;
    }
    if (!ty) {
        ty = type_enum(p->types, tag);
        if (tag)
            scope_define_in(p->tag_scope, p->arena, tag, SYM_ENUM_TAG, ty, NULL);
    }

    if (!check(p, TOK_LBRACE)) return ty;

    advance(p); /* { */

    EnumConst *head = NULL, *tail = NULL;
    long long next_val = 0;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        if (!check(p, TOK_IDENT)) {
            diag_emit(DIAG_ERROR, peek(p).line, peek(p).col,
                      "expected enumerator name");
            p->n_errors++;
            advance(p);
            continue;
        }
        Token name_tok = advance(p);
        const char *cname = tok_intern(p, name_tok);
        long long val = next_val;

        if (match(p, TOK_ASSIGN)) {
            ASTNode *expr = parse_expr(p, 10); /* stop before enumerator comma */
            val = parser_const_eval(expr);
        }
        next_val = val + 1;

        EnumConst *ec = (EnumConst *)arena_alloc(
            p->arena, sizeof(EnumConst), _Alignof(EnumConst));
        ec->name = cname;
        ec->value = val;
        ec->next = NULL;
        if (!head) { head = tail = ec; } else { tail->next = ec; tail = ec; }

        /* Expose enum constant as a symbol */
        scope_define_in(p->scope, p->arena, cname, SYM_ENUM_CONST, type_int(), NULL);

        if (!match(p, TOK_COMMA)) break;
    }

    expect(p, TOK_RBRACE);
    skip_attributes(p);

    ty->enm.consts = head;
    ty->enm.complete = 1;
    return ty;
}

/* ═══════════════════════════════════════════════════════════════════
 * Declaration-specifier parsing
 * ═══════════════════════════════════════════════════════════════════ */

static DeclSpec parse_decl_spec(Parser *p) {
    DeclSpec ds;
    memset(&ds, 0, sizeof(ds));
    int seen_type = 0;

    for (;;) {
        Token t = peek(p);
        switch (t.kind) {
        /* Storage classes */
        case TOK_AUTO:
            if (ds.storage && ds.storage != SC_AUTO) goto sc_conflict;
            ds.storage = SC_AUTO; advance(p); break;
        case TOK_STATIC:
            if (ds.storage && ds.storage != SC_STATIC) goto sc_conflict;
            ds.storage = SC_STATIC; advance(p); break;
        case TOK_EXTERN:
            if (ds.storage && ds.storage != SC_EXTERN) goto sc_conflict;
            ds.storage = SC_EXTERN; advance(p); break;
        case TOK_REGISTER:
            if (ds.storage && ds.storage != SC_REGISTER) goto sc_conflict;
            ds.storage = SC_REGISTER; advance(p); break;
        case TOK_TYPEDEF:
            if (ds.storage && ds.storage != SC_TYPEDEF) goto sc_conflict;
            ds.storage = SC_TYPEDEF; advance(p); break;
        /* Function specifier */
        case TOK_INLINE:
            ds.func_spec = FS_INLINE; advance(p); break;
        /* Qualifiers */
        case TOK_CONST:    ds.qual = (TypeQual)(ds.qual | QUAL_CONST);    advance(p); break;
        case TOK_VOLATILE: ds.qual = (TypeQual)(ds.qual | QUAL_VOLATILE); advance(p); break;
        case TOK_RESTRICT: ds.qual = (TypeQual)(ds.qual | QUAL_RESTRICT); advance(p); break;
        /* Type specifiers */
        case TOK_VOID:
            if (seen_type) goto dup_type;
            ds.spec_flags |= TS_VOID; seen_type = 1; advance(p); break;
        case TOK_BOOL:
            if (seen_type) goto dup_type;
            ds.spec_flags |= TS_BOOL; seen_type = 1; advance(p); break;
        case TOK_CHAR:
            if (ds.spec_flags & TS_CHAR) goto dup_type;
            ds.spec_flags |= TS_CHAR; advance(p); break;
        case TOK_SHORT:
            if (ds.spec_flags & TS_SHORT) goto dup_type;
            ds.spec_flags |= TS_SHORT; advance(p); break;
        case TOK_INT:
            if (ds.spec_flags & TS_INT) goto dup_type;
            ds.spec_flags |= TS_INT; advance(p); break;
        case TOK_LONG:
            if (ds.spec_flags & TS_LONG2) goto dup_type;
            if (ds.spec_flags & TS_LONG)  ds.spec_flags |= TS_LONG2;
            else                          ds.spec_flags |= TS_LONG;
            advance(p); break;
        case TOK_FLOAT:
            if (seen_type) goto dup_type;
            ds.spec_flags |= TS_FLOAT; seen_type = 1; advance(p); break;
        case TOK_DOUBLE:
            if (ds.spec_flags & TS_DOUBLE) goto dup_type;
            ds.spec_flags |= TS_DOUBLE; advance(p); break;
        case TOK_COMPLEX:
            if (ds.spec_flags & TS_COMPLEX) goto dup_type;
            ds.spec_flags |= TS_COMPLEX; advance(p); break;
        case TOK_IMAGINARY:
            if (ds.spec_flags & TS_IMAGINARY) goto dup_type;
            ds.spec_flags |= TS_IMAGINARY; advance(p); break;
        case TOK_SIGNED:
            if (ds.spec_flags & TS_SIGNED) goto dup_type;
            ds.spec_flags |= TS_SIGNED; advance(p); break;
        case TOK_UNSIGNED:
            if (ds.spec_flags & TS_UNSIGNED) goto dup_type;
            ds.spec_flags |= TS_UNSIGNED; advance(p); break;
        case TOK_STRUCT:
        case TOK_UNION:
            if (seen_type) goto dup_type;
            ds.base = parse_struct_union(p, t.kind == TOK_UNION);
            seen_type = 1; break;
        case TOK_ENUM:
            if (seen_type) goto dup_type;
            ds.base = parse_enum(p);
            seen_type = 1; break;
        case TOK_ATTRIBUTE:
        case TOK_ASM:
        case TOK_EXTENSION:
        case TOK_NONNULL:
        case TOK_NORETURN:
        case TOK_NULLABLE:
            skip_attributes(p);
            continue;
        case TOK_BUILTIN_VA_LIST:
            if (seen_type) goto dup_type;
            ds.base = type_pointer(p->types, type_void(), QUAL_NONE);
            seen_type = 1; advance(p); break;
        case TOK_TYPEOF:
            if (seen_type) goto dup_type;
            advance(p);
            if (match(p, TOK_LPAREN)) {
                int depth = 1;
                while (depth > 0 && !check(p, TOK_EOF)) {
                    if (check(p, TOK_LPAREN)) depth++;
                    else if (check(p, TOK_RPAREN)) depth--;
                    advance(p);
                }
            }
            ds.base = type_int();
            seen_type = 1; break;
        case TOK_IDENT: {
            const char *name = strtab_get(p->strings, t.str_idx);
            if (!seen_type && is_typedef_name(p, name)) {
                Symbol *sym = scope_lookup_chain(p->scope, name);
                ds.base = sym->type;
                seen_type = 1;
                advance(p);
                break;
            }
            goto done;
        }
        default:
            goto done;
        }
        continue;
sc_conflict:
        diag_emit(DIAG_ERROR, t.line, t.col, "multiple storage classes");
        p->n_errors++;
        advance(p);
        continue;
dup_type:
        diag_emit(DIAG_ERROR, t.line, t.col, "duplicate type specifier");
        p->n_errors++;
        advance(p);
        continue;
    }
done:
    return ds;
}

/* ═══════════════════════════════════════════════════════════════════
 * Declarator parsing
 *
 * Builds a Type by wrapping the base type inside-out.
 * Returns the declared name (or NULL for abstract declarators).
 * ═══════════════════════════════════════════════════════════════════ */

/* Parse the suffix part of a declarator: [] and () modifiers applied
 * to `inner_type`. Returns the resulting type. */
static Type *parse_declarator_suffix(Parser *p, Type *inner) {
    for (;;) {
        if (check(p, TOK_LBRACKET)) {
            advance(p);
            if (check(p, TOK_RBRACKET)) {
                advance(p);
                inner = type_array_unsized(p->types, inner);
            } else {
                ASTNode *sz = parse_expr(p, 0);
                long long count = 0;
                if (sz && sz->kind == AST_INT_LIT) count = (long long)sz->int_lit.val;
                expect(p, TOK_RBRACKET);
                inner = type_array(p->types, inner, count);
            }
        } else if (check(p, TOK_LPAREN)) {
            /* Function declarator */
            advance(p); /* ( */

            FuncParam *phead = NULL, *ptail = NULL;
            int n_params = 0;
            int variadic = 0;

            if (!check(p, TOK_RPAREN)) {
                do {
                    if (check(p, TOK_ELLIPSIS)) { advance(p); variadic = 1; break; }
                    DeclSpec pds = parse_decl_spec(p);
                    Type *pbase = resolve_type_spec(p, &pds, peek(p));
                    if (pds.qual) pbase = type_qualify(p->types, pbase, pds.qual);
                    Type *ptype;
                    DeclName dn = parse_declarator(p, pbase, &ptype);
                    FuncParam *fp = (FuncParam *)arena_alloc(
                        p->arena, sizeof(FuncParam), _Alignof(FuncParam));
                    fp->name = dn.name; fp->type = ptype; fp->next = NULL;
                    if (!phead) { phead = ptail = fp; }
                    else        { ptail->next = fp; ptail = fp; }
                    n_params++;
                } while (match(p, TOK_COMMA));
            }

            if (n_params == 1 && phead->type->kind == TY_VOID && phead->name == NULL) {
                phead = NULL;
                n_params = 0;
            }

            expect(p, TOK_RPAREN);
            inner = type_func(p->types, inner, phead, n_params, variadic);
        } else {
            break;
        }
    }
    return inner;
}

static DeclName parse_declarator(Parser *p, Type *base, Type **out_type) {
    skip_attributes(p);
    /* Collect leading pointer stars */
    TypeQual ptr_quals[16];
    int n_stars = 0;
    while ((check(p, TOK_STAR) || check(p, TOK_CARET)) && n_stars < 16) {
        advance(p);
        TypeQual q = QUAL_NONE;
        for (;;) {
            if (check(p, TOK_CONST))       { q = (TypeQual)(q | QUAL_CONST);    advance(p); }
            else if (check(p, TOK_VOLATILE)) { q = (TypeQual)(q | QUAL_VOLATILE); advance(p); }
            else if (check(p, TOK_RESTRICT)) { q = (TypeQual)(q | QUAL_RESTRICT); advance(p); }
            else if (check(p, TOK_ATTRIBUTE) || check(p, TOK_ASM) || check(p, TOK_NONNULL) || check(p, TOK_NULLABLE)) {
                skip_attributes(p);
            }
            else break;
        }
        ptr_quals[n_stars++] = q;
    }
    skip_attributes(p);

    Type *starred_base = base;
    for (int i = n_stars - 1; i >= 0; i--)
        starred_base = type_pointer(p->types, starred_base, ptr_quals[i]);

    DeclName result;
    memset(&result, 0, sizeof(result));
    Type *inner = starred_base;

    if (check(p, TOK_LPAREN) && !is_type_start(p)) {
        /* Grouped declarator: int (*fp)(int) */
        advance(p); /* ( */
        /* We need to defer the inner type. Trick: parse with a placeholder,
         * then fill in after parsing the suffix. Use a local Type* that we
         * point the inner parse to. We'll substitute it in a two-pass way:
         * allocate a mutable pointer slot in the arena. */
        Type *placeholder = NULL;
        Type **hole = &placeholder;

        /* Recursively parse the inner declarator with a NULL base.
         * We'll come back and set the base once we know the suffix type. */
        result = parse_declarator(p, NULL, &inner);
        expect(p, TOK_RPAREN);

        /* Now parse the suffix on the outer level to get the actual base */
        Type *suffix_type = parse_declarator_suffix(p, starred_base);

        /* The inner declarator's type chain has `hole` pointing to where
         * `base` would go. We need to substitute `suffix_type` in.
         * Since inner was built with NULL as base and is either a pointer
         * chain or a function/array suffix, we need to patch the deepest
         * base. Walk to find it. */
        if (inner == NULL) {
            inner = suffix_type;
        } else {
            /* Walk the pointer chain to find where NULL base was left */
            Type *cur = inner;
            while (cur) {
                if (cur->kind == TY_POINTER && cur->ptr.pointee == NULL) {
                    cur->ptr.pointee = suffix_type;
                    break;
                }
                /* For function and array types built on the null base */
                if (cur->kind == TY_FUNC && cur->func.ret == NULL) {
                    cur->func.ret = suffix_type;
                    break;
                }
                if ((cur->kind == TY_ARRAY || cur->kind == TY_ARRAY_UNSIZED)
                    && cur->arr.elem == NULL) {
                    cur->arr.elem = suffix_type;
                    break;
                }
                /* Couldn't find a NULL slot — shouldn't happen in valid code */
                break;
            }
            if (cur == NULL) inner = suffix_type;
        }
        (void)hole;
    } else if (check(p, TOK_IDENT)) {
        Token t = advance(p);
        result.name = tok_intern(p, t);
        result.line = t.line;
        result.col  = t.col;
        inner = parse_declarator_suffix(p, starred_base);
    } else {
        /* Abstract declarator (no name) */
        inner = parse_declarator_suffix(p, starred_base);
    }

    *out_type = inner;
    skip_attributes(p);
    return result;
}

static DeclName parse_abstract_declarator(Parser *p, Type *base, Type **out_type) {
    /* Same as parse_declarator but never expects a name. */
    return parse_declarator(p, base, out_type);
}

/* ═══════════════════════════════════════════════════════════════════
 * Initializer parsing
 * ═══════════════════════════════════════════════════════════════════ */

static ASTNode *parse_initializer(Parser *p);

static ASTNode *parse_init_list(Parser *p) {
    Token lb = expect(p, TOK_LBRACE);
    ASTNode *node = ast_alloc(p->arena, AST_INIT_LIST, lb.line, lb.col);
    ASTNode *head = NULL, *tail = NULL;
    int count = 0;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        ASTNode *item;

        /* Check for designator */
        if (check(p, TOK_DOT) || check(p, TOK_LBRACKET)) {
            Token dt = peek(p);
            ASTNode *des = ast_alloc(p->arena, AST_DESIGNATOR, dt.line, dt.col);
            if (check(p, TOK_DOT)) {
                advance(p);
                Token ft = expect(p, TOK_IDENT);
                des->designator.is_field = 1;
                des->designator.field = tok_intern(p, ft);
            } else {
                advance(p); /* [ */
                des->designator.is_field = 0;
                des->designator.index = parse_expr(p, 0);
                expect(p, TOK_RBRACKET);
            }
            expect(p, TOK_ASSIGN);
            des->designator.value = parse_initializer(p);
            item = des;
        } else {
            item = parse_initializer(p);
        }

        ast_list_append(&head, &tail, item);
        count++;
        if (!match(p, TOK_COMMA)) break;
    }

    expect(p, TOK_RBRACE);
    node->init_list.items = head;
    node->init_list.n_items = count;
    return node;
}

static ASTNode *parse_initializer(Parser *p) {
    if (check(p, TOK_LBRACE)) return parse_init_list(p);
    return parse_expr(p, 20); /* above assignment level */
}

/* ═══════════════════════════════════════════════════════════════════
 * Expression parsing — Pratt algorithm
 * ═══════════════════════════════════════════════════════════════════ */

/* Left binding powers (larger = binds tighter):
 *   comma=10, assign=20(right-assoc), ternary=30(right-assoc),
 *   logical-or=40, logical-and=50, bit-or=60, bit-xor=70, bit-and=80,
 *   eq/ne=90, relational=100, shift=110,
 *   additive=120, multiplicative=130,
 *   prefix-unary=140 (handled in nud), postfix/call/subscript/member=150
 */
static int tok_lbp(TokenKind k) {
    switch (k) {
    case TOK_COMMA:         return 10;
    case TOK_ASSIGN:
    case TOK_PLUS_ASSIGN:
    case TOK_MINUS_ASSIGN:
    case TOK_STAR_ASSIGN:
    case TOK_SLASH_ASSIGN:
    case TOK_PERCENT_ASSIGN:
    case TOK_AMP_ASSIGN:
    case TOK_PIPE_ASSIGN:
    case TOK_CARET_ASSIGN:
    case TOK_LSHIFT_ASSIGN:
    case TOK_RSHIFT_ASSIGN: return 20;
    case TOK_QUESTION:      return 30;
    case TOK_OR:            return 40;
    case TOK_AND:           return 50;
    case TOK_PIPE:          return 60;
    case TOK_CARET:         return 70;
    case TOK_AMP:           return 80;
    case TOK_EQ:
    case TOK_NEQ:           return 90;
    case TOK_LT:
    case TOK_LE:
    case TOK_GT:
    case TOK_GE:            return 100;
    case TOK_LSHIFT:
    case TOK_RSHIFT:        return 110;
    case TOK_PLUS:
    case TOK_MINUS:         return 120;
    case TOK_STAR:
    case TOK_SLASH:
    case TOK_PERCENT:       return 130;
    case TOK_LPAREN:
    case TOK_LBRACKET:
    case TOK_DOT:
    case TOK_ARROW:
    case TOK_PLUSPLUS:
    case TOK_MINUSMINUS:    return 150;
    default:                return 0;
    }
}

static ASTNode *nud(Parser *p, Token t);
static ASTNode *led(Parser *p, ASTNode *left, Token t);

static ASTNode *parse_expr(Parser *p, int min_bp) {
    Token t = advance(p);
    ASTNode *left = nud(p, t);
    for (;;) {
        Token op = peek(p);
        int bp = tok_lbp(op.kind);
        if (bp <= min_bp) break;
        advance(p);
        left = led(p, left, op);
    }
    return left;
}


static ASTNode *nud(Parser *p, Token t) {
    ASTNode *n;
    switch (t.kind) {
    /* Literals */
    case TOK_INT_LIT:
        n = ast_alloc(p->arena, AST_INT_LIT, t.line, t.col);
        n->int_lit.val    = t.int_val;
        n->int_lit.suffix = t.int_suffix;
        return n;
    case TOK_FLOAT_LIT:
        n = ast_alloc(p->arena, AST_FLOAT_LIT, t.line, t.col);
        n->float_lit.val    = t.float_val;
        n->float_lit.suffix = t.float_suffix;
        return n;
    case TOK_CHAR_LIT:
        n = ast_alloc(p->arena, AST_CHAR_LIT, t.line, t.col);
        n->int_lit.val    = t.int_val;
        n->int_lit.is_wide = t.is_wide;
        return n;
    case TOK_STR_LIT:
        n = ast_alloc(p->arena, AST_STR_LIT, t.line, t.col);
        n->str_lit.str    = strtab_get(p->strings, t.str_idx);
        n->str_lit.is_wide = t.is_wide;
        return n;
    case TOK_IDENT: {
        n = ast_alloc(p->arena, AST_IDENT, t.line, t.col);
        n->ident.name = tok_intern(p, t);
        return n;
    }

    /* Prefix unary */
    case TOK_MINUS:
    case TOK_BANG:
    case TOK_TILDE:
    case TOK_AMP:
    case TOK_STAR:
        n = ast_alloc(p->arena, AST_UNOP, t.line, t.col);
        n->unop.op      = t.kind;
        n->unop.operand = parse_expr(p, 140);
        return n;
    case TOK_PLUS:
        /* Unary plus — just return the operand */
        return parse_expr(p, 140);
    case TOK_PLUSPLUS:
    case TOK_MINUSMINUS:
        n = ast_alloc(p->arena, AST_UNOP, t.line, t.col);
        n->unop.op      = t.kind;
        n->unop.operand = parse_expr(p, 140);
        return n;

    case TOK_ALIGNOF: {
        expect(p, TOK_LPAREN);
        DeclSpec ds = parse_decl_spec(p);
        Type *base = resolve_type_spec(p, &ds, peek(p));
        if (ds.qual != QUAL_NONE) base = type_qualify(p->types, base, ds.qual);
        Type *ty = NULL;
        parse_abstract_declarator(p, base, &ty);
        expect(p, TOK_RPAREN);
        n = ast_alloc(p->arena, AST_INT_LIT, t.line, t.col);
        n->int_lit.val = type_alignof(ty);
        n->int_lit.suffix = INT_SUFFIX_U | INT_SUFFIX_L;
        return n;
    }

    /* sizeof */
    case TOK_SIZEOF:
        if (check(p, TOK_LPAREN)) {
            advance(p); /* consume ( */
            if (is_type_start(p)) {
                /* sizeof(type) */
                DeclSpec ds = parse_decl_spec(p);
                Type *base = resolve_type_spec(p, &ds, peek(p));
                if (ds.qual) base = type_qualify(p->types, base, ds.qual);
                Type *ty;
                parse_abstract_declarator(p, base, &ty);
                expect(p, TOK_RPAREN);
                n = ast_alloc(p->arena, AST_SIZEOF_TYPE, t.line, t.col);
                n->sizeof_type.sizeof_type = ty;
                return n;
            } else {
                /* sizeof(expr) */
                ASTNode *inner = parse_expr(p, 0);
                expect(p, TOK_RPAREN);
                n = ast_alloc(p->arena, AST_SIZEOF_EXPR, t.line, t.col);
                n->sizeof_expr.expr = inner;
                return n;
            }
        }
        /* sizeof without parens: sizeof expr */
        n = ast_alloc(p->arena, AST_SIZEOF_EXPR, t.line, t.col);
        n->sizeof_expr.expr = parse_expr(p, 140);
        return n;

    /* Parenthesized expr, cast, or compound literal */
    case TOK_LPAREN:
        if (is_type_start(p)) {
            DeclSpec ds = parse_decl_spec(p);
            Type *base = resolve_type_spec(p, &ds, peek(p));
            if (ds.qual) base = type_qualify(p->types, base, ds.qual);
            Type *cast_type;
            parse_abstract_declarator(p, base, &cast_type);
            expect(p, TOK_RPAREN);

            if (check(p, TOK_LBRACE)) {
                /* Compound literal: (Type){ ... } */
                ASTNode *il = parse_init_list(p);
                n = ast_alloc(p->arena, AST_COMPOUND_LIT, t.line, t.col);
                n->compound_lit.lit_type = cast_type;
                n->compound_lit.init = il;
                return n;
            }
            /* Cast expression */
            n = ast_alloc(p->arena, AST_CAST, t.line, t.col);
            n->cast.cast_type = cast_type;
            n->cast.expr      = parse_expr(p, 140);
            return n;
        }
        {
            ASTNode *inner = parse_expr(p, 0);
            expect(p, TOK_RPAREN);
            return inner;
        }

    default:
        /* Unexpected token at the start of an expression */
        diag_emit(DIAG_ERROR, t.line, t.col,
                  "unexpected token '%s' in expression", token_describe(&t));
        p->n_errors++;
        /* Return a placeholder INT_LIT 0 for error recovery */
        n = ast_alloc(p->arena, AST_INT_LIT, t.line, t.col);
        return n;
    }
}

static ASTNode *led(Parser *p, ASTNode *left, Token t) {
    ASTNode *n;
    switch (t.kind) {
    /* Binary arithmetic / bitwise / comparison / logical */
    case TOK_PLUS: case TOK_MINUS:
    case TOK_STAR: case TOK_SLASH: case TOK_PERCENT:
    case TOK_AMP:  case TOK_PIPE:  case TOK_CARET:
    case TOK_AND:  case TOK_OR:
    case TOK_EQ:   case TOK_NEQ:
    case TOK_LT:   case TOK_LE:    case TOK_GT: case TOK_GE:
    case TOK_LSHIFT: case TOK_RSHIFT:
        n = ast_alloc(p->arena, AST_BINOP, t.line, t.col);
        n->binop.op    = t.kind;
        n->binop.left  = left;
        n->binop.right = parse_expr(p, tok_lbp(t.kind));  /* left-assoc */
        return n;

    /* Assignment — right-associative */
    case TOK_ASSIGN:
    case TOK_PLUS_ASSIGN:  case TOK_MINUS_ASSIGN:
    case TOK_STAR_ASSIGN:  case TOK_SLASH_ASSIGN:  case TOK_PERCENT_ASSIGN:
    case TOK_AMP_ASSIGN:   case TOK_PIPE_ASSIGN:   case TOK_CARET_ASSIGN:
    case TOK_LSHIFT_ASSIGN: case TOK_RSHIFT_ASSIGN:
        n = ast_alloc(p->arena, AST_ASSIGN, t.line, t.col);
        n->assign.op    = t.kind;
        n->assign.left  = left;
        n->assign.right = parse_expr(p, tok_lbp(t.kind) - 1);  /* right-assoc */
        return n;

    /* Ternary — right-associative */
    case TOK_QUESTION: {
        ASTNode *then_expr = parse_expr(p, 0);
        expect(p, TOK_COLON);
        ASTNode *else_expr = parse_expr(p, tok_lbp(TOK_QUESTION) - 1);
        n = ast_alloc(p->arena, AST_TERNARY, t.line, t.col);
        n->ternary.cond      = left;
        n->ternary.then_expr = then_expr;
        n->ternary.else_expr = else_expr;
        return n;
    }

    /* Function call */
    case TOK_LPAREN: {
        n = ast_alloc(p->arena, AST_CALL, t.line, t.col);
        n->call.func = left;
        ASTNode *ahead = NULL, *atail = NULL;
        int argc = 0;
        if (!check(p, TOK_RPAREN)) {
            do {
                /* Parse at above-comma level so comma separates args */
                ASTNode *arg = parse_expr(p, tok_lbp(TOK_COMMA));
                ast_list_append(&ahead, &atail, arg);
                argc++;
            } while (match(p, TOK_COMMA));
        }
        expect(p, TOK_RPAREN);
        n->call.args   = ahead;
        n->call.n_args = argc;
        return n;
    }

    /* Subscript */
    case TOK_LBRACKET:
        n = ast_alloc(p->arena, AST_SUBSCRIPT, t.line, t.col);
        n->subscript.base  = left;
        n->subscript.index = parse_expr(p, 0);
        expect(p, TOK_RBRACKET);
        return n;

    /* Member access */
    case TOK_DOT:
    case TOK_ARROW: {
        Token ft = expect(p, TOK_IDENT);
        n = ast_alloc(p->arena,
                      t.kind == TOK_DOT ? AST_MEMBER : AST_MEMBER_PTR,
                      t.line, t.col);
        n->member.base  = left;
        n->member.field = tok_intern(p, ft);
        return n;
    }

    /* Postfix ++/-- */
    case TOK_PLUSPLUS:
    case TOK_MINUSMINUS:
        n = ast_alloc(p->arena, AST_POSTFIX, t.line, t.col);
        n->postfix.op      = t.kind;
        n->postfix.operand = left;
        return n;

    /* Comma operator */
    case TOK_COMMA:
        n = ast_alloc(p->arena, AST_COMMA, t.line, t.col);
        n->comma.left  = left;
        n->comma.right = parse_expr(p, tok_lbp(TOK_COMMA));
        return n;

    default:
        diag_emit(DIAG_ERROR, t.line, t.col,
                  "unexpected infix token '%s'", token_describe(&t));
        p->n_errors++;
        return left;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Statement parsing
 * ═══════════════════════════════════════════════════════════════════ */

static ASTNode *parse_compound(Parser *p);

static void skip_pragma(Parser *p) {
    advance(p); /* _Pragma */
    if (match(p, TOK_LPAREN)) {
        if (check(p, TOK_STR_LIT))
            advance(p);
        expect(p, TOK_RPAREN);
    }
    match(p, TOK_SEMICOLON);
}

static ASTNode *parse_statement(Parser *p) {
    Token t = peek(p);

    if (check(p, TOK_PRAGMA)) {
        skip_pragma(p);
        return ast_alloc(p->arena, AST_COMPOUND, t.line, t.col);
    }

    if (is_type_start(p)) {
        DeclSpec ds = parse_decl_spec(p);
        ASTNode *decls = parse_decl_list(p, &ds, 0);
        ASTNode *ds_node = ast_alloc(p->arena, AST_DECL_STMT, t.line, t.col);
        ds_node->decl_stmt.decl = decls;
        return ds_node;
    }

    switch (t.kind) {
    case TOK_LBRACE:
        return parse_compound(p);

    case TOK_IF: {
        advance(p);
        ASTNode *n = ast_alloc(p->arena, AST_IF, t.line, t.col);
        expect(p, TOK_LPAREN);
        n->if_stmt.cond = parse_expr(p, 0);
        expect(p, TOK_RPAREN);
        n->if_stmt.then_stmt = parse_statement(p);
        if (match(p, TOK_ELSE))
            n->if_stmt.else_stmt = parse_statement(p);
        return n;
    }

    case TOK_WHILE: {
        advance(p);
        ASTNode *n = ast_alloc(p->arena, AST_WHILE, t.line, t.col);
        expect(p, TOK_LPAREN);
        n->while_stmt.cond = parse_expr(p, 0);
        expect(p, TOK_RPAREN);
        p->loop_depth++;
        n->while_stmt.body = parse_statement(p);
        p->loop_depth--;
        return n;
    }

    case TOK_DO: {
        advance(p);
        ASTNode *n = ast_alloc(p->arena, AST_DO_WHILE, t.line, t.col);
        p->loop_depth++;
        n->while_stmt.body = parse_statement(p);
        p->loop_depth--;
        expect(p, TOK_WHILE);
        expect(p, TOK_LPAREN);
        n->while_stmt.cond = parse_expr(p, 0);
        expect(p, TOK_RPAREN);
        expect(p, TOK_SEMICOLON);
        return n;
    }

    case TOK_FOR: {
        advance(p);
        ASTNode *n = ast_alloc(p->arena, AST_FOR, t.line, t.col);
        expect(p, TOK_LPAREN);

        /* Init: declaration or expression or empty */
        if (check(p, TOK_SEMICOLON)) {
            advance(p);
            n->for_stmt.init = NULL;
        } else if (is_type_start(p)) {
            /* C99 allows declarations in for-init */
            DeclSpec ds = parse_decl_spec(p);
            ASTNode *decls = parse_decl_list(p, &ds, 0);
            ASTNode *ds_node = ast_alloc(p->arena, AST_DECL_STMT, t.line, t.col);
            ds_node->decl_stmt.decl = decls;
            n->for_stmt.init = ds_node;
            /* parse_decl_list already consumed the ';' */
        } else {
            ASTNode *es = ast_alloc(p->arena, AST_EXPR_STMT, t.line, t.col);
            es->expr_stmt.expr = parse_expr(p, 0);
            expect(p, TOK_SEMICOLON);
            n->for_stmt.init = es;
        }

        n->for_stmt.cond = check(p, TOK_SEMICOLON) ? NULL : parse_expr(p, 0);
        expect(p, TOK_SEMICOLON);
        n->for_stmt.incr = check(p, TOK_RPAREN) ? NULL : parse_expr(p, 0);
        expect(p, TOK_RPAREN);

        p->loop_depth++;
        n->for_stmt.body = parse_statement(p);
        p->loop_depth--;
        return n;
    }

    case TOK_RETURN: {
        advance(p);
        ASTNode *n = ast_alloc(p->arena, AST_RETURN, t.line, t.col);
        if (!check(p, TOK_SEMICOLON))
            n->ret.expr = parse_expr(p, 0);
        expect(p, TOK_SEMICOLON);
        return n;
    }

    case TOK_BREAK: {
        advance(p);
        if (p->loop_depth == 0 && p->switch_depth == 0) {
            diag_emit(DIAG_ERROR, t.line, t.col,
                      "'break' outside loop or switch");
            p->n_errors++;
        }
        expect(p, TOK_SEMICOLON);
        return ast_alloc(p->arena, AST_BREAK, t.line, t.col);
    }

    case TOK_CONTINUE: {
        advance(p);
        if (p->loop_depth == 0) {
            diag_emit(DIAG_ERROR, t.line, t.col, "'continue' outside loop");
            p->n_errors++;
        }
        expect(p, TOK_SEMICOLON);
        return ast_alloc(p->arena, AST_CONTINUE, t.line, t.col);
    }

    case TOK_GOTO: {
        advance(p);
        Token lt = expect(p, TOK_IDENT);
        expect(p, TOK_SEMICOLON);
        ASTNode *n = ast_alloc(p->arena, AST_GOTO, t.line, t.col);
        n->go.label = tok_intern(p, lt);
        return n;
    }

    case TOK_SWITCH: {
        advance(p);
        ASTNode *n = ast_alloc(p->arena, AST_SWITCH, t.line, t.col);
        expect(p, TOK_LPAREN);
        n->switch_stmt.expr = parse_expr(p, 0);
        expect(p, TOK_RPAREN);
        p->switch_depth++;
        n->switch_stmt.body = parse_statement(p);
        p->switch_depth--;
        return n;
    }

    case TOK_CASE: {
        advance(p);
        ASTNode *n = ast_alloc(p->arena, AST_CASE, t.line, t.col);
        n->case_stmt.expr = parse_expr(p, 0);
        expect(p, TOK_COLON);
        /* Allow fallthrough: case may be followed by next case or default */
        if (!check(p, TOK_CASE) && !check(p, TOK_DEFAULT) && !check(p, TOK_RBRACE))
            n->case_stmt.stmt = parse_statement(p);
        else {
            ASTNode *empty = ast_alloc(p->arena, AST_COMPOUND, t.line, t.col);
            n->case_stmt.stmt = empty;
        }
        return n;
    }

    case TOK_DEFAULT: {
        advance(p);
        expect(p, TOK_COLON);
        ASTNode *n = ast_alloc(p->arena, AST_DEFAULT, t.line, t.col);
        if (!check(p, TOK_CASE) && !check(p, TOK_DEFAULT) && !check(p, TOK_RBRACE))
            n->default_stmt.stmt = parse_statement(p);
        else {
            ASTNode *empty = ast_alloc(p->arena, AST_COMPOUND, t.line, t.col);
            n->default_stmt.stmt = empty;
        }
        return n;
    }

    case TOK_SEMICOLON: {
        /* Empty statement */
        advance(p);
        return ast_alloc(p->arena, AST_COMPOUND, t.line, t.col); /* empty block */
    }

    case TOK_IDENT: {
        /* Could be a label: "name:" */
        Token t2 = t;
        advance(p);  /* consume ident */
        if (check(p, TOK_COLON)) {
            advance(p); /* consume : */
            ASTNode *n = ast_alloc(p->arena, AST_LABEL, t2.line, t2.col);
            n->label.label = tok_intern(p, t2);
            n->label.stmt  = parse_statement(p);
            return n;
        }
        /* Not a label — fall through to expression statement.
         * We already consumed the ident; reconstruct via nud. */
        ASTNode *id_expr = ast_alloc(p->arena, AST_IDENT, t2.line, t2.col);
        id_expr->ident.name = tok_intern(p, t2);
        /* Continue parsing as expression with this as the left node */
        for (;;) {
            Token op = peek(p);
            int bp = tok_lbp(op.kind);
            if (bp <= 0) break;
            advance(p);
            id_expr = led(p, id_expr, op);
        }
        expect(p, TOK_SEMICOLON);
        ASTNode *es = ast_alloc(p->arena, AST_EXPR_STMT, t.line, t.col);
        es->expr_stmt.expr = id_expr;
        return es;
    }

    default:
        {
            /* Expression statement */
            ASTNode *expr = parse_expr(p, 0);
            expect(p, TOK_SEMICOLON);
            ASTNode *es = ast_alloc(p->arena, AST_EXPR_STMT, t.line, t.col);
            es->expr_stmt.expr = expr;
            return es;
        }
    }
}

static ASTNode *parse_compound(Parser *p) {
    Token lb = expect(p, TOK_LBRACE);
    ASTNode *n = ast_alloc(p->arena, AST_COMPOUND, lb.line, lb.col);

    /* Push a new scope for block-scoped declarations */
    Scope *saved_scope     = p->scope;
    Scope *saved_tag_scope = p->tag_scope;
    p->scope     = scope_new(p->arena, p->scope);
    p->tag_scope = scope_new(p->arena, p->tag_scope);

    ASTNode *head = NULL, *tail = NULL;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        ASTNode *s = parse_statement(p);
        if (s) ast_list_append(&head, &tail, s);
    }

    expect(p, TOK_RBRACE);

    p->scope     = saved_scope;
    p->tag_scope = saved_tag_scope;

    n->compound.stmts = head;
    return n;
}

/* ═══════════════════════════════════════════════════════════════════
 * Declaration list parsing
 * Returns the first declaration node; rest are linked via ->next.
 * Consumes the trailing semicolon.
 * ═══════════════════════════════════════════════════════════════════ */

static ASTNode *parse_decl_list(Parser *p, DeclSpec *ds, int top_level) {
    Token loc = peek(p);
    ASTNode *head = NULL, *tail = NULL;
    Type *base = resolve_type_spec(p, ds, loc);
    if (ds->qual != QUAL_NONE) base = type_qualify(p->types, base, ds->qual);

    /* Standalone type declaration: struct foo {}; or enum bar {}; */
    if (check(p, TOK_SEMICOLON)) {
        advance(p);
        if (ds->base &&
            (ds->base->kind == TY_STRUCT ||
             ds->base->kind == TY_UNION  ||
             ds->base->kind == TY_ENUM)) {
            ASTNodeKind kind = ds->base->kind == TY_ENUM ? AST_ENUM_DECL : AST_STRUCT_DECL;
            ASTNode *n = ast_alloc(p->arena, kind, loc.line, loc.col);
            n->type_decl.the_type = ds->base;
            return n;
        }
        return NULL;
    }

    if (ds->base && ds->base->kind == TY_ENUM && ds->base->enm.consts) {
        ASTNode *n = ast_alloc(p->arena, AST_ENUM_DECL, loc.line, loc.col);
        n->type_decl.the_type = ds->base;
        ast_list_append(&head, &tail, n);
    }

    do {
        Type *decl_type;
        DeclName dn = parse_declarator(p, base, &decl_type);

        if (!dn.name) {
            /* Abstract declarator in a declaration list — skip */
            if (!match(p, TOK_COMMA)) break;
            continue;
        }

        /* Typedef */
        if (ds->storage == SC_TYPEDEF) {
            ASTNode *n = ast_alloc(p->arena, AST_TYPEDEF_DECL, dn.line, dn.col);
            n->typedef_decl.name    = dn.name;
            n->typedef_decl.aliased = decl_type;
            scope_define_in(p->scope, p->arena, dn.name, SYM_TYPEDEF, decl_type, n);
            ast_list_append(&head, &tail, n);
            continue;
        }

        /* Function definition (top level only) */
        if (top_level && decl_type && decl_type->kind == TY_FUNC &&
            check(p, TOK_LBRACE)) {
            ASTNode *n = ast_alloc(p->arena, AST_FUNC_DEF, dn.line, dn.col);
            n->func_def.name      = dn.name;
            n->func_def.func_type = decl_type;
            n->func_def.storage   = ds->storage;
            n->func_def.spec      = ds->func_spec;

            /* Build param list as ASTNode list */
            ASTNode *phead = NULL, *ptail = NULL;

            /* Push function scope */
            Scope *saved = p->scope;
            Scope *saved_tag = p->tag_scope;
            p->scope     = scope_new(p->arena, p->scope);
            p->tag_scope = scope_new(p->arena, p->tag_scope);

            if (decl_type->func.params) {
                for (FuncParam *fp = decl_type->func.params; fp; fp = fp->next) {
                    ASTNode *pn = ast_alloc(p->arena, AST_PARAM_DECL,
                                            dn.line, dn.col);
                    pn->param_decl.name       = fp->name;
                    pn->param_decl.param_type = fp->type;
                    if (fp->name)
                        scope_define_in(p->scope, p->arena, fp->name,
                                        SYM_VAR, fp->type, pn);
                    ast_list_append(&phead, &ptail, pn);
                }
            }
            n->func_def.params = phead;

            n->func_def.body = parse_compound(p);

            p->scope     = saved;
            p->tag_scope = saved_tag;

            scope_define_in(p->scope, p->arena, dn.name, SYM_FUNC, decl_type, n);
            ast_list_append(&head, &tail, n);
            return head;  /* function def consumes no trailing semicolon */
        }

        /* Variable declaration */
        ASTNode *n = ast_alloc(p->arena, AST_VAR_DECL, dn.line, dn.col);
        n->var_decl.name      = dn.name;
        n->var_decl.decl_type = decl_type;
        n->var_decl.storage   = ds->storage;

        if (match(p, TOK_ASSIGN))
            n->var_decl.init = parse_initializer(p);

        scope_define_in(p->scope, p->arena, dn.name,
                        SYM_VAR, decl_type, n);
        ast_list_append(&head, &tail, n);

    } while (match(p, TOK_COMMA));

    expect(p, TOK_SEMICOLON);
    return head;
}

/* ═══════════════════════════════════════════════════════════════════
 * Top-level parser entry point
 * ═══════════════════════════════════════════════════════════════════ */

ASTNode *parser_parse(Parser *p) {
    Token t = peek(p);
    ASTNode *root = ast_alloc(p->arena, AST_TRANSLATION_UNIT, t.line, t.col);
    ASTNode *head = NULL, *tail = NULL;

    while (!check(p, TOK_EOF)) {
        Token loc = peek(p);

        if (check(p, TOK_PRAGMA)) {
            skip_pragma(p);
            continue;
        }

        if (!is_type_start(p)) {
            diag_emit(DIAG_ERROR, loc.line, loc.col,
                      "expected a declaration, got '%s'", token_describe(&loc));
            p->n_errors++;
            advance(p);
            continue;
        }

        DeclSpec ds = parse_decl_spec(p);
        ASTNode *decls = parse_decl_list(p, &ds, 1);
        if (decls) {
            /* Link all declarations */
            for (ASTNode *d = decls; d; ) {
                ASTNode *next_d = d->next;
                d->next = NULL;
                ast_list_append(&head, &tail, d);
                d = next_d;
            }
        }
    }

    root->tu.decls = head;
    return root;
}
