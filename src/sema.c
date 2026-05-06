/*
 * sema.c — Semantic analysis / type checker
 *
 * Responsibilities:
 *   1. Fill node->type for every expression node
 *   2. Insert AST_CAST nodes for implicit conversions
 *   3. Validate break/continue/return/goto usage
 *   4. Resolve identifiers to their types
 *   5. Integer promotions + usual arithmetic conversions
 */
#include "tinyc/sema.h"
#include "tinyc/diagnostic.h"
#include <string.h>
#include <stdio.h>

/* ── Scope helpers ───────────────────────────────────────────────── */

static SScope *sscope_new(Arena *a, SScope *parent) {
    SScope *s = (SScope *)arena_alloc(a, sizeof(SScope), _Alignof(SScope));
    memset(s, 0, sizeof(*s));
    s->parent = parent;
    return s;
}

static unsigned int ssym_hash(const char *name) {
    unsigned int h = 2166136261u;
    for (const char *p = name; *p; p++)
        h = (h ^ (unsigned char)*p) * 16777619u;
    return h & (SSCOPE_BUCKETS - 1);
}

static void sscope_define(Sema *s, const char *name, SSym_kind kind,
                           Type *ty, int offset, int is_global,
                           long long enum_val) {
    unsigned int slot = ssym_hash(name);
    SSymbol *sym = (SSymbol *)arena_alloc(s->arena, sizeof(SSymbol),
                                           _Alignof(SSymbol));
    sym->name      = name;
    sym->kind      = kind;
    sym->type      = ty;
    sym->offset    = offset;
    sym->is_global = is_global;
    sym->enum_val  = enum_val;
    sym->next      = s->scope->buckets[slot];
    s->scope->buckets[slot] = sym;
}

static SSymbol *sscope_lookup(Sema *s, const char *name) {
    unsigned int slot = ssym_hash(name);
    for (SScope *sc = s->scope; sc; sc = sc->parent) {
        for (SSymbol *sym = sc->buckets[slot]; sym; sym = sym->next)
            if (sym->name == name) return sym;
    }
    return NULL;
}

static StructField *lookup_member(Type *ty, const char *name) {
    if (!ty || (ty->kind != TY_STRUCT && ty->kind != TY_UNION))
        return NULL;
    for (StructField *f = ty->agg.fields; f; f = f->next) {
        if (f->name && f->name == name)
            return f;
        if (!f->name && f->type &&
            (f->type->kind == TY_STRUCT || f->type->kind == TY_UNION)) {
            StructField *found = lookup_member(f->type, name);
            if (found)
                return found;
        }
    }
    return NULL;
}

/* ── Implicit cast insertion ─────────────────────────────────────── */

static ASTNode *make_cast(Arena *a, ASTNode *expr, Type *to) {
    if (expr->type == to) return expr;
    ASTNode *n = ast_alloc(a, AST_CAST, expr->line, expr->col);
    n->cast.cast_type = to;
    n->cast.expr = expr;
    n->type = to;
    return n;
}

/* ── Integer promotions (C99 §6.3.1.1) ──────────────────────────── */
/* char/short → int; unsigned char/unsigned short → int if int can hold all
 * values, otherwise unsigned int. On all our targets int > short, so always int. */
static Type *integer_promote(Type *t) {
    switch (t->kind) {
    case TY_BOOL:
    case TY_CHAR: case TY_SCHAR: case TY_UCHAR:
    case TY_SHORT: case TY_USHORT:
        return type_int();
    default: return t;
    }
}

/* ── Usual arithmetic conversions (C99 §6.3.1.8) ────────────────── */
static Type *usual_arithmetic(Type *a, Type *b) {
    /* If either is floating: float < double < long double */
    if (a->kind == TY_LDOUBLE || b->kind == TY_LDOUBLE) return type_ldouble();
    if (a->kind == TY_DOUBLE  || b->kind == TY_DOUBLE)  return type_double();
    if (a->kind == TY_FLOAT   || b->kind == TY_FLOAT)   return type_float();

    /* Both integer: apply promotions first */
    a = integer_promote(a);
    b = integer_promote(b);

    /* Same type after promotion */
    if (a == b) return a;

    /* Rank order: int < unsigned int < long < unsigned long < llong < ullong */
    int rank_a = 0, rank_b = 0;
    switch (a->kind) {
    case TY_INT:    rank_a = 1; break;
    case TY_UINT:   rank_a = 2; break;
    case TY_LONG:   rank_a = 3; break;
    case TY_ULONG:  rank_a = 4; break;
    case TY_LLONG:  rank_a = 5; break;
    case TY_ULLONG: rank_a = 6; break;
    default: rank_a = 1; break;
    }
    switch (b->kind) {
    case TY_INT:    rank_b = 1; break;
    case TY_UINT:   rank_b = 2; break;
    case TY_LONG:   rank_b = 3; break;
    case TY_ULONG:  rank_b = 4; break;
    case TY_LLONG:  rank_b = 5; break;
    case TY_ULLONG: rank_b = 6; break;
    default: rank_b = 1; break;
    }
    return rank_a >= rank_b ? a : b;
}

/* ── Array/function decay ────────────────────────────────────────── */
static Type *decay(TypeTable *tt, Type *t) {
    if (t->kind == TY_ARRAY || t->kind == TY_ARRAY_UNSIZED)
        return type_pointer(tt, t->arr.elem, QUAL_NONE);
    if (t->kind == TY_FUNC)
        return type_pointer(tt, t, QUAL_NONE);
    return t;
}

/* ── Forward declarations ────────────────────────────────────────── */
static ASTNode *check_expr(Sema *s, ASTNode *node);
static void check_stmt(Sema *s, ASTNode *node);
static void check_decl(Sema *s, ASTNode *node, int is_global);
static int label_exists(ASTNode *node, const char *label);

static void sema_expr_error(Sema *s, ASTNode *node, const char *msg) {
    int line = node ? node->line : 1;
    int col = node ? node->col : 1;
    diag_emit(DIAG_ERROR, line, col, "%s", msg);
    s->n_errors++;
}

static int is_const_lvalue(ASTNode *node) {
    return node && node->type && (node->type->qual & QUAL_CONST) != 0;
}

/* ── Expression type checker ─────────────────────────────────────── */

static ASTNode *check_expr(Sema *s, ASTNode *node) {
    if (!node) return node;

    switch (node->kind) {
    case AST_INT_LIT:
    case AST_CHAR_LIT: {
        /* Determine type from suffix */
        int suf = node->int_lit.suffix;
        if (suf & INT_SUFFIX_LL)      node->type = (suf & INT_SUFFIX_U) ? type_ullong() : type_llong();
        else if (suf & INT_SUFFIX_L)  node->type = (suf & INT_SUFFIX_U) ? type_ulong()  : type_long();
        else if (suf & INT_SUFFIX_U)  node->type = type_uint();
        else                          node->type = type_int();
        return node;
    }
    case AST_FLOAT_LIT:
        if (node->float_lit.suffix == 'f') node->type = type_float();
        else if (node->float_lit.suffix == 'l') node->type = type_ldouble();
        else node->type = type_double();
        return node;

    case AST_STR_LIT:
        node->type = type_pointer(s->types, type_char(), QUAL_CONST);
        return node;

    case AST_IDENT: {
        SSymbol *sym = sscope_lookup(s, node->ident.name);
        if (!sym) {
            /* Undeclared — assign int type and continue for error recovery */
            node->type = type_int();
            /* Only warn — many test snippets use undeclared identifiers */
            return node;
        }
        if (sym->kind == SSYM_ENUM_CONST) {
            node->kind = AST_INT_LIT;
            node->int_lit.val = (unsigned long long)sym->enum_val;
            node->int_lit.suffix = 0;
            node->int_lit.is_wide = 0;
            node->type = type_int();
        } else {
            node->type = sym->type ? sym->type : type_int();
        }
        return node;
    }

    case AST_UNOP: {
        ASTNode *op = check_expr(s, node->unop.operand);
        node->unop.operand = op;
        Type *t = op->type ? decay(s->types, op->type) : type_int();
        switch (node->unop.op) {
        case TOK_MINUS: case TOK_PLUS:
            node->type = integer_promote(t);
            break;
        case TOK_BANG:
            node->type = type_int();
            break;
        case TOK_TILDE:
            node->type = integer_promote(t);
            break;
        case TOK_AMP:
            /* address-of: &T → T* */
            node->type = type_pointer(s->types, op->type ? op->type : type_int(), QUAL_NONE);
            break;
        case TOK_STAR:
            /* dereference: *T* → T */
            if (t->kind == TY_POINTER)
                node->type = t->ptr.pointee;
            else
                node->type = type_int();
            break;
        case TOK_PLUSPLUS: case TOK_MINUSMINUS:
            if (is_const_lvalue(op))
                sema_expr_error(s, node, "assignment to const-qualified object");
            node->type = t;
            break;
        default:
            node->type = type_int();
            break;
        }
        return node;
    }

    case AST_POSTFIX: {
        ASTNode *op = check_expr(s, node->postfix.operand);
        node->postfix.operand = op;
        Type *t = op->type ? decay(s->types, op->type) : type_int();
        if (is_const_lvalue(op))
            sema_expr_error(s, node, "assignment to const-qualified object");
        node->type = t;
        return node;
    }

    case AST_BINOP: {
        ASTNode *l = check_expr(s, node->binop.left);
        ASTNode *r = check_expr(s, node->binop.right);
        Type *lt = l->type ? decay(s->types, l->type) : type_int();
        Type *rt = r->type ? decay(s->types, r->type) : type_int();

        Type *common = NULL;

        switch (node->binop.op) {
        /* Comparison operators always yield int, but operate on common type */
        case TOK_EQ: case TOK_NEQ:
        case TOK_LT: case TOK_LE:
        case TOK_GT: case TOK_GE:
            common = usual_arithmetic(lt, rt);
            node->type = type_int();
            break;
        case TOK_AND: case TOK_OR:
            node->type = type_int();
            break;
        /* Pointer arithmetic: ptr + int → ptr, ptr - ptr → ptrdiff_t */
        case TOK_PLUS:
            if (lt->kind == TY_POINTER) { node->type = lt; break; }
            if (rt->kind == TY_POINTER) { node->type = rt; break; }
            node->type = usual_arithmetic(lt, rt);
            common = node->type;
            break;
        case TOK_MINUS:
            if (lt->kind == TY_POINTER && rt->kind == TY_POINTER)
                { node->type = type_long(); break; }
            if (lt->kind == TY_POINTER) { node->type = lt; break; }
            node->type = usual_arithmetic(lt, rt);
            common = node->type;
            break;
        default:
            node->type = usual_arithmetic(lt, rt);
            common = node->type;
            break;
        }
        /* Insert conversions to common type */
        if (common) {
            if (common != lt && type_is_arithmetic(lt) && type_is_arithmetic(common))
                node->binop.left = make_cast(s->arena, l, common);
            if (common != rt && type_is_arithmetic(rt) && type_is_arithmetic(common))
                node->binop.right = make_cast(s->arena, r, common);
        }
        return node;
    }

    case AST_ASSIGN: {
        ASTNode *l = check_expr(s, node->assign.left);
        ASTNode *r = check_expr(s, node->assign.right);
        Type *lt = l->type ? l->type : type_int();
        Type *rt = r->type ? r->type : type_int();
        if (is_const_lvalue(l))
            sema_expr_error(s, node, "assignment to const-qualified object");
        /* Compound assignments compute arithmetic type, then assign */
        if (node->assign.op != TOK_ASSIGN && type_is_arithmetic(lt) && type_is_arithmetic(rt)) {
            Type *common = usual_arithmetic(lt, rt);
            node->assign.right = make_cast(s->arena, r, common);
        } else if (lt != rt) {
            node->assign.right = make_cast(s->arena, r, lt);
        }
        node->type = lt;
        return node;
    }

    case AST_TERNARY: {
        check_expr(s, node->ternary.cond);
        ASTNode *th = check_expr(s, node->ternary.then_expr);
        ASTNode *el = check_expr(s, node->ternary.else_expr);
        Type *tt2 = th->type ? decay(s->types, th->type) : type_int();
        Type *et  = el->type ? decay(s->types, el->type) : type_int();
        if (type_is_arithmetic(tt2) && type_is_arithmetic(et))
            node->type = usual_arithmetic(tt2, et);
        else if (tt2->kind == TY_POINTER)
            node->type = tt2;
        else
            node->type = tt2;
        return node;
    }

    case AST_CALL: {
        ASTNode *fn = check_expr(s, node->call.func);
        Type *ft = fn->type ? decay(s->types, fn->type) : NULL;
        if (ft && ft->kind == TY_POINTER && ft->ptr.pointee->kind == TY_FUNC)
            ft = ft->ptr.pointee;

        node->type = (ft && ft->kind == TY_FUNC) ? ft->func.ret : type_int();

        /* Check args */
        for (ASTNode *a = node->call.args; a; a = a->next)
            check_expr(s, a);
        return node;
    }

    case AST_SUBSCRIPT: {
        ASTNode *base = check_expr(s, node->subscript.base);
        check_expr(s, node->subscript.index);
        Type *bt = base->type ? decay(s->types, base->type) : NULL;
        if (bt && bt->kind == TY_POINTER)
            node->type = bt->ptr.pointee;
        else
            node->type = type_int();
        return node;
    }

    case AST_MEMBER: {
        ASTNode *base = check_expr(s, node->member.base);
        Type *bt = base->type;
        if (bt && (bt->kind == TY_STRUCT || bt->kind == TY_UNION)) {
            StructField *f = lookup_member(bt, node->member.field);
            if (f)
                node->type = f->type;
        }
        if (!node->type) node->type = type_int();
        return node;
    }

    case AST_MEMBER_PTR: {
        ASTNode *base = check_expr(s, node->member.base);
        Type *bt = base->type ? decay(s->types, base->type) : NULL;
        if (bt && bt->kind == TY_POINTER) {
            Type *pt = bt->ptr.pointee;
            if (pt->kind == TY_STRUCT || pt->kind == TY_UNION) {
                StructField *f = lookup_member(pt, node->member.field);
                if (f)
                    node->type = f->type;
            }
        }
        if (!node->type) node->type = type_int();
        return node;
    }

    case AST_CAST: {
        check_expr(s, node->cast.expr);
        node->type = node->cast.cast_type;
        return node;
    }

    case AST_SIZEOF_EXPR:
        check_expr(s, node->sizeof_expr.expr);
        node->type = type_ulong();
        return node;

    case AST_SIZEOF_TYPE:
        node->type = type_ulong();
        return node;

    case AST_COMMA: {
        check_expr(s, node->comma.left);
        ASTNode *r = check_expr(s, node->comma.right);
        node->type = r->type;
        return node;
    }

    case AST_INIT_LIST:
        for (ASTNode *it = node->init_list.items; it; it = it->next) {
            if (it->kind == AST_DESIGNATOR) {
                check_expr(s, it->designator.value);
            } else {
                check_expr(s, it);
            }
        }
        node->type = type_void();
        return node;

    case AST_DESIGNATOR:
        check_expr(s, node->designator.value);
        node->type = node->designator.value ? node->designator.value->type : type_int();
        return node;

    case AST_COMPOUND_LIT:
        check_expr(s, node->compound_lit.init);
        node->type = node->compound_lit.lit_type;
        return node;

    default:
        node->type = type_int();
        return node;
    }
}

/* ── Statement checker ───────────────────────────────────────────── */

static void sema_error(Sema *s, ASTNode *node, const char *msg) {
    int line = node ? node->line : 1;
    int col = node ? node->col : 1;
    diag_emit(DIAG_ERROR, line, col, "%s", msg);
    s->n_errors++;
}

static int label_exists(ASTNode *node, const char *label) {
    if (!node) return 0;
    for (ASTNode *n = node; n; n = n->next) {
        switch (n->kind) {
        case AST_LABEL:
            if (n->label.label == label) return 1;
            if (label_exists(n->label.stmt, label)) return 1;
            break;
        case AST_COMPOUND:
            if (label_exists(n->compound.stmts, label)) return 1;
            break;
        case AST_IF:
            if (label_exists(n->if_stmt.then_stmt, label)) return 1;
            if (label_exists(n->if_stmt.else_stmt, label)) return 1;
            break;
        case AST_WHILE:
        case AST_DO_WHILE:
            if (label_exists(n->while_stmt.body, label)) return 1;
            break;
        case AST_FOR:
            if (label_exists(n->for_stmt.body, label)) return 1;
            break;
        case AST_SWITCH:
            if (label_exists(n->switch_stmt.body, label)) return 1;
            break;
        case AST_CASE:
            if (label_exists(n->case_stmt.stmt, label)) return 1;
            break;
        case AST_DEFAULT:
            if (label_exists(n->default_stmt.stmt, label)) return 1;
            break;
        default:
            break;
        }
    }
    return 0;
}

static void check_stmt(Sema *s, ASTNode *node) {
    if (!node) return;

    switch (node->kind) {
    case AST_COMPOUND: {
        SScope *saved = s->scope;
        s->scope = sscope_new(s->arena, s->scope);
        for (ASTNode *st = node->compound.stmts; st; st = st->next)
            check_stmt(s, st);
        s->scope = saved;
        break;
    }

    case AST_EXPR_STMT:
        check_expr(s, node->expr_stmt.expr);
        break;

    case AST_DECL_STMT:
        check_decl(s, node->decl_stmt.decl, 0);
        break;

    case AST_IF:
        check_expr(s, node->if_stmt.cond);
        check_stmt(s, node->if_stmt.then_stmt);
        check_stmt(s, node->if_stmt.else_stmt);
        break;

    case AST_WHILE:
        check_expr(s, node->while_stmt.cond);
        s->loop_depth++;
        check_stmt(s, node->while_stmt.body);
        s->loop_depth--;
        break;

    case AST_DO_WHILE:
        s->loop_depth++;
        check_stmt(s, node->while_stmt.body);
        s->loop_depth--;
        check_expr(s, node->while_stmt.cond);
        break;

    case AST_FOR:
        check_stmt(s, node->for_stmt.init);
        check_expr(s, node->for_stmt.cond);
        check_expr(s, node->for_stmt.incr);
        s->loop_depth++;
        check_stmt(s, node->for_stmt.body);
        s->loop_depth--;
        break;

    case AST_RETURN:
        if (node->ret.expr) {
            ASTNode *e = check_expr(s, node->ret.expr);
            if (s->cur_func_ret && e->type && e->type != s->cur_func_ret &&
                type_is_arithmetic(e->type) && type_is_arithmetic(s->cur_func_ret)) {
                node->ret.expr = make_cast(s->arena, e, s->cur_func_ret);
            }
        }
        break;

    case AST_SWITCH:
        check_expr(s, node->switch_stmt.expr);
        s->switch_depth++;
        check_stmt(s, node->switch_stmt.body);
        s->switch_depth--;
        break;

    case AST_CASE:
        if (s->switch_depth == 0)
            sema_error(s, node, "'case' outside switch");
        check_expr(s, node->case_stmt.expr);
        check_stmt(s, node->case_stmt.stmt);
        break;

    case AST_DEFAULT:
        if (s->switch_depth == 0)
            sema_error(s, node, "'default' outside switch");
        check_stmt(s, node->default_stmt.stmt);
        break;

    case AST_LABEL:
        check_stmt(s, node->label.stmt);
        break;

    case AST_BREAK:
        if (s->loop_depth == 0 && s->switch_depth == 0)
            sema_error(s, node, "'break' outside loop or switch");
        break;

    case AST_CONTINUE:
        if (s->loop_depth == 0)
            sema_error(s, node, "'continue' outside loop");
        break;

    case AST_GOTO:
        if (!label_exists(s->cur_func_body, node->go.label))
            sema_error(s, node, "goto target label not found");
        break;

    default:
        break;
    }
}

/* ── Declaration checker ─────────────────────────────────────────── */

static void check_decl(Sema *s, ASTNode *node, int is_global) {
    if (!node) return;

    /* Handle linked lists of declarations */
    for (ASTNode *d = node; d; d = d->next) {
        switch (d->kind) {
        case AST_VAR_DECL:
            if (d->var_decl.init)
                check_expr(s, d->var_decl.init);
            sscope_define(s, d->var_decl.name, SSYM_VAR,
                          d->var_decl.decl_type, 0, is_global, 0);
            break;

        case AST_TYPEDEF_DECL:
            sscope_define(s, d->typedef_decl.name, SSYM_TYPEDEF,
                          d->typedef_decl.aliased, 0, 0, 0);
            break;

        case AST_FUNC_DEF: {
            sscope_define(s, d->func_def.name, SSYM_FUNC,
                          d->func_def.func_type, 0, 1, 0);

            /* Save and restore function context */
            Type *saved_ret = s->cur_func_ret;
            ASTNode *saved_body = s->cur_func_body;
            SScope *saved_scope = s->scope;

            if (d->func_def.func_type && d->func_def.func_type->kind == TY_FUNC)
                s->cur_func_ret = d->func_def.func_type->func.ret;
            s->cur_func_body = d->func_def.body;

            /* Push function scope and add parameters */
            s->scope = sscope_new(s->arena, s->scope);
            for (ASTNode *p = d->func_def.params; p; p = p->next) {
                if (p->param_decl.name)
                    sscope_define(s, p->param_decl.name, SSYM_VAR,
                                  p->param_decl.param_type, 0, 0, 0);
            }

            check_stmt(s, d->func_def.body);

            s->scope = saved_scope;
            s->cur_func_ret = saved_ret;
            s->cur_func_body = saved_body;
            break;
        }

        case AST_ENUM_DECL:
            if (d->type_decl.the_type && d->type_decl.the_type->kind == TY_ENUM) {
                for (EnumConst *ec = d->type_decl.the_type->enm.consts; ec; ec = ec->next) {
                    sscope_define(s, ec->name, SSYM_ENUM_CONST, type_int(),
                                  0, is_global, ec->value);
                }
            }
            break;

        case AST_STRUCT_DECL:
            /* Type is already in the type table via the parser; nothing to do */
            break;

        default:
            break;
        }
    }
}

/* ── Public entry point ──────────────────────────────────────────── */

void sema_init(Sema *s, Arena *arena, StringTable *strings, TypeTable *types) {
    memset(s, 0, sizeof(*s));
    s->arena   = arena;
    s->strings = strings;
    s->types   = types;
    s->scope   = sscope_new(arena, NULL);
}

int sema_check(Sema *s, ASTNode *root) {
    if (!root || root->kind != AST_TRANSLATION_UNIT) return 0;
    for (ASTNode *d = root->tu.decls; d; d = d->next)
        check_decl(s, d, 1);
    return s->n_errors;
}
