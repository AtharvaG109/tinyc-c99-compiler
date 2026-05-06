/*
 * ir.c — IR module / function management and pretty printer
 */
#include "tinyc/ir.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

IRModule *ir_module_new(Arena *arena) {
    IRModule *m = (IRModule *)arena_alloc(arena, sizeof(IRModule), _Alignof(IRModule));
    memset(m, 0, sizeof(*m));
    m->arena = arena;
    m->cap   = 16;
    m->funcs = (IRFunc **)arena_alloc(arena, sizeof(IRFunc *) * m->cap,
                                       _Alignof(IRFunc *));
    return m;
}

const char *ir_add_string_const(IRModule *m, const char *data) {
    /* Check for an existing entry with the same content */
    for (IRStrConst *s = m->str_consts; s; s = s->next)
        if (strcmp(s->data, data) == 0) return s->label;

    /* Build label ".Lstr<N>" */
    char buf[32];
    snprintf(buf, sizeof(buf), ".Lstr%d", m->next_str_id++);
    const char *label = arena_strdup(m->arena, buf, strlen(buf));

    IRStrConst *sc = (IRStrConst *)arena_alloc(m->arena, sizeof(IRStrConst),
                                                _Alignof(IRStrConst));
    sc->label      = label;
    sc->data       = data;   /* already interned in strtab */
    sc->next       = m->str_consts;
    m->str_consts  = sc;
    return label;
}

IRFunc *ir_func_new(IRModule *m, const char *name, Type *func_type, int is_static) {
    if (m->nfuncs == m->cap) {
        int new_cap = m->cap * 2;
        IRFunc **nf = (IRFunc **)arena_alloc(m->arena, sizeof(IRFunc *) * new_cap,
                                              _Alignof(IRFunc *));
        memcpy(nf, m->funcs, sizeof(IRFunc *) * m->nfuncs);
        m->funcs = nf;
        m->cap = new_cap;
    }
    IRFunc *f = (IRFunc *)arena_alloc(m->arena, sizeof(IRFunc), _Alignof(IRFunc));
    memset(f, 0, sizeof(*f));
    f->name      = name;
    f->func_type = func_type;
    f->is_static = is_static;
    f->arena     = m->arena;
    f->cap       = 64;
    f->instrs    = (IRInstr *)arena_alloc(m->arena, sizeof(IRInstr) * f->cap,
                                           _Alignof(IRInstr));
    f->next_temp  = 1;
    f->next_label = 1;
    m->funcs[m->nfuncs++] = f;
    return f;
}

IRInstr *ir_emit(IRFunc *f, IROp op, IRValue dst, IRValue src1, IRValue src2) {
    if (f->ninstr == f->cap) {
        /* Allocate a fresh block of 2x size and copy — the old block leaks in
         * the arena, which is fine since arena_destroy reclaims everything. */
        int new_cap = f->cap * 2;
        IRInstr *ni = (IRInstr *)arena_alloc(f->arena, sizeof(IRInstr) * new_cap,
                                              _Alignof(IRInstr));
        memcpy(ni, f->instrs, sizeof(IRInstr) * f->ninstr);
        f->instrs = ni;
        f->cap    = new_cap;
    }
    IRInstr *instr = &f->instrs[f->ninstr++];
    memset(instr, 0, sizeof(*instr));
    instr->op   = op;
    instr->dst  = dst;
    instr->src1 = src1;
    instr->src2 = src2;
    return instr;
}

int ir_new_temp(IRFunc *f)  { return f->next_temp++; }
int ir_new_label(IRFunc *f) { return f->next_label++; }

void ir_emit_label(IRFunc *f, int label_id) {
    IRValue none = irv_none();
    IRInstr *i = ir_emit(f, IR_LABEL, none, none, none);
    i->label_id = label_id;
}

void ir_emit_jmp(IRFunc *f, int label_id) {
    IRValue none = irv_none();
    IRInstr *i = ir_emit(f, IR_JMP, none, none, none);
    i->label_id = label_id;
}

void ir_emit_jz(IRFunc *f, IRValue cond, int label_id) {
    IRValue none = irv_none();
    IRInstr *i = ir_emit(f, IR_JZ, none, cond, none);
    i->label_id = label_id;
}

IRGlobalVar *ir_add_global_var(IRModule *m, const char *name, Type *type,
                                long long init_val, int has_init, int is_static) {
    IRGlobalVar *gv = (IRGlobalVar *)arena_alloc(m->arena, sizeof(IRGlobalVar),
                                                  _Alignof(IRGlobalVar));
    gv->name      = name;
    gv->type      = type;
    gv->init_val  = init_val;
    gv->has_init  = has_init;
    gv->init_data = NULL;
    gv->init_size = 0;
    gv->is_static = is_static;
    gv->next      = m->global_vars;
    m->global_vars = gv;
    return gv;
}

void ir_emit_jnz(IRFunc *f, IRValue cond, int label_id) {
    IRValue none = irv_none();
    IRInstr *i = ir_emit(f, IR_JNZ, none, cond, none);
    i->label_id = label_id;
}

/* ── Pretty printer ──────────────────────────────────────────────── */

static void print_val(const IRValue *v, FILE *out) {
    switch (v->kind) {
    case IRV_NONE:       fprintf(out, "-"); break;
    case IRV_TEMP:       fprintf(out, "t%d", v->temp_id); break;
    case IRV_CONST_INT:  fprintf(out, "%lld", v->const_int); break;
    case IRV_CONST_FLOAT:fprintf(out, "%g", v->const_float); break;
    case IRV_GLOBAL:     fprintf(out, "@%s", v->global); break;
    }
}

static const char *op_name(IROp op) {
    switch (op) {
    case IR_NOP:        return "nop";
    case IR_COPY:       return "copy";
    case IR_LOAD:       return "load";
    case IR_STORE:      return "store";
    case IR_ADDR:       return "addr";
    case IR_LOAD_IMM:   return "imm";
    case IR_ADD:        return "add";
    case IR_SUB:        return "sub";
    case IR_MUL:        return "mul";
    case IR_DIV:        return "div";
    case IR_MOD:        return "mod";
    case IR_AND:        return "and";
    case IR_OR:         return "or";
    case IR_XOR:        return "xor";
    case IR_SHL:        return "shl";
    case IR_SHR:        return "shr";
    case IR_NEG:        return "neg";
    case IR_NOT:        return "not";
    case IR_EQ:         return "eq";
    case IR_NE:         return "ne";
    case IR_LT:         return "lt";
    case IR_LE:         return "le";
    case IR_GT:         return "gt";
    case IR_GE:         return "ge";
    case IR_SEXT:       return "sext";
    case IR_ZEXT:       return "zext";
    case IR_TRUNC:      return "trunc";
    case IR_ITOF:       return "itof";
    case IR_FTOI:       return "ftoi";
    case IR_LABEL:      return "label";
    case IR_JMP:        return "jmp";
    case IR_JZ:         return "jz";
    case IR_JNZ:        return "jnz";
    case IR_CALL:       return "call";
    case IR_RET:        return "ret";
    case IR_ALLOCA:     return "alloca";
    case IR_MEMCPY:     return "memcpy";
    case IR_GLOBAL_DEF: return "global";
    }
    return "?";
}

void ir_print_func(const IRFunc *f, FILE *out) {
    fprintf(out, "func %s:\n", f->name);
    for (int i = 0; i < f->ninstr; i++) {
        const IRInstr *ins = &f->instrs[i];
        if (ins->op == IR_LABEL) {
            fprintf(out, "L%d:\n", ins->label_id);
            continue;
        }
        fprintf(out, "  %-8s  ", op_name(ins->op));
        if (ins->op == IR_JMP || ins->op == IR_JZ || ins->op == IR_JNZ) {
            if (ins->op != IR_JMP) { print_val(&ins->src1, out); fprintf(out, " -> "); }
            fprintf(out, "L%d", ins->label_id);
        } else if (ins->op == IR_RET) {
            if (ins->src1.kind != IRV_NONE) print_val(&ins->src1, out);
        } else if (ins->op == IR_ALLOCA) {
            print_val(&ins->dst, out);
            fprintf(out, "  [size=%d align=%d]", ins->alloca_size, ins->alloca_align);
        } else if (ins->op == IR_CALL) {
            print_val(&ins->dst, out); fprintf(out, " = call ");
            print_val(&ins->src1, out); fprintf(out, "(");
            for (int j = 0; j < ins->call_nargs; j++) {
                if (j > 0) fprintf(out, ", ");
                print_val(&ins->call_args[j], out);
            }
            fprintf(out, ")");
        } else {
            if (ins->dst.kind != IRV_NONE) { print_val(&ins->dst, out); fprintf(out, " = "); }
            if (ins->src1.kind != IRV_NONE) print_val(&ins->src1, out);
            if (ins->src2.kind != IRV_NONE) { fprintf(out, ", "); print_val(&ins->src2, out); }
        }
        fprintf(out, "\n");
    }
}

void ir_print(const IRModule *m, FILE *out) {
    for (int i = 0; i < m->nfuncs; i++) {
        ir_print_func(m->funcs[i], out);
        fprintf(out, "\n");
    }
}
