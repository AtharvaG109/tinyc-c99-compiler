/*
 * irgen.c — Lower AST to three-address IR
 *
 * Each expression returns an IRValue (virtual register or constant).
 * Statements emit instructions into the current IRFunc.
 *
 * Variable storage model:
 *   - Local variables → IR_ALLOCA on entry, access via IR_LOAD / IR_STORE
 *   - Global variables → IR_GLOBAL_DEF, access via IR_LOAD / IR_STORE of global addr
 *   - Parameters → slots on entry block, treated as locals
 */
#include "tinyc/irgen.h"
#include "tinyc/diagnostic.h"
#include "tinyc/abi_classify.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Variable table: name → alloca temp ─────────────────────────── */

typedef struct VarEntry {
    const char     *name;
    IRValue         addr;   /* pointer to storage (result of IR_ALLOCA or IRV_GLOBAL) */
    int             is_global;
    struct VarEntry *next;
} VarEntry;

/* ── Switch case → label mapping ─────────────────────────────────── */
typedef struct CaseEntry {
    ASTNode        *node;        /* pointer to AST_CASE or AST_DEFAULT node */
    long long       val;         /* case constant value */
    int             is_default;
    int             label_id;
    struct CaseEntry *next;
} CaseEntry;

/* ── Named label → label_id mapping (for goto) ───────────────────── */
typedef struct GotoLabel {
    const char      *name;
    int              label_id;
    struct GotoLabel *next;
} GotoLabel;

#define VTAB_BUCKETS 64

typedef struct VScope {
    VarEntry      *buckets[VTAB_BUCKETS];
    struct VScope *parent;
} VScope;

typedef struct {
    IRModule   *module;
    IRFunc     *func;        /* current function */
    Arena      *arena;
    StringTable *strings;
    TypeTable  *types;       /* for constructing pointer types */
    VScope     *scope;
    int         break_label;
    int         cont_label;
    /* Switch support */
    CaseEntry  *case_entries;    /* cases for current switch (NULL outside switch) */
    int         sw_default_lbl;  /* label_id of default case (0 = none) */
    /* Goto support */
    GotoLabel  *goto_labels;     /* all named labels in current function */
} IrGen;

static VScope *vscope_new(Arena *a, VScope *parent) {
    VScope *s = (VScope *)arena_alloc(a, sizeof(VScope), _Alignof(VScope));
    memset(s, 0, sizeof(*s));
    s->parent = parent;
    return s;
}

static unsigned int vhash(const char *name) {
    unsigned int h = 2166136261u;
    for (const char *p = name; *p; p++)
        h = (h ^ (unsigned char)*p) * 16777619u;
    return h & (VTAB_BUCKETS - 1);
}

static void vscope_define(IrGen *g, const char *name, IRValue addr, int is_global) {
    unsigned int slot = vhash(name);
    VarEntry *e = (VarEntry *)arena_alloc(g->arena, sizeof(VarEntry),
                                           _Alignof(VarEntry));
    e->name      = name;
    e->addr      = addr;
    e->is_global = is_global;
    e->next      = g->scope->buckets[slot];
    g->scope->buckets[slot] = e;
}

static VarEntry *vscope_lookup(IrGen *g, const char *name) {
    unsigned int slot = vhash(name);
    for (VScope *sc = g->scope; sc; sc = sc->parent) {
        for (VarEntry *e = sc->buckets[slot]; e; e = e->next)
            if (e->name == name) return e;
    }
    return NULL;
}

/* ── Helper: emit arithmetic IR from token operator ─────────────── */
static IROp tok_to_ir_binop(TokenKind tk) {
    switch (tk) {
    case TOK_PLUS:    return IR_ADD;
    case TOK_MINUS:   return IR_SUB;
    case TOK_STAR:    return IR_MUL;
    case TOK_SLASH:   return IR_DIV;
    case TOK_PERCENT: return IR_MOD;
    case TOK_AMP:     return IR_AND;
    case TOK_PIPE:    return IR_OR;
    case TOK_CARET:   return IR_XOR;
    case TOK_LSHIFT:  return IR_SHL;
    case TOK_RSHIFT:  return IR_SHR;
    case TOK_EQ:      return IR_EQ;
    case TOK_NEQ:     return IR_NE;
    case TOK_LT:      return IR_LT;
    case TOK_LE:      return IR_LE;
    case TOK_GT:      return IR_GT;
    case TOK_GE:      return IR_GE;
    default:          return IR_ADD;
    }
}

/* Map compound assignment op to its base arithmetic op */
static TokenKind compound_base(TokenKind tk) {
    switch (tk) {
    case TOK_PLUS_ASSIGN:   return TOK_PLUS;
    case TOK_MINUS_ASSIGN:  return TOK_MINUS;
    case TOK_STAR_ASSIGN:   return TOK_STAR;
    case TOK_SLASH_ASSIGN:  return TOK_SLASH;
    case TOK_PERCENT_ASSIGN:return TOK_PERCENT;
    case TOK_AMP_ASSIGN:    return TOK_AMP;
    case TOK_PIPE_ASSIGN:   return TOK_PIPE;
    case TOK_CARET_ASSIGN:  return TOK_CARET;
    case TOK_LSHIFT_ASSIGN: return TOK_LSHIFT;
    case TOK_RSHIFT_ASSIGN: return TOK_RSHIFT;
    default: return tok_to_ir_binop(tk) != IR_ADD ? TOK_PLUS : TOK_PLUS;
    }
}

/* ── Forward declarations ────────────────────────────────────────── */
static IRValue gen_expr(IrGen *g, ASTNode *node);
static IRValue gen_lvalue(IrGen *g, ASTNode *node);
static void    gen_stmt(IrGen *g, ASTNode *node);
static void    gen_initializer(IrGen *g, IRValue addr, Type *ty, ASTNode *init);
static void    gen_zero_initializer(IrGen *g, IRValue addr, Type *ty);
static StructField *find_field(Type *ty, const char *name);
static StructField *find_field_with_offset(Type *ty, const char *name, int *offset);
static unsigned long long bit_mask(int width);

static int is_aggregate_type(Type *ty) {
    return ty && (ty->kind == TY_STRUCT || ty->kind == TY_UNION);
}

static int pointer_pointee_size(Type *ty) {
    if (!ty || ty->kind != TY_POINTER)
        return 1;
    int sz = type_sizeof(ty->ptr.pointee);
    return sz > 0 ? sz : 1;
}

static IRValue gen_aggregate_expr_addr(IrGen *g, ASTNode *node) {
    if (!node) return irv_none();
    switch (node->kind) {
    case AST_CALL:
    case AST_COMPOUND_LIT:
    case AST_ASSIGN:
        return gen_expr(g, node);
    default:
        return gen_lvalue(g, node);
    }
}

/* ── Evaluate a constant expression (for switch case values) ─────── */
static long long eval_const_expr(ASTNode *node) {
    if (!node) return 0;
    switch (node->kind) {
    case AST_INT_LIT:  return (long long)node->int_lit.val;
    case AST_CHAR_LIT: return (long long)node->int_lit.val;
    case AST_UNOP:
        if (node->unop.op == TOK_MINUS)
            return -eval_const_expr(node->unop.operand);
        return eval_const_expr(node->unop.operand);
    default: return 0;
    }
}

/* ── Pre-scan switch body for case/default labels ────────────────── */
static void collect_switch_cases(IrGen *g, ASTNode *node) {
    if (!node) return;
    switch (node->kind) {
    case AST_CASE: {
        long long val = eval_const_expr(node->case_stmt.expr);
        int lbl = ir_new_label(g->func);
        CaseEntry *e = (CaseEntry *)arena_alloc(g->arena, sizeof(CaseEntry),
                                                 _Alignof(CaseEntry));
        e->node = node; e->val = val; e->is_default = 0; e->label_id = lbl;
        e->next = g->case_entries; g->case_entries = e;
        collect_switch_cases(g, node->case_stmt.stmt);
        break;
    }
    case AST_DEFAULT: {
        int lbl = ir_new_label(g->func);
        g->sw_default_lbl = lbl;
        CaseEntry *e = (CaseEntry *)arena_alloc(g->arena, sizeof(CaseEntry),
                                                 _Alignof(CaseEntry));
        e->node = node; e->val = 0; e->is_default = 1; e->label_id = lbl;
        e->next = g->case_entries; g->case_entries = e;
        collect_switch_cases(g, node->default_stmt.stmt);
        break;
    }
    case AST_SWITCH:
        /* Don't recurse into nested switches */
        break;
    case AST_COMPOUND:
        for (ASTNode *s = node->compound.stmts; s; s = s->next)
            collect_switch_cases(g, s);
        break;
    case AST_IF:
        collect_switch_cases(g, node->if_stmt.then_stmt);
        collect_switch_cases(g, node->if_stmt.else_stmt);
        break;
    case AST_WHILE: case AST_DO_WHILE:
        collect_switch_cases(g, node->while_stmt.body);
        break;
    case AST_FOR:
        collect_switch_cases(g, node->for_stmt.body);
        break;
    case AST_LABEL:
        collect_switch_cases(g, node->label.stmt);
        break;
    default: break;
    }
}

/* ── Pre-scan function body for named labels (goto targets) ──────── */
static void collect_labels(IrGen *g, ASTNode *node) {
    if (!node) return;
    if (node->kind == AST_LABEL) {
        int lbl = ir_new_label(g->func);
        GotoLabel *gl = (GotoLabel *)arena_alloc(g->arena, sizeof(GotoLabel),
                                                  _Alignof(GotoLabel));
        gl->name = node->label.label;
        gl->label_id = lbl;
        gl->next = g->goto_labels;
        g->goto_labels = gl;
        collect_labels(g, node->label.stmt);
        return;
    }
    switch (node->kind) {
    case AST_COMPOUND:
        for (ASTNode *s = node->compound.stmts; s; s = s->next)
            collect_labels(g, s);
        break;
    case AST_IF:
        collect_labels(g, node->if_stmt.then_stmt);
        collect_labels(g, node->if_stmt.else_stmt);
        break;
    case AST_WHILE: case AST_DO_WHILE:
        collect_labels(g, node->while_stmt.body);
        break;
    case AST_FOR:
        collect_labels(g, node->for_stmt.body);
        break;
    case AST_SWITCH:
        collect_labels(g, node->switch_stmt.body);
        break;
    case AST_CASE:
        collect_labels(g, node->case_stmt.stmt);
        break;
    case AST_DEFAULT:
        collect_labels(g, node->default_stmt.stmt);
        break;
    default: break;
    }
}

/* Load from an address value */
static IRValue gen_load(IrGen *g, IRValue addr, Type *ty) {
    int t = ir_new_temp(g->func);
    IRValue dst = irv_temp(t, ty);
    ir_emit(g->func, IR_LOAD, dst, addr, irv_none());
    return dst;
}

/* Emit a store */
static void gen_store(IrGen *g, IRValue addr, IRValue val) {
    ir_emit(g->func, IR_STORE, addr, val, irv_none());
}

static IRValue gen_bitfield_load(IrGen *g, IRValue storage_addr,
                                 StructField *field) {
    Type *storage_ty = field->type ? field->type : type_uint();
    IRValue raw = gen_load(g, storage_addr, storage_ty);
    IRValue shifted = raw;
    if (field->bit_offset > 0) {
        int st = ir_new_temp(g->func);
        shifted = irv_temp(st, storage_ty);
        ir_emit(g->func, IR_SHR, shifted, raw,
                irv_int(field->bit_offset, type_int()));
    }
    int mt = ir_new_temp(g->func);
    IRValue masked = irv_temp(mt, storage_ty);
    ir_emit(g->func, IR_AND, masked, shifted,
            irv_int((long long)bit_mask(field->bit_width), storage_ty));
    return masked;
}

static IRValue gen_bitfield_store(IrGen *g, IRValue storage_addr,
                                  StructField *field, IRValue value) {
    Type *storage_ty = field->type ? field->type : type_uint();
    unsigned long long mask = bit_mask(field->bit_width);
    unsigned long long shifted_mask = mask << field->bit_offset;

    IRValue current = gen_load(g, storage_addr, storage_ty);

    int ct = ir_new_temp(g->func);
    IRValue cleared = irv_temp(ct, storage_ty);
    ir_emit(g->func, IR_AND, cleared, current,
            irv_int((long long)~shifted_mask, storage_ty));

    int vt = ir_new_temp(g->func);
    IRValue clipped = irv_temp(vt, storage_ty);
    ir_emit(g->func, IR_AND, clipped, value, irv_int((long long)mask, storage_ty));

    IRValue shifted = clipped;
    if (field->bit_offset > 0) {
        int st = ir_new_temp(g->func);
        shifted = irv_temp(st, storage_ty);
        ir_emit(g->func, IR_SHL, shifted, clipped,
                irv_int(field->bit_offset, type_int()));
    }

    int nt = ir_new_temp(g->func);
    IRValue next = irv_temp(nt, storage_ty);
    ir_emit(g->func, IR_OR, next, cleared, shifted);
    gen_store(g, storage_addr, next);
    return value;
}

static IRValue gen_alloca_value(IrGen *g, Type *ty) {
    int sz = type_sizeof(ty);
    int al = type_alignof(ty);
    if (sz <= 0 && ty && ty->kind == TY_ARRAY && ty->arr.count <= 0)
        sz = 4096;
    if (sz <= 0) sz = 8;
    if (al <= 0) al = 8;

    int addr_t = ir_new_temp(g->func);
    IRValue addr = irv_temp(addr_t, type_pointer(g->types, ty, QUAL_NONE));
    IRInstr *alloca = ir_emit(g->func, IR_ALLOCA, addr, irv_none(), irv_none());
    alloca->alloca_size = sz;
    alloca->alloca_align = al;
    return addr;
}

static IRValue gen_addr_offset(IrGen *g, IRValue base, int offset, Type *pointee) {
    Type *ptr_ty = type_pointer(g->types, pointee, QUAL_NONE);
    if (offset == 0 && base.type && base.type->kind == TY_POINTER &&
        base.type->ptr.pointee == pointee)
        return base;
    int t = ir_new_temp(g->func);
    IRValue dst = irv_temp(t, ptr_ty);
    ir_emit(g->func, IR_ADD, dst, base, irv_int(offset, type_long()));
    return dst;
}

static StructField *find_field(Type *ty, const char *name) {
    int offset = 0;
    return find_field_with_offset(ty, name, &offset);
}

static StructField *find_field_with_offset(Type *ty, const char *name, int *offset) {
    if (!ty || (ty->kind != TY_STRUCT && ty->kind != TY_UNION))
        return NULL;
    for (StructField *f = ty->agg.fields; f; f = f->next) {
        if (f->name == name) {
            if (offset) *offset = f->offset;
            return f;
        }
        if (!f->name && f->type &&
            (f->type->kind == TY_STRUCT || f->type->kind == TY_UNION)) {
            int nested = 0;
            StructField *found = find_field_with_offset(f->type, name, &nested);
            if (found) {
                if (offset) *offset = f->offset + nested;
                return found;
            }
        }
    }
    return NULL;
}

static StructField *member_field(ASTNode *node) {
    if (!node) return NULL;
    if (node->kind == AST_MEMBER) {
        Type *bt = node->member.base ? node->member.base->type : NULL;
        return find_field(bt, node->member.field);
    }
    if (node->kind == AST_MEMBER_PTR) {
        Type *pt = node->member.base ? node->member.base->type : NULL;
        if (pt && pt->kind == TY_POINTER)
            pt = pt->ptr.pointee;
        return find_field(pt, node->member.field);
    }
    return NULL;
}

static unsigned long long bit_mask(int width) {
    if (width <= 0) return 0;
    if (width >= 63) return ~0ull;
    return (1ull << width) - 1ull;
}

static long long const_int_value(ASTNode *node) {
    if (!node) return 0;
    switch (node->kind) {
    case AST_INT_LIT:
    case AST_CHAR_LIT:
        return (long long)node->int_lit.val;
    case AST_UNOP:
        if (node->unop.op == TOK_MINUS)
            return -const_int_value(node->unop.operand);
        if (node->unop.op == TOK_PLUS)
            return const_int_value(node->unop.operand);
        break;
    default:
        break;
    }
    return 0;
}

static void write_int_le(unsigned char *buf, int off, int sz, long long val) {
    unsigned long long u = (unsigned long long)val;
    for (int i = 0; i < sz; i++)
        buf[off + i] = (unsigned char)((u >> (i * 8)) & 0xffu);
}

static void fill_global_initializer(Type *ty, ASTNode *init,
                                    unsigned char *buf, int base_off);

static void fill_global_scalar(Type *ty, ASTNode *init,
                               unsigned char *buf, int base_off) {
    if (init && init->kind == AST_INIT_LIST) {
        ASTNode *first = init->init_list.items;
        if (first && first->kind == AST_DESIGNATOR)
            first = first->designator.value;
        init = first;
    }
    int sz = type_sizeof(ty);
    if (sz <= 0) return;
    if (sz > 8) sz = 8;
    write_int_le(buf, base_off, sz, const_int_value(init));
}

static void fill_global_array(Type *ty, ASTNode *init,
                              unsigned char *buf, int base_off) {
    if (!init || init->kind != AST_INIT_LIST) {
        fill_global_scalar(ty, init, buf, base_off);
        return;
    }

    Type *elem = ty->arr.elem;
    int elem_sz = type_sizeof(elem);
    if (elem_sz <= 0) elem_sz = 1;
    long long next_index = 0;

    for (ASTNode *it = init->init_list.items; it; it = it->next) {
        ASTNode *value = it;
        long long index = next_index;
        if (it->kind == AST_DESIGNATOR) {
            if (it->designator.is_field)
                continue;
            index = const_int_value(it->designator.index);
            value = it->designator.value;
        }
        if (ty->kind == TY_ARRAY && (index < 0 || index >= ty->arr.count)) {
            next_index = index + 1;
            continue;
        }
        fill_global_initializer(elem, value, buf, base_off + (int)(index * elem_sz));
        next_index = index + 1;
    }
}

static void fill_global_aggregate(Type *ty, ASTNode *init,
                                  unsigned char *buf, int base_off) {
    if (!init || init->kind != AST_INIT_LIST) {
        fill_global_scalar(ty, init, buf, base_off);
        return;
    }

    StructField *next_field = ty->agg.fields;
    for (ASTNode *it = init->init_list.items; it; it = it->next) {
        ASTNode *value = it;
        StructField *field = next_field;

        if (it->kind == AST_DESIGNATOR) {
            if (!it->designator.is_field)
                continue;
            field = find_field(ty, it->designator.field);
            value = it->designator.value;
        }
        if (!field)
            break;
        fill_global_initializer(field->type, value, buf, base_off + field->offset);
        next_field = field->next;
    }
}

static void fill_global_initializer(Type *ty, ASTNode *init,
                                    unsigned char *buf, int base_off) {
    if (!ty || !init) return;
    if (init->kind == AST_COMPOUND_LIT)
        init = init->compound_lit.init;

    if (ty->kind == TY_ARRAY || ty->kind == TY_ARRAY_UNSIZED)
        fill_global_array(ty, init, buf, base_off);
    else if (ty->kind == TY_STRUCT || ty->kind == TY_UNION)
        fill_global_aggregate(ty, init, buf, base_off);
    else
        fill_global_scalar(ty, init, buf, base_off);
}

static void gen_scalar_or_nested_init(IrGen *g, IRValue addr, Type *ty, ASTNode *init) {
    if (init && init->kind == AST_INIT_LIST) {
        gen_initializer(g, addr, ty, init);
    } else if (init && is_aggregate_type(ty)) {
        IRValue src_addr = gen_aggregate_expr_addr(g, init);
        int sz = type_sizeof(ty);
        if (sz < 0) sz = 0;
        ir_emit(g->func, IR_MEMCPY, addr, src_addr, irv_int(sz, type_ulong()));
    } else if (init) {
        gen_store(g, addr, gen_expr(g, init));
    }
}

static void gen_zero_initializer(IrGen *g, IRValue addr, Type *ty) {
    if (!ty) return;
    if (ty->kind == TY_ARRAY) {
        Type *elem = ty->arr.elem;
        int elem_sz = type_sizeof(elem);
        if (elem_sz <= 0) elem_sz = 1;
        for (long long i = 0; i < ty->arr.count; i++) {
            IRValue elem_addr = gen_addr_offset(g, addr, (int)(i * elem_sz), elem);
            gen_zero_initializer(g, elem_addr, elem);
        }
    } else if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        for (StructField *f = ty->agg.fields; f; f = f->next) {
            IRValue field_addr = gen_addr_offset(g, addr, f->offset, f->type);
            gen_zero_initializer(g, field_addr, f->type);
        }
    } else {
        gen_store(g, addr, irv_int(0, ty));
    }
}

static void gen_array_initializer(IrGen *g, IRValue addr, Type *ty, ASTNode *init) {
    Type *elem = ty->arr.elem;
    int elem_sz = type_sizeof(elem);
    if (elem_sz <= 0) elem_sz = 1;

    long long next_index = 0;
    for (ASTNode *it = init->init_list.items; it; it = it->next) {
        ASTNode *value = it;
        long long index = next_index;

        if (it->kind == AST_DESIGNATOR) {
            if (it->designator.is_field)
                continue;
            index = const_int_value(it->designator.index);
            value = it->designator.value;
        }

        if (ty->kind == TY_ARRAY && (index < 0 || index >= ty->arr.count)) {
            next_index = index + 1;
            continue;
        }

        IRValue elem_addr = gen_addr_offset(g, addr, (int)(index * elem_sz), elem);
        gen_scalar_or_nested_init(g, elem_addr, elem, value);
        next_index = index + 1;
    }
}

static void gen_aggregate_initializer(IrGen *g, IRValue addr, Type *ty, ASTNode *init) {
    StructField *next_field = ty->agg.fields;
    for (ASTNode *it = init->init_list.items; it; it = it->next) {
        ASTNode *value = it;
        StructField *field = next_field;

        if (it->kind == AST_DESIGNATOR) {
            if (!it->designator.is_field)
                continue;
            field = find_field(ty, it->designator.field);
            value = it->designator.value;
        }
        if (!field)
            break;

        IRValue field_addr = gen_addr_offset(g, addr, field->offset, field->type);
        gen_scalar_or_nested_init(g, field_addr, field->type, value);
        next_field = field->next;
    }
}

static void gen_initializer(IrGen *g, IRValue addr, Type *ty, ASTNode *init) {
    if (!init) return;
    if (init->kind == AST_COMPOUND_LIT) {
        if (is_aggregate_type(ty)) {
            gen_initializer(g, addr, ty, init->compound_lit.init);
        } else {
            gen_store(g, addr, gen_expr(g, init));
        }
        return;
    }
    if (init->kind != AST_INIT_LIST) {
        if (is_aggregate_type(ty)) {
            IRValue src_addr = gen_aggregate_expr_addr(g, init);
            int sz = type_sizeof(ty);
            if (sz < 0) sz = 0;
            ir_emit(g->func, IR_MEMCPY, addr, src_addr, irv_int(sz, type_ulong()));
            return;
        }
        gen_store(g, addr, gen_expr(g, init));
        return;
    }

    gen_zero_initializer(g, addr, ty);

    if (ty->kind == TY_ARRAY || ty->kind == TY_ARRAY_UNSIZED) {
        gen_array_initializer(g, addr, ty, init);
    } else if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        gen_aggregate_initializer(g, addr, ty, init);
    } else if (init->init_list.items) {
        ASTNode *first = init->init_list.items;
        if (first->kind == AST_DESIGNATOR)
            first = first->designator.value;
        gen_scalar_or_nested_init(g, addr, ty, first);
    }
}

/* ── lvalue code generation (returns address of the lvalue) ──────── */
static IRValue gen_lvalue(IrGen *g, ASTNode *node) {
    switch (node->kind) {
    case AST_IDENT: {
        VarEntry *e = vscope_lookup(g, node->ident.name);
        if (e) return e->addr;
        /* Undeclared — return global ref */
        return irv_global(node->ident.name, type_pointer(g->types, type_int(), QUAL_NONE));
    }
    case AST_UNOP:
        if (node->unop.op == TOK_STAR)
            return gen_expr(g, node->unop.operand);
        break;
    case AST_SUBSCRIPT: {
        /* a[i] = *(a + i) */
        IRValue base = gen_expr(g, node->subscript.base);
        IRValue idx  = gen_expr(g, node->subscript.index);
        int t = ir_new_temp(g->func);
        Type *ty = node->type ? node->type : type_int();
        IRValue dst = irv_temp(t, type_pointer(g->types, ty, QUAL_NONE));
        /* Scale index by element size for correct pointer arithmetic */
        int esz = type_sizeof(ty);
        if (esz <= 0) esz = 1;
        if (esz != 1) {
            int st = ir_new_temp(g->func);
            IRValue scaled = irv_temp(st, type_long());
            ir_emit(g->func, IR_MUL, scaled, idx, irv_int(esz, type_long()));
            ir_emit(g->func, IR_ADD, dst, base, scaled);
        } else {
            ir_emit(g->func, IR_ADD, dst, base, idx);
        }
        return dst;
    }
    case AST_MEMBER: {
        /* Compute base address, add offset */
        IRValue base_addr = gen_lvalue(g, node->member.base);
        Type *bt = node->member.base->type;
        if (bt && (bt->kind == TY_STRUCT || bt->kind == TY_UNION)) {
            int offset = 0;
            StructField *f = find_field_with_offset(bt, node->member.field, &offset);
            if (f)
                return gen_addr_offset(g, base_addr, offset, f->type);
        }
        return base_addr;
    }
    case AST_MEMBER_PTR: {
        IRValue ptr = gen_expr(g, node->member.base);
        Type *pt = node->member.base->type;
        if (pt && pt->kind == TY_POINTER) pt = pt->ptr.pointee;
        if (pt && (pt->kind == TY_STRUCT || pt->kind == TY_UNION)) {
            int offset = 0;
            StructField *f = find_field_with_offset(pt, node->member.field, &offset);
            if (f)
                return gen_addr_offset(g, ptr, offset, f->type);
        }
        return ptr;
    }
    case AST_COMPOUND_LIT: {
        Type *ty = node->compound_lit.lit_type ? node->compound_lit.lit_type : type_int();
        IRValue addr = gen_alloca_value(g, ty);
        gen_initializer(g, addr, ty, node->compound_lit.init);
        return addr;
    }
    default: break;
    }
    /* Fallback: generate expression and treat result as address (error) */
    return gen_expr(g, node);
}

/* ── Expression code generation ──────────────────────────────────── */
static IRValue gen_expr(IrGen *g, ASTNode *node) {
    if (!node) return irv_int(0, type_int());

    switch (node->kind) {
    case AST_INT_LIT:
    case AST_CHAR_LIT:
        return irv_int((long long)node->int_lit.val,
                       node->type ? node->type : type_int());

    case AST_FLOAT_LIT:
        return irv_float(node->float_lit.val,
                         node->type ? node->type : type_double());

    case AST_STR_LIT: {
        /* Register the string in .rodata and return its label as a global ref */
        const char *label = ir_add_string_const(g->module, node->str_lit.str);
        return irv_global(label,
                          node->type ? node->type
                                     : type_pointer(g->types, type_char(), QUAL_NONE));
    }

    case AST_IDENT: {
        VarEntry *e = vscope_lookup(g, node->ident.name);
        if (e) {
            Type *ty = node->type ? node->type : type_int();
            /* Arrays and function pointers: return address directly (no load) */
            if (ty->kind == TY_ARRAY || ty->kind == TY_ARRAY_UNSIZED)
                return e->addr;
            if (ty->kind == TY_FUNC)
                return e->addr;
            if (is_aggregate_type(ty))
                return e->addr;
            return gen_load(g, e->addr, ty);
        }
        /* Undeclared identifier — treat as extern function reference */
        return irv_global(node->ident.name, node->type ? node->type : type_int());
    }

    case AST_UNOP: {
        if (node->unop.op == TOK_AMP) {
            return gen_lvalue(g, node->unop.operand);
        }
        if (node->unop.op == TOK_STAR) {
            IRValue addr = gen_expr(g, node->unop.operand);
            if (is_aggregate_type(node->type))
                return addr;
            return gen_load(g, addr, node->type ? node->type : type_int());
        }
        if (node->unop.op == TOK_PLUSPLUS || node->unop.op == TOK_MINUSMINUS) {
            IRValue addr = gen_lvalue(g, node->unop.operand);
            IRValue old  = gen_load(g, addr, node->type ? node->type : type_int());
            int t = ir_new_temp(g->func);
            IRValue dst = irv_temp(t, old.type);
            IRValue one = irv_int(pointer_pointee_size(old.type), type_int());
            ir_emit(g->func, node->unop.op == TOK_PLUSPLUS ? IR_ADD : IR_SUB,
                    dst, old, one);
            gen_store(g, addr, dst);
            return dst;  /* prefix: return new value */
        }
        IRValue operand = gen_expr(g, node->unop.operand);
        int t = ir_new_temp(g->func);
        Type *ty = node->type ? node->type : type_int();
        IRValue dst = irv_temp(t, ty);
        switch (node->unop.op) {
        case TOK_MINUS: ir_emit(g->func, IR_NEG, dst, operand, irv_none()); break;
        case TOK_TILDE: ir_emit(g->func, IR_NOT, dst, operand, irv_none()); break;
        case TOK_BANG:  /* logical not: (operand == 0) */
            ir_emit(g->func, IR_EQ, dst, operand, irv_int(0, type_int())); break;
        case TOK_PLUS:  return operand;
        default:        ir_emit(g->func, IR_COPY, dst, operand, irv_none()); break;
        }
        return dst;
    }

    case AST_POSTFIX: {
        IRValue addr = gen_lvalue(g, node->postfix.operand);
        IRValue old  = gen_load(g, addr, node->type ? node->type : type_int());
        int t = ir_new_temp(g->func);
        IRValue dst = irv_temp(t, old.type);
        IRValue one = irv_int(pointer_pointee_size(old.type), type_int());
        ir_emit(g->func, node->postfix.op == TOK_PLUSPLUS ? IR_ADD : IR_SUB,
                dst, old, one);
        gen_store(g, addr, dst);
        return old;  /* postfix: return old value */
    }

    case AST_BINOP: {
        /* Short-circuit for && and || */
        if (node->binop.op == TOK_AND || node->binop.op == TOK_OR) {
            int result_t = ir_new_temp(g->func);
            Type *ty = type_int();
            IRValue result = irv_temp(result_t, ty);
            int end_label = ir_new_label(g->func);
            int false_label = ir_new_label(g->func);
            int true_label = ir_new_label(g->func);

            IRValue lv = gen_expr(g, node->binop.left);

            if (node->binop.op == TOK_AND) {
                ir_emit_jz(g->func, lv, false_label);
                IRValue rv = gen_expr(g, node->binop.right);
                ir_emit_jnz(g->func, rv, true_label);
                ir_emit_jmp(g->func, false_label);
                ir_emit_label(g->func, false_label);
                ir_emit(g->func, IR_COPY, result, irv_int(0, ty), irv_none());
                ir_emit_jmp(g->func, end_label);
                ir_emit_label(g->func, true_label);
                ir_emit(g->func, IR_COPY, result, irv_int(1, ty), irv_none());
            } else {
                ir_emit_jnz(g->func, lv, true_label);
                IRValue rv = gen_expr(g, node->binop.right);
                ir_emit_jnz(g->func, rv, true_label);
                ir_emit(g->func, IR_COPY, result, irv_int(0, ty), irv_none());
                ir_emit_jmp(g->func, end_label);
                ir_emit_label(g->func, true_label);
                ir_emit(g->func, IR_COPY, result, irv_int(1, ty), irv_none());
            }
            ir_emit_label(g->func, end_label);
            return result;
        }

        IRValue lv = gen_expr(g, node->binop.left);
        IRValue rv = gen_expr(g, node->binop.right);
        int t = ir_new_temp(g->func);
        Type *ty = node->type ? node->type : type_int();
        IRValue dst = irv_temp(t, ty);

        Type *lt = node->binop.left->type;
        Type *rt = node->binop.right->type;
        if (node->binop.op == TOK_PLUS && lt && lt->kind == TY_POINTER &&
            (!rt || rt->kind != TY_POINTER)) {
            int esz = pointer_pointee_size(lt);
            if (esz != 1) {
                int st = ir_new_temp(g->func);
                IRValue scaled = irv_temp(st, type_long());
                ir_emit(g->func, IR_MUL, scaled, rv, irv_int(esz, type_long()));
                rv = scaled;
            }
            ir_emit(g->func, IR_ADD, dst, lv, rv);
            return dst;
        }
        if (node->binop.op == TOK_PLUS && rt && rt->kind == TY_POINTER &&
            (!lt || lt->kind != TY_POINTER)) {
            int esz = pointer_pointee_size(rt);
            if (esz != 1) {
                int st = ir_new_temp(g->func);
                IRValue scaled = irv_temp(st, type_long());
                ir_emit(g->func, IR_MUL, scaled, lv, irv_int(esz, type_long()));
                lv = scaled;
            }
            ir_emit(g->func, IR_ADD, dst, rv, lv);
            return dst;
        }
        if (node->binop.op == TOK_MINUS && lt && lt->kind == TY_POINTER &&
            rt && rt->kind == TY_POINTER) {
            ir_emit(g->func, IR_SUB, dst, lv, rv);
            int esz = pointer_pointee_size(lt);
            if (esz != 1) {
                int dt = ir_new_temp(g->func);
                IRValue divided = irv_temp(dt, ty);
                ir_emit(g->func, IR_DIV, divided, dst, irv_int(esz, type_long()));
                return divided;
            }
            return dst;
        }
        if (node->binop.op == TOK_MINUS && lt && lt->kind == TY_POINTER) {
            int esz = pointer_pointee_size(lt);
            if (esz != 1) {
                int st = ir_new_temp(g->func);
                IRValue scaled = irv_temp(st, type_long());
                ir_emit(g->func, IR_MUL, scaled, rv, irv_int(esz, type_long()));
                rv = scaled;
            }
            ir_emit(g->func, IR_SUB, dst, lv, rv);
            return dst;
        }

        ir_emit(g->func, tok_to_ir_binop(node->binop.op), dst, lv, rv);
        return dst;
    }

    case AST_ASSIGN: {
        IRValue addr = gen_lvalue(g, node->assign.left);
        IRValue rval;
        Type *lhs_ty = node->assign.left->type ? node->assign.left->type : node->type;
        StructField *bf = member_field(node->assign.left);
        if (node->assign.op == TOK_ASSIGN && is_aggregate_type(lhs_ty)) {
            IRValue src_addr = gen_aggregate_expr_addr(g, node->assign.right);
            int sz = type_sizeof(lhs_ty);
            if (sz < 0) sz = 0;
            ir_emit(g->func, IR_MEMCPY, addr, src_addr, irv_int(sz, type_ulong()));
            return addr;
        } else if (node->assign.op == TOK_ASSIGN && bf && bf->bit_width > 0) {
            rval = gen_expr(g, node->assign.right);
            return gen_bitfield_store(g, addr, bf, rval);
        } else if (node->assign.op == TOK_ASSIGN) {
            rval = gen_expr(g, node->assign.right);
        } else {
            /* Compound: load lvalue, apply op, store */
            IRValue lval = gen_load(g, addr, node->type ? node->type : type_int());
            IRValue rv   = gen_expr(g, node->assign.right);
            int t = ir_new_temp(g->func);
            Type *ty = node->type ? node->type : type_int();
            IRValue dst = irv_temp(t, ty);
            if (ty && ty->kind == TY_POINTER &&
                (node->assign.op == TOK_PLUS_ASSIGN || node->assign.op == TOK_MINUS_ASSIGN)) {
                int esz = pointer_pointee_size(ty);
                if (esz != 1) {
                    int st = ir_new_temp(g->func);
                    IRValue scaled = irv_temp(st, type_long());
                    ir_emit(g->func, IR_MUL, scaled, rv, irv_int(esz, type_long()));
                    rv = scaled;
                }
            }
            ir_emit(g->func, tok_to_ir_binop(compound_base(node->assign.op)),
                    dst, lval, rv);
            rval = dst;
        }
        gen_store(g, addr, rval);
        return rval;
    }

    case AST_TERNARY: {
        int then_label = ir_new_label(g->func);
        int else_label = ir_new_label(g->func);
        int end_label  = ir_new_label(g->func);
        int res_t = ir_new_temp(g->func);
        Type *ty = node->type ? node->type : type_int();
        IRValue result = irv_temp(res_t, ty);

        IRValue cond = gen_expr(g, node->ternary.cond);
        ir_emit_jz(g->func, cond, else_label);

        ir_emit_label(g->func, then_label);
        IRValue tv = gen_expr(g, node->ternary.then_expr);
        ir_emit(g->func, IR_COPY, result, tv, irv_none());
        ir_emit_jmp(g->func, end_label);

        ir_emit_label(g->func, else_label);
        IRValue ev = gen_expr(g, node->ternary.else_expr);
        ir_emit(g->func, IR_COPY, result, ev, irv_none());

        ir_emit_label(g->func, end_label);
        return result;
    }

    case AST_CALL: {
        /* Generate all arguments first */
        int nargs = node->call.n_args;
        IRValue *args = NULL;
        if (nargs > 0) {
            args = (IRValue *)arena_alloc(g->arena, sizeof(IRValue) * nargs,
                                           _Alignof(IRValue));
            int i = 0;
            for (ASTNode *a = node->call.args; a; a = a->next) {
                Type *aty = a->type ? a->type : type_int();
                if (is_aggregate_type(aty)) {
                    IRValue arg_addr = gen_aggregate_expr_addr(g, a);
                    /* Keep the by-value aggregate type on the operand.  The
                     * value itself is still the address of the object; codegen
                     * uses the type to classify the ABI chunks and then loads
                     * bytes from that address. */
                    arg_addr.type = aty;
                    args[i++] = arg_addr;
                } else {
                    args[i++] = gen_expr(g, a);
                }
            }
        }

        IRValue callee = gen_expr(g, node->call.func);

        Type *ret_ty = node->type ? node->type : type_int();
        IRValue dst;
        ABIClassification cls = abi_classify(ret_ty);
        int returns_aggregate = abi_in_memory(&cls);
        if (is_aggregate_type(ret_ty)) {
            dst = gen_alloca_value(g, ret_ty);
        } else {
            int t = ir_new_temp(g->func);
            dst = irv_temp(t, ret_ty);
        }
        IRInstr *ins = ir_emit(g->func, IR_CALL, dst, callee, irv_none());
        ins->call_args  = args;
        ins->call_nargs = nargs;
        ins->call_sret  = returns_aggregate;
        ins->call_ret_ty = ret_ty;
        return dst;
    }

    case AST_SUBSCRIPT: {
        IRValue addr = gen_lvalue(g, node);
        if (is_aggregate_type(node->type))
            return addr;
        return gen_load(g, addr, node->type ? node->type : type_int());
    }

    case AST_MEMBER: {
        IRValue addr = gen_lvalue(g, node);
        StructField *bf = member_field(node);
        if (bf && bf->bit_width > 0)
            return gen_bitfield_load(g, addr, bf);
        if (node->type && (node->type->kind == TY_ARRAY || node->type->kind == TY_ARRAY_UNSIZED))
            return addr;
        if (is_aggregate_type(node->type))
            return addr;
        return gen_load(g, addr, node->type ? node->type : type_int());
    }

    case AST_MEMBER_PTR: {
        IRValue addr = gen_lvalue(g, node);
        StructField *bf = member_field(node);
        if (bf && bf->bit_width > 0)
            return gen_bitfield_load(g, addr, bf);
        if (node->type && (node->type->kind == TY_ARRAY || node->type->kind == TY_ARRAY_UNSIZED))
            return addr;
        if (is_aggregate_type(node->type))
            return addr;
        return gen_load(g, addr, node->type ? node->type : type_int());
    }

    case AST_CAST: {
        IRValue src = gen_expr(g, node->cast.expr);
        Type *from = node->cast.expr->type;
        Type *to   = node->cast.cast_type;
        if (!from || from == to) return src;

        int t = ir_new_temp(g->func);
        IRValue dst = irv_temp(t, to);
        IROp conv = IR_COPY;

        if (type_is_integer(from) && type_is_integer(to)) {
            int from_sz = type_sizeof(from), to_sz = type_sizeof(to);
            if (to_sz > from_sz)
                conv = type_is_signed(from) ? IR_SEXT : IR_ZEXT;
            else if (to_sz < from_sz)
                conv = IR_TRUNC;
            else if (to_sz < 8 && type_is_signed(from) != type_is_signed(to))
                conv = IR_TRUNC;
        } else if (type_is_integer(from) && type_is_arithmetic(to)) {
            conv = IR_ITOF;
        } else if (type_is_arithmetic(from) && type_is_integer(to)) {
            conv = IR_FTOI;
        }
        ir_emit(g->func, conv, dst, src, irv_none());
        return dst;
    }

    case AST_SIZEOF_EXPR: {
        /* sizeof is a compile-time constant in our implementation.
         * Evaluate the type of the expression but don't emit code. */
        Type *ty = node->sizeof_expr.expr ? node->sizeof_expr.expr->type : type_int();
        int sz = ty ? type_sizeof(ty) : 0;
        if (sz < 0) sz = 0;
        return irv_int(sz, type_ulong());
    }

    case AST_SIZEOF_TYPE: {
        int sz = node->sizeof_type.sizeof_type ? type_sizeof(node->sizeof_type.sizeof_type) : 0;
        if (sz < 0) sz = 0;
        return irv_int(sz, type_ulong());
    }

    case AST_COMMA:
        gen_expr(g, node->comma.left);
        return gen_expr(g, node->comma.right);

    case AST_COMPOUND_LIT:
    {
        IRValue addr = gen_lvalue(g, node);
        Type *ty = node->type ? node->type : type_int();
        if (is_aggregate_type(ty) || ty->kind == TY_ARRAY || ty->kind == TY_ARRAY_UNSIZED)
            return addr;
        return gen_load(g, addr, ty);
    }

    default:
        return irv_int(0, type_int());
    }
}

/* ── Statement code generation ───────────────────────────────────── */
static void gen_stmt(IrGen *g, ASTNode *node) {
    if (!node) return;

    switch (node->kind) {
    case AST_COMPOUND: {
        VScope *saved = g->scope;
        g->scope = vscope_new(g->arena, g->scope);
        for (ASTNode *s = node->compound.stmts; s; s = s->next)
            gen_stmt(g, s);
        g->scope = saved;
        break;
    }

    case AST_EXPR_STMT:
        gen_expr(g, node->expr_stmt.expr);
        break;

    case AST_DECL_STMT: {
        ASTNode *d = node->decl_stmt.decl;
        for (ASTNode *v = d; v; v = v->next) {
            if (v->kind == AST_VAR_DECL) {
                Type *ty = v->var_decl.decl_type ? v->var_decl.decl_type : type_int();
                IRValue addr = gen_alloca_value(g, ty);

                vscope_define(g, v->var_decl.name, addr, 0);

                gen_initializer(g, addr, ty, v->var_decl.init);
            } else if (v->kind == AST_TYPEDEF_DECL) {
                /* Nothing to emit for typedefs */
            }
        }
        break;
    }

    case AST_IF: {
        int else_label = ir_new_label(g->func);
        int end_label  = ir_new_label(g->func);
        IRValue cond = gen_expr(g, node->if_stmt.cond);
        ir_emit_jz(g->func, cond, else_label);
        gen_stmt(g, node->if_stmt.then_stmt);
        if (node->if_stmt.else_stmt) ir_emit_jmp(g->func, end_label);
        ir_emit_label(g->func, else_label);
        if (node->if_stmt.else_stmt) {
            gen_stmt(g, node->if_stmt.else_stmt);
            ir_emit_label(g->func, end_label);
        }
        break;
    }

    case AST_WHILE: {
        int top_label  = ir_new_label(g->func);
        int body_label = ir_new_label(g->func);
        int end_label  = ir_new_label(g->func);
        int saved_brk  = g->break_label;
        int saved_cont = g->cont_label;
        g->break_label = end_label;
        g->cont_label  = top_label;

        ir_emit_label(g->func, top_label);
        IRValue cond = gen_expr(g, node->while_stmt.cond);
        ir_emit_jz(g->func, cond, end_label);
        ir_emit_label(g->func, body_label);
        gen_stmt(g, node->while_stmt.body);
        ir_emit_jmp(g->func, top_label);
        ir_emit_label(g->func, end_label);

        g->break_label = saved_brk;
        g->cont_label  = saved_cont;
        break;
    }

    case AST_DO_WHILE: {
        int body_label = ir_new_label(g->func);
        int cond_label = ir_new_label(g->func);
        int end_label  = ir_new_label(g->func);
        int saved_brk  = g->break_label;
        int saved_cont = g->cont_label;
        g->break_label = end_label;
        g->cont_label  = cond_label;

        ir_emit_label(g->func, body_label);
        gen_stmt(g, node->while_stmt.body);
        ir_emit_label(g->func, cond_label);
        IRValue cond = gen_expr(g, node->while_stmt.cond);
        ir_emit_jnz(g->func, cond, body_label);
        ir_emit_label(g->func, end_label);

        g->break_label = saved_brk;
        g->cont_label  = saved_cont;
        break;
    }

    case AST_FOR: {
        int top_label  = ir_new_label(g->func);
        int body_label = ir_new_label(g->func);
        int incr_label = ir_new_label(g->func);
        int end_label  = ir_new_label(g->func);
        int saved_brk  = g->break_label;
        int saved_cont = g->cont_label;
        g->break_label = end_label;
        g->cont_label  = incr_label;

        VScope *saved_scope = g->scope;
        g->scope = vscope_new(g->arena, g->scope);

        if (node->for_stmt.init) gen_stmt(g, node->for_stmt.init);

        ir_emit_label(g->func, top_label);
        if (node->for_stmt.cond) {
            IRValue cond = gen_expr(g, node->for_stmt.cond);
            ir_emit_jz(g->func, cond, end_label);
        }
        ir_emit_label(g->func, body_label);
        gen_stmt(g, node->for_stmt.body);
        ir_emit_label(g->func, incr_label);
        if (node->for_stmt.incr) gen_expr(g, node->for_stmt.incr);
        ir_emit_jmp(g->func, top_label);
        ir_emit_label(g->func, end_label);

        g->scope = saved_scope;
        g->break_label = saved_brk;
        g->cont_label  = saved_cont;
        break;
    }

    case AST_RETURN: {
        if (node->ret.expr) {
            Type *ret_ty = (g->func && g->func->func_type &&
                            g->func->func_type->kind == TY_FUNC)
                               ? g->func->func_type->func.ret
                               : (node->ret.expr->type ? node->ret.expr->type : type_int());
            IRValue rv = is_aggregate_type(ret_ty)
                             ? gen_aggregate_expr_addr(g, node->ret.expr)
                             : gen_expr(g, node->ret.expr);
            ir_emit(g->func, IR_RET, irv_none(), rv, irv_none());
        } else {
            ir_emit(g->func, IR_RET, irv_none(), irv_none(), irv_none());
        }
        break;
    }

    case AST_BREAK:
        if (g->break_label) ir_emit_jmp(g->func, g->break_label);
        break;

    case AST_CONTINUE:
        if (g->cont_label) ir_emit_jmp(g->func, g->cont_label);
        break;

    case AST_SWITCH: {
        int end_label = ir_new_label(g->func);
        int saved_brk = g->break_label;
        g->break_label = end_label;

        /* Save outer switch context and start fresh for this switch */
        CaseEntry *saved_cases   = g->case_entries;
        int        saved_default = g->sw_default_lbl;
        g->case_entries   = NULL;
        g->sw_default_lbl = 0;

        /* Pre-scan body to assign label IDs to all case/default nodes */
        collect_switch_cases(g, node->switch_stmt.body);

        /* Evaluate switch expression and keep it in a temp */
        IRValue sv = gen_expr(g, node->switch_stmt.expr);
        int sv_t = ir_new_temp(g->func);
        Type *sv_ty = sv.type ? sv.type : type_int();
        IRValue sv_copy = irv_temp(sv_t, sv_ty);
        ir_emit(g->func, IR_COPY, sv_copy, sv, irv_none());

        /* Emit comparison chain: for each case, test sv == val, jnz to label */
        for (CaseEntry *ce = g->case_entries; ce; ce = ce->next) {
            if (!ce->is_default) {
                int cmp_t = ir_new_temp(g->func);
                IRValue cmp_dst = irv_temp(cmp_t, type_int());
                ir_emit(g->func, IR_EQ, cmp_dst, sv_copy,
                        irv_int(ce->val, sv_ty));
                ir_emit_jnz(g->func, cmp_dst, ce->label_id);
            }
        }
        /* Fall through to default or skip body entirely */
        if (g->sw_default_lbl)
            ir_emit_jmp(g->func, g->sw_default_lbl);
        else
            ir_emit_jmp(g->func, end_label);

        /* Emit body — AST_CASE/AST_DEFAULT will emit their pre-assigned labels */
        gen_stmt(g, node->switch_stmt.body);
        ir_emit_label(g->func, end_label);

        g->case_entries   = saved_cases;
        g->sw_default_lbl = saved_default;
        g->break_label    = saved_brk;
        break;
    }

    case AST_CASE: {
        /* Find pre-assigned label and emit it, then fall through into body */
        for (CaseEntry *ce = g->case_entries; ce; ce = ce->next) {
            if (ce->node == node && !ce->is_default) {
                ir_emit_label(g->func, ce->label_id);
                break;
            }
        }
        gen_stmt(g, node->case_stmt.stmt);
        break;
    }

    case AST_DEFAULT: {
        for (CaseEntry *ce = g->case_entries; ce; ce = ce->next) {
            if (ce->node == node && ce->is_default) {
                ir_emit_label(g->func, ce->label_id);
                break;
            }
        }
        gen_stmt(g, node->default_stmt.stmt);
        break;
    }

    case AST_GOTO: {
        for (GotoLabel *gl = g->goto_labels; gl; gl = gl->next) {
            if (gl->name == node->go.label) {
                ir_emit_jmp(g->func, gl->label_id);
                break;
            }
        }
        break;
    }

    case AST_LABEL: {
        /* Emit pre-assigned label, then generate the labelled statement */
        for (GotoLabel *gl = g->goto_labels; gl; gl = gl->next) {
            if (gl->name == node->label.label) {
                ir_emit_label(g->func, gl->label_id);
                break;
            }
        }
        gen_stmt(g, node->label.stmt);
        break;
    }

    default:
        break;
    }
}

/* ── Function code generation ────────────────────────────────────── */
static void gen_func(IrGen *g, ASTNode *func_node) {
    IRFunc *f = ir_func_new(g->module, func_node->func_def.name,
                             func_node->func_def.func_type,
                             func_node->func_def.storage == SC_STATIC ? 1 : 0);
    g->func        = f;
    g->break_label = 0;
    g->cont_label  = 0;

    VScope *saved = g->scope;
    g->scope = vscope_new(g->arena, g->scope);

    Type *ret_ty = (f->func_type && f->func_type->kind == TY_FUNC)
                       ? f->func_type->func.ret
                       : type_int();
    ABIClassification ret_cls = abi_classify(ret_ty);
    if (abi_in_memory(&ret_cls)) {
        int sret_t = ir_new_temp(f);
        IRValue sret_val = irv_temp(sret_t, type_pointer(g->types, ret_ty, QUAL_NONE));
        IRInstr *sret_ins = ir_emit(f, IR_COPY, sret_val, irv_none(), irv_none());
        sret_ins->label_id = -1;
        f->sret_temp = sret_t;
    }

    int total_params = 0;
    for (ASTNode *p = func_node->func_def.params; p; p = p->next)
        total_params++;
    IRValue *param_addrs = NULL;
    IRValue *param_vals = NULL;
    Type **param_types = NULL;
    int *param_is_agg = NULL;
    if (total_params > 0) {
        param_addrs = (IRValue *)arena_alloc(g->arena, sizeof(IRValue) * total_params,
                                             _Alignof(IRValue));
        param_vals = (IRValue *)arena_alloc(g->arena, sizeof(IRValue) * total_params,
                                            _Alignof(IRValue));
        param_types = (Type **)arena_alloc(g->arena, sizeof(Type *) * total_params,
                                           _Alignof(Type *));
        param_is_agg = (int *)arena_alloc(g->arena, sizeof(int) * total_params,
                                          _Alignof(int));
        for (int i = 0; i < total_params; i++) {
            param_addrs[i] = irv_none();
            param_vals[i] = irv_none();
            param_types[i] = NULL;
            param_is_agg[i] = 0;
        }
    }

    /* Allocate parameter slots and capture all incoming registers before any
     * aggregate parameter copy can clobber later argument registers. */
    int param_idx = 0;
    for (ASTNode *p = func_node->func_def.params; p; p = p->next, param_idx++) {
        if (!p->param_decl.name) continue;
        Type *pty = p->param_decl.param_type ? p->param_decl.param_type : type_int();
        IRValue addr = gen_alloca_value(g, pty);

        if (is_aggregate_type(pty)) {
            IRInstr *param_ins = ir_emit(f, IR_COPY, addr, irv_none(), irv_none());
            param_ins->label_id = param_idx; /* abuse: encodes param position */
            param_ins->src2.type = pty;      /* aggregate type to capture */
            param_addrs[param_idx] = addr;
            param_vals[param_idx] = irv_none();
            param_types[param_idx] = pty;
            param_is_agg[param_idx] = 1;
        } else {
            /* The ABI passes params in registers; represent as a param temp */
            int param_t = ir_new_temp(f);
            IRValue param_val = irv_temp(param_t, pty);
            /* Mark this instruction specially: param_idx is stored in label_id */
            IRInstr *param_ins = ir_emit(f, IR_COPY, param_val, irv_none(), irv_none());
            param_ins->label_id = param_idx; /* abuse: encodes param position */
            param_addrs[param_idx] = addr;
            param_vals[param_idx] = param_val;
            param_types[param_idx] = pty;
            param_is_agg[param_idx] = 0;
        }

        vscope_define(g, p->param_decl.name, addr, 0);
    }
    for (int i = 0; i < total_params; i++) {
        if (param_vals[i].kind == IRV_NONE)
            continue;
        if (param_is_agg[i]) {
            continue;
        } else {
            gen_store(g, param_addrs[i], param_vals[i]);
        }
    }

    /* Pre-scan for named labels so goto can resolve forward references */
    g->goto_labels = NULL;
    collect_labels(g, func_node->func_def.body);

    gen_stmt(g, func_node->func_def.body);

    g->scope = saved;
    g->func  = NULL;
}

/* ── Module entry point ──────────────────────────────────────────── */
IRModule *irgen(ASTNode *root, Arena *arena, StringTable *strings, TypeTable *types) {
    if (!root) return NULL;

    IRModule *m = ir_module_new(arena);
    IrGen g;
    memset(&g, 0, sizeof(g));
    g.module  = m;
    g.arena   = arena;
    g.strings = strings;
    g.types   = types;
    g.scope   = vscope_new(arena, NULL);

    for (ASTNode *d = root->tu.decls; d; d = d->next) {
        switch (d->kind) {
        case AST_FUNC_DEF:
            gen_func(&g, d);
            break;
        case AST_VAR_DECL: {
            Type *ty = d->var_decl.decl_type ? d->var_decl.decl_type : type_int();
            const char *name = d->var_decl.name;
            if (!name) break;

            /* Register in global scope so gen_expr can emit IR_LOAD for reads */
            Type *ptr_ty = type_pointer(types, ty, QUAL_NONE);
            IRValue gaddr = irv_global(name, ptr_ty);
            vscope_define(&g, name, gaddr, 1);

            /* Emit definition unless this is an extern declaration or a function type */
            int is_func = (ty && ty->kind == TY_FUNC);
            if (d->var_decl.storage != SC_EXTERN && !is_func) {
                long long init_val = 0;
                int has_init = 0;
                unsigned char *init_data = NULL;
                int init_size = 0;
                if (d->var_decl.init && d->var_decl.init->kind != AST_INIT_LIST) {
                    init_val = eval_const_expr(d->var_decl.init);
                    has_init = 1;
                } else if (d->var_decl.init) {
                    int sz = type_sizeof(ty);
                    if (sz > 0) {
                        init_data = (unsigned char *)arena_alloc(arena, (size_t)sz, 1);
                        memset(init_data, 0, (size_t)sz);
                        fill_global_initializer(ty, d->var_decl.init, init_data, 0);
                        init_size = sz;
                        has_init = 1;
                    }
                }
                int is_static = (d->var_decl.storage == SC_STATIC) ? 1 : 0;
                IRGlobalVar *gv = ir_add_global_var(m, name, ty, init_val, has_init, is_static);
                gv->init_data = init_data;
                gv->init_size = init_size;
            }
            break;
        }
        default:
            break;
        }
    }

    return m;
}
