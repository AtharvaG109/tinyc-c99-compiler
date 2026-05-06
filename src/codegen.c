/*
 * codegen.c — x86-64 AT&T-syntax code generation
 *
 * Calling convention: System V AMD64 ABI
 *   Integer args:  rdi, rsi, rdx, rcx, r8, r9  (first 6)
 *   Return value:  rax
 *   Caller-saved:  rax, rcx, rdx, rsi, rdi, r8-r11
 *   Callee-saved:  rbx, r12-r15, rbp, rsp
 *
 * Scratch registers used here:
 *   %rax  — primary (result / src1)
 *   %rcx  — secondary (src2 / shift count)
 *   %rdx  — tertiary (idiv remainder, memcpy size)
 */
#include "tinyc/codegen.h"
#include "tinyc/regalloc.h"
#include "tinyc/type.h"
#include "tinyc/arena.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "tinyc/abi_classify.h"

typedef struct {
    int int_regs_used;
    int sse_regs_used;
    int stack_offset;
} ArgState;

static void compute_arg_state(const IRFunc *f, int up_to_param, ArgState *st) {
    st->int_regs_used = 0;
    st->sse_regs_used = 0;
    st->stack_offset = 16;

    Type *ret_ty = (f && f->func_type && f->func_type->kind == TY_FUNC) ? f->func_type->func.ret : type_int();
    ABIClassification ret_cls = abi_classify(ret_ty);
    if (abi_in_memory(&ret_cls)) {
        st->int_regs_used = 1;
    }

    if (!f || !f->func_type || f->func_type->kind != TY_FUNC) return;

    FuncParam *p = f->func_type->func.params;
    for (int i = 0; i < up_to_param && p; i++, p = p->next) {
        Type *pty = p->type;
        ABIClassification cls = abi_classify(pty);

        if (abi_in_memory(&cls)) {
            int sz = type_sizeof(pty);
            if (sz < 0) sz = 0;
            st->stack_offset += (sz + 7) & ~7;
        } else {
            int needed_int = 0, needed_sse = 0;
            for (int j = 0; j < cls.n_eightbytes; j++) {
                if (cls.classes[j] == ABI_CLASS_INTEGER) needed_int++;
                else if (cls.classes[j] == ABI_CLASS_SSE) needed_sse++;
            }
            if (st->int_regs_used + needed_int <= 6 && st->sse_regs_used + needed_sse <= 8) {
                st->int_regs_used += needed_int;
                st->sse_regs_used += needed_sse;
            } else {
                int sz = type_sizeof(pty);
                if (sz < 0) sz = 0;
                st->stack_offset += (sz + 7) & ~7;
            }
        }
    }
}

static const char *to_32bit(const char *reg64) {
    if (strcmp(reg64, "%rax") == 0) return "%eax";
    if (strcmp(reg64, "%rcx") == 0) return "%ecx";
    if (strcmp(reg64, "%rdx") == 0) return "%edx";
    if (strcmp(reg64, "%rbx") == 0) return "%ebx";
    if (strcmp(reg64, "%rsi") == 0) return "%esi";
    if (strcmp(reg64, "%rdi") == 0) return "%edi";
    if (strcmp(reg64, "%rbp") == 0) return "%ebp";
    if (strcmp(reg64, "%rsp") == 0) return "%esp";
    if (strcmp(reg64, "%r8") == 0) return "%r8d";
    if (strcmp(reg64, "%r9") == 0) return "%r9d";
    if (strcmp(reg64, "%r10") == 0) return "%r10d";
    if (strcmp(reg64, "%r11") == 0) return "%r11d";
    return reg64;
}

static const char *to_16bit(const char *reg64) {
    if (strcmp(reg64, "%rax") == 0) return "%ax";
    if (strcmp(reg64, "%rcx") == 0) return "%cx";
    if (strcmp(reg64, "%rdx") == 0) return "%dx";
    if (strcmp(reg64, "%rsi") == 0) return "%si";
    if (strcmp(reg64, "%rdi") == 0) return "%di";
    if (strcmp(reg64, "%r8") == 0) return "%r8w";
    if (strcmp(reg64, "%r9") == 0) return "%r9w";
    return reg64;
}

static const char *to_8bit(const char *reg64) {
    if (strcmp(reg64, "%rax") == 0) return "%al";
    if (strcmp(reg64, "%rcx") == 0) return "%cl";
    if (strcmp(reg64, "%rdx") == 0) return "%dl";
    if (strcmp(reg64, "%rsi") == 0) return "%sil";
    if (strcmp(reg64, "%rdi") == 0) return "%dil";
    if (strcmp(reg64, "%r8") == 0) return "%r8b";
    if (strcmp(reg64, "%r9") == 0) return "%r9b";
    return reg64;
}

static void emit_load_eightbyte_from_mem(FILE *out, const char *base_reg, int byte_off, int remaining, const char *dst_reg) {
    if (remaining >= 8) {
        fprintf(out, "\tmovq %d(%s), %s\n", byte_off, base_reg, dst_reg);
    } else if (remaining >= 4) {
        fprintf(out, "\tmovl %d(%s), %s\n", byte_off, base_reg, to_32bit(dst_reg));
    } else if (remaining >= 2) {
        fprintf(out, "\tmovzwq %d(%s), %s\n", byte_off, base_reg, dst_reg);
    } else {
        fprintf(out, "\tmovzbq %d(%s), %s\n", byte_off, base_reg, dst_reg);
    }
}

static void emit_store_eightbyte_to_mem(FILE *out, const char *base_reg, int byte_off, int remaining, const char *src_reg) {
    if (remaining >= 8) {
        fprintf(out, "\tmovq %s, %d(%s)\n", src_reg, byte_off, base_reg);
    } else if (remaining >= 4) {
        fprintf(out, "\tmovl %s, %d(%s)\n", to_32bit(src_reg), byte_off, base_reg);
    } else if (remaining >= 2) {
        fprintf(out, "\tmovw %s, %d(%s)\n", to_16bit(src_reg), byte_off, base_reg);
    } else if (remaining >= 1) {
        fprintf(out, "\tmovb %s, %d(%s)\n", to_8bit(src_reg), byte_off, base_reg);
    }
}

static void emit_copy_bytes(FILE *out, const char *dst_reg, const char *src_reg, int size) {
    int off = 0;
    while (off + 8 <= size) {
        fprintf(out, "\tmovq %d(%s), %%rax\n", off, src_reg);
        fprintf(out, "\tmovq %%rax, %d(%s)\n", off, dst_reg);
        off += 8;
    }
    if (off + 4 <= size) {
        fprintf(out, "\tmovl %d(%s), %%eax\n", off, src_reg);
        fprintf(out, "\tmovl %%eax, %d(%s)\n", off, dst_reg);
        off += 4;
    }
    if (off + 2 <= size) {
        fprintf(out, "\tmovw %d(%s), %%ax\n", off, src_reg);
        fprintf(out, "\tmovw %%ax, %d(%s)\n", off, dst_reg);
        off += 2;
    }
    if (off < size) {
        fprintf(out, "\tmovb %d(%s), %%al\n", off, src_reg);
        fprintf(out, "\tmovb %%al, %d(%s)\n", off, dst_reg);
    }
}

/* ── Platform symbol prefix ──────────────────────────────────────── */
/* Local labels (starting with '.') never get a prefix. */
#ifdef __APPLE__
static void fprint_sym(FILE *out, const char *name) {
    if (name[0] == '.') fputs(name, out);
    else fprintf(out, "_%s", name);
}
#else
static void fprint_sym(FILE *out, const char *name) { fputs(name, out); }
#endif

static void emit_global_addr(FILE *out, const char *name, const char *reg) {
#ifdef __APPLE__
    if (name[0] == '.') {
        fprintf(out, "\tleaq ");
        fprint_sym(out, name);
        fprintf(out, "(%%rip), %s\n", reg);
    } else {
        fprintf(out, "\tmovq ");
        fprint_sym(out, name);
        fprintf(out, "@GOTPCREL(%%rip), %s\n", reg);
    }
#else
    fprintf(out, "\tleaq ");
    fprint_sym(out, name);
    fprintf(out, "(%%rip), %s\n", reg);
#endif
}

/* ── Load an IRValue into a 64-bit general-purpose register ───────── */
static void load_reg(FILE *out, const RegAlloc *ra, IRValue v, const char *reg) {
    switch (v.kind) {
    case IRV_NONE:
        break;
    case IRV_TEMP:
        fprintf(out, "\tmovq %d(%%rbp), %s\n", ra->slot[v.temp_id], reg);
        break;
    case IRV_CONST_INT:
        fprintf(out, "\tmovq $%lld, %s\n", v.const_int, reg);
        break;
    case IRV_CONST_FLOAT:
        fprintf(out, "\txorq %s, %s\n", reg, reg);
        break;
    case IRV_GLOBAL:
        emit_global_addr(out, v.global, reg);
        break;
    }
}

/* ── Store %reg into an IRV_TEMP slot ────────────────────────────── */
static void store_reg(FILE *out, const RegAlloc *ra, IRValue dst, const char *reg) {
    if (dst.kind != IRV_TEMP) return;
    fprintf(out, "\tmovq %s, %d(%%rbp)\n", reg, ra->slot[dst.temp_id]);
}

static void normalize_rax_to_type(FILE *out, Type *ty) {
    if (!ty || !type_is_integer(ty))
        return;
    int sz = type_sizeof(ty);
    int is_signed = type_is_signed(ty);
    switch (sz) {
    case 1:
        fprintf(out, is_signed ? "\tmovsbq %%al, %%rax\n"
                               : "\tmovzbq %%al, %%rax\n");
        break;
    case 2:
        fprintf(out, is_signed ? "\tmovswq %%ax, %%rax\n"
                               : "\tmovzwq %%ax, %%rax\n");
        break;
    case 4:
        fprintf(out, is_signed ? "\tmovslq %%eax, %%rax\n"
                               : "\tmovl %%eax, %%eax\n");
        break;
    default:
        break;
    }
}

static int is_fp_type(Type *ty) {
    return ty && (ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE);
}

static int is_double_type(Type *ty) {
    return ty && ty->kind == TY_DOUBLE;
}

static int is_aggregate_type(Type *ty) {
    return ty && (ty->kind == TY_STRUCT || ty->kind == TY_UNION);
}

static void load_xmm(FILE *out, const RegAlloc *ra, IRValue v, const char *xmm) {
    Type *ty = v.type;
    switch (v.kind) {
    case IRV_NONE:
        break;
    case IRV_TEMP:
        fprintf(out, is_double_type(ty) ? "\tmovsd %d(%%rbp), %s\n"
                                        : "\tmovss %d(%%rbp), %s\n",
                ra->slot[v.temp_id], xmm);
        break;
    case IRV_CONST_FLOAT:
        if (is_double_type(ty)) {
            union { double d; uint64_t u; } cv;
            cv.d = v.const_float;
            fprintf(out, "\tmovabsq $%llu, %%rax\n", (unsigned long long)cv.u);
            fprintf(out, "\tmovq %%rax, %s\n", xmm);
        } else {
            union { float f; uint32_t u; } cv;
            cv.f = (float)v.const_float;
            fprintf(out, "\tmovl $%u, %%eax\n", (unsigned)cv.u);
            fprintf(out, "\tmovd %%eax, %s\n", xmm);
        }
        break;
    case IRV_CONST_INT:
        fprintf(out, "\tmovq $%lld, %%rax\n", v.const_int);
        fprintf(out, is_double_type(ty) ? "\tcvtsi2sdq %%rax, %s\n"
                                        : "\tcvtsi2ssq %%rax, %s\n", xmm);
        break;
    case IRV_GLOBAL:
        emit_global_addr(out, v.global, "%rax");
        fprintf(out, is_double_type(ty) ? "\tmovsd (%%rax), %s\n"
                                        : "\tmovss (%%rax), %s\n", xmm);
        break;
    }
}

static void store_xmm(FILE *out, const RegAlloc *ra, IRValue dst, const char *xmm) {
    if (dst.kind != IRV_TEMP) return;
    fprintf(out, is_double_type(dst.type) ? "\tmovsd %s, %d(%%rbp)\n"
                                          : "\tmovss %s, %d(%%rbp)\n",
            xmm, ra->slot[dst.temp_id]);
}



/* ── Emit code for one function ──────────────────────────────────── */
static void codegen_func(const IRFunc *f, FILE *out) {
    Arena *tmp = arena_create(0);
    RegAlloc ra;
    regalloc_run(f, &ra, tmp);

    /* --- Header --- */
#ifdef __APPLE__
    if (!f->is_static) {
        fprintf(out, "\t.globl _");
        fputs(f->name, out);
        fprintf(out, "\n");
    }
    fprintf(out, "_");
    fputs(f->name, out);
    fprintf(out, ":\n");
#else
    if (!f->is_static) {
        fprintf(out, "\t.globl ");
        fputs(f->name, out);
        fprintf(out, "\n");
    }
    fprintf(out, "\t.type ");
    fputs(f->name, out);
    fprintf(out, ", @function\n");
    fputs(f->name, out);
    fprintf(out, ":\n");
#endif

    /* --- Prologue --- */
    fprintf(out, "\tpushq %%rbp\n");
    fprintf(out, "\tmovq %%rsp, %%rbp\n");
    fprintf(out, "\tsubq $%d, %%rsp\n", ra.frame_size);

    /* --- Instruction selection --- */
    for (int i = 0; i < f->ninstr; i++) {
        const IRInstr *ins = &f->instrs[i];

        switch (ins->op) {

        case IR_NOP:
            break;

        case IR_LABEL:
            fprintf(out, ".L%s_%d:\n", f->name, ins->label_id);
            break;

        /* dst = stack address of local region */
        case IR_ALLOCA:
            if (ins->dst.kind == IRV_TEMP) {
                int off = ra.alloca_off[ins->dst.temp_id];
                fprintf(out, "\tleaq %d(%%rbp), %%rax\n", off);
                store_reg(out, &ra, ins->dst, "%rax");
            }
            break;

        /* dst = *src1 */
        case IR_LOAD: {
            load_reg(out, &ra, ins->src1, "%rax");
            Type *ty = ins->dst.type;
            if (is_fp_type(ty)) {
                fprintf(out, is_double_type(ty) ? "\tmovsd (%%rax), %%xmm0\n"
                                                : "\tmovss (%%rax), %%xmm0\n");
                store_xmm(out, &ra, ins->dst, "%xmm0");
                break;
            }
            int sz = (ty && type_sizeof(ty) > 0) ? type_sizeof(ty) : 8;
            int is_signed = ty ? type_is_signed(ty) : 1;
            switch (sz) {
            case 1:
                fprintf(out, is_signed ? "\tmovsbq (%%rax), %%rdx\n"
                                       : "\tmovzbq (%%rax), %%rdx\n");
                break;
            case 2:
                fprintf(out, is_signed ? "\tmovswq (%%rax), %%rdx\n"
                                       : "\tmovzwq (%%rax), %%rdx\n");
                break;
            case 4:
                fprintf(out, is_signed ? "\tmovslq (%%rax), %%rdx\n"
                                       : "\tmovl (%%rax), %%edx\n");
                break;
            default:
                fprintf(out, "\tmovq (%%rax), %%rdx\n");
                break;
            }
            store_reg(out, &ra, ins->dst, "%rdx");
            break;
        }

        /* *dst = src1 */
        case IR_STORE: {
            /* Use %r10 (caller-saved, not an arg reg) for the address to avoid
             * clobbering %rcx which carries the 4th integer argument. */
            load_reg(out, &ra, ins->dst,  "%r10");   /* pointer */
            Type *ty = ins->src1.type;
            if (ins->dst.type && ins->dst.type->kind == TY_POINTER)
                ty = ins->dst.type->ptr.pointee;
            if (is_fp_type(ty)) {
                IRValue src = ins->src1;
                src.type = ty;
                load_xmm(out, &ra, src, "%xmm0");
                fprintf(out, is_double_type(ty) ? "\tmovsd %%xmm0, (%%r10)\n"
                                                : "\tmovss %%xmm0, (%%r10)\n");
                break;
            }
            load_reg(out, &ra, ins->src1, "%rax");   /* value */
            int sz = (ty && type_sizeof(ty) > 0) ? type_sizeof(ty) : 8;
            switch (sz) {
            case 1: fprintf(out, "\tmovb %%al, (%%r10)\n");  break;
            case 2: fprintf(out, "\tmovw %%ax, (%%r10)\n");  break;
            case 4: fprintf(out, "\tmovl %%eax, (%%r10)\n"); break;
            default:fprintf(out, "\tmovq %%rax, (%%r10)\n"); break;
            }
            break;
        }

        case IR_COPY:
        case IR_LOAD_IMM:
            /* irgen uses IR_COPY with src1=IRV_NONE and label_id=N for parameter N */
            if (ins->src1.kind == IRV_NONE && ins->op == IR_COPY) {
                static const char *param_regs[6] = {
                    "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"
                };
                static const char *xmm_regs[8] = {
                    "%xmm0", "%xmm1", "%xmm2", "%xmm3",
                    "%xmm4", "%xmm5", "%xmm6", "%xmm7"
                };
                int pi = ins->label_id;
                if (pi < 0) {
                    store_reg(out, &ra, ins->dst, "%rdi");
                    break;
                }
                ArgState st;
                compute_arg_state(f, pi, &st);

                Type *pty = is_aggregate_type(ins->src2.type) ? ins->src2.type : ins->dst.type;
                int copy_to_addr = is_aggregate_type(ins->src2.type);
                ABIClassification cls = abi_classify(pty);
                int slot = ra.slot[ins->dst.temp_id];

                if (abi_in_memory(&cls)) {
                    /* Passed in memory on the caller's stack. */
                    int sz = type_sizeof(pty);
                    if (sz < 0) sz = 0;
                    if (copy_to_addr)
                        load_reg(out, &ra, ins->dst, "%r11");
                    else
                        fprintf(out, "\tleaq %d(%%rbp), %%r11\n", slot);
                    fprintf(out, "\tleaq %d(%%rbp), %%r10\n", st.stack_offset);
                    emit_copy_bytes(out, "%r11", "%r10", sz);
                } else {
                    /* Passed in registers or stack (if spilled) */
                    int needed_int = 0, needed_sse = 0;
                    for (int j = 0; j < cls.n_eightbytes; j++) {
                        if (cls.classes[j] == ABI_CLASS_INTEGER) needed_int++;
                        else if (cls.classes[j] == ABI_CLASS_SSE) needed_sse++;
                    }
                    if (st.int_regs_used + needed_int <= 6 && st.sse_regs_used + needed_sse <= 8) {
                        /* Load from registers */
                        if (copy_to_addr)
                            load_reg(out, &ra, ins->dst, "%r11");
                        for (int j = 0; j < cls.n_eightbytes; j++) {
                            int byte_off = j * 8;
                            int remaining = type_sizeof(pty) - byte_off;
                            if (cls.classes[j] == ABI_CLASS_INTEGER) {
                                const char *src = param_regs[st.int_regs_used++];
                                if (copy_to_addr) emit_store_eightbyte_to_mem(out, "%r11", byte_off, remaining, src);
                                else if (remaining >= 8) fprintf(out, "\tmovq %s, %d(%%rbp)\n", src, slot + byte_off);
                                else if (remaining >= 4) fprintf(out, "\tmovl %s, %d(%%rbp)\n", to_32bit(src), slot + byte_off);
                                else if (remaining >= 2) fprintf(out, "\tmovw %s, %d(%%rbp)\n", to_16bit(src), slot + byte_off);
                                else fprintf(out, "\tmovb %s, %d(%%rbp)\n", to_8bit(src), slot + byte_off);
                            } else if (cls.classes[j] == ABI_CLASS_SSE) {
                                const char *src = xmm_regs[st.sse_regs_used++];
                                if (copy_to_addr)
                                    fprintf(out, remaining >= 8 ? "\tmovsd %s, %d(%%r11)\n" : "\tmovss %s, %d(%%r11)\n", src, byte_off);
                                else
                                    fprintf(out, remaining >= 8 ? "\tmovsd %s, %d(%%rbp)\n" : "\tmovss %s, %d(%%rbp)\n", src, slot + byte_off);
                            }
                        }
                    } else {
                        /* Load from stack (spilled) */
                        int sz = type_sizeof(pty);
                        if (sz < 0) sz = 0;
                        if (copy_to_addr)
                            load_reg(out, &ra, ins->dst, "%r11");
                        else
                            fprintf(out, "\tleaq %d(%%rbp), %%r11\n", slot);
                        fprintf(out, "\tleaq %d(%%rbp), %%r10\n", st.stack_offset);
                        emit_copy_bytes(out, "%r11", "%r10", sz);
                    }
                }
            } else {
                if (is_fp_type(ins->dst.type) || is_fp_type(ins->src1.type)) {
                    load_xmm(out, &ra, ins->src1, "%xmm0");
                    store_xmm(out, &ra, ins->dst, "%xmm0");
                } else {
                    load_reg(out, &ra, ins->src1, "%rax");
                    store_reg(out, &ra, ins->dst, "%rax");
                }
            }
            break;

        /* dst = &src1  (address of a temp's stack slot, or a global label) */
        case IR_ADDR:
            if (ins->src1.kind == IRV_TEMP) {
                fprintf(out, "\tleaq %d(%%rbp), %%rax\n", ra.slot[ins->src1.temp_id]);
            } else if (ins->src1.kind == IRV_GLOBAL) {
                emit_global_addr(out, ins->src1.global, "%rax");
            } else {
                load_reg(out, &ra, ins->src1, "%rax");
            }
            store_reg(out, &ra, ins->dst, "%rax");
            break;

        /* Binary arithmetic / bitwise */
        case IR_ADD: case IR_SUB: case IR_MUL:
        case IR_AND: case IR_OR:  case IR_XOR:
        case IR_SHL: case IR_SHR: {
            if (is_fp_type(ins->dst.type)) {
                int is_double = is_double_type(ins->dst.type);
                load_xmm(out, &ra, ins->src1, "%xmm0");
                load_xmm(out, &ra, ins->src2, "%xmm1");
                switch (ins->op) {
                case IR_ADD: fprintf(out, is_double ? "\taddsd %%xmm1, %%xmm0\n" : "\taddss %%xmm1, %%xmm0\n"); break;
                case IR_SUB: fprintf(out, is_double ? "\tsubsd %%xmm1, %%xmm0\n" : "\tsubss %%xmm1, %%xmm0\n"); break;
                case IR_MUL: fprintf(out, is_double ? "\tmulsd %%xmm1, %%xmm0\n" : "\tmulss %%xmm1, %%xmm0\n"); break;
                default: break;
                }
                store_xmm(out, &ra, ins->dst, "%xmm0");
                break;
            }
            load_reg(out, &ra, ins->src1, "%rax");
            load_reg(out, &ra, ins->src2, "%rcx");
            switch (ins->op) {
            case IR_ADD: fprintf(out, "\taddq %%rcx, %%rax\n");  break;
            case IR_SUB: fprintf(out, "\tsubq %%rcx, %%rax\n");  break;
            case IR_MUL: fprintf(out, "\timulq %%rcx, %%rax\n"); break;
            case IR_AND: fprintf(out, "\tandq %%rcx, %%rax\n");  break;
            case IR_OR:  fprintf(out, "\torq %%rcx, %%rax\n");   break;
            case IR_XOR: fprintf(out, "\txorq %%rcx, %%rax\n");  break;
            case IR_SHL: fprintf(out, "\tsalq %%cl, %%rax\n");   break;
            case IR_SHR: fprintf(out, "\tsarq %%cl, %%rax\n");   break;
            default: break;
            }
            store_reg(out, &ra, ins->dst, "%rax");
            break;
        }

        case IR_DIV:
            if (is_fp_type(ins->dst.type)) {
                int is_double = is_double_type(ins->dst.type);
                load_xmm(out, &ra, ins->src1, "%xmm0");
                load_xmm(out, &ra, ins->src2, "%xmm1");
                fprintf(out, is_double ? "\tdivsd %%xmm1, %%xmm0\n" : "\tdivss %%xmm1, %%xmm0\n");
                store_xmm(out, &ra, ins->dst, "%xmm0");
                break;
            }
            load_reg(out, &ra, ins->src1, "%rax");
            fprintf(out, "\tcqto\n");
            load_reg(out, &ra, ins->src2, "%rcx");
            fprintf(out, "\tidivq %%rcx\n");
            store_reg(out, &ra, ins->dst, "%rax");  /* quotient */
            break;

        case IR_MOD:
            load_reg(out, &ra, ins->src1, "%rax");
            fprintf(out, "\tcqto\n");
            load_reg(out, &ra, ins->src2, "%rcx");
            fprintf(out, "\tidivq %%rcx\n");
            store_reg(out, &ra, ins->dst, "%rdx");  /* remainder */
            break;

        case IR_NEG:
            load_reg(out, &ra, ins->src1, "%rax");
            fprintf(out, "\tnegq %%rax\n");
            store_reg(out, &ra, ins->dst, "%rax");
            break;

        case IR_NOT:
            load_reg(out, &ra, ins->src1, "%rax");
            fprintf(out, "\tnotq %%rax\n");
            store_reg(out, &ra, ins->dst, "%rax");
            break;

        /* Comparisons: result = 0 or 1 */
        case IR_EQ: case IR_NE:
        case IR_LT: case IR_LE:
        case IR_GT: case IR_GE: {
            if (is_fp_type(ins->src1.type) || is_fp_type(ins->src2.type)) {
                load_xmm(out, &ra, ins->src1, "%xmm0");
                load_xmm(out, &ra, ins->src2, "%xmm1");
                fprintf(out, "\tucomisd %%xmm1, %%xmm0\n");
                switch (ins->op) {
                case IR_EQ: fprintf(out, "\tsete %%al\n");  break;
                case IR_NE: fprintf(out, "\tsetne %%al\n"); break;
                case IR_LT: fprintf(out, "\tsetb %%al\n");  break;
                case IR_LE: fprintf(out, "\tsetbe %%al\n"); break;
                case IR_GT: fprintf(out, "\tseta %%al\n");  break;
                case IR_GE: fprintf(out, "\tsetae %%al\n"); break;
                default: break;
                }
                fprintf(out, "\tmovzbq %%al, %%rax\n");
                store_reg(out, &ra, ins->dst, "%rax");
                break;
            }
            load_reg(out, &ra, ins->src1, "%rax");
            load_reg(out, &ra, ins->src2, "%rcx");
            fprintf(out, "\tcmpq %%rcx, %%rax\n");
            switch (ins->op) {
            case IR_EQ: fprintf(out, "\tsete %%al\n");  break;
            case IR_NE: fprintf(out, "\tsetne %%al\n"); break;
            case IR_LT: fprintf(out, "\tsetl %%al\n");  break;
            case IR_LE: fprintf(out, "\tsetle %%al\n"); break;
            case IR_GT: fprintf(out, "\tsetg %%al\n");  break;
            case IR_GE: fprintf(out, "\tsetge %%al\n"); break;
            default: break;
            }
            fprintf(out, "\tmovzbq %%al, %%rax\n");
            store_reg(out, &ra, ins->dst, "%rax");
            break;
        }

        /* Type conversions: simplify to sign-extend or truncate */
        case IR_SEXT: {
            load_reg(out, &ra, ins->src1, "%rax");
            /* Determine source size from src1 type and sign-extend */
            Type *from = ins->src1.type;
            int fsz = (from && type_sizeof(from) > 0) ? type_sizeof(from) : 8;
            switch (fsz) {
            case 1: fprintf(out, "\tmovsbq %%al, %%rax\n");  break;
            case 2: fprintf(out, "\tmovswq %%ax, %%rax\n");  break;
            case 4: fprintf(out, "\tmovslq %%eax, %%rax\n"); break;
            default: break;
            }
            store_reg(out, &ra, ins->dst, "%rax");
            break;
        }
        case IR_ZEXT: {
            load_reg(out, &ra, ins->src1, "%rax");
            Type *from = ins->src1.type;
            int fsz = (from && type_sizeof(from) > 0) ? type_sizeof(from) : 8;
            switch (fsz) {
            case 1: fprintf(out, "\tmovzbq %%al, %%rax\n");  break;
            case 2: fprintf(out, "\tmovzwq %%ax, %%rax\n");  break;
            case 4: /* movl to %eax zero-extends automatically */ break;
            default: break;
            }
            store_reg(out, &ra, ins->dst, "%rax");
            break;
        }
        case IR_TRUNC:
            load_reg(out, &ra, ins->src1, "%rax");
            if (ins->dst.type) {
                int tsz = type_sizeof(ins->dst.type);
                int is_signed = type_is_signed(ins->dst.type);
                switch (tsz) {
                case 1:
                    fprintf(out, is_signed ? "\tmovsbq %%al, %%rax\n"
                                           : "\tmovzbq %%al, %%rax\n");
                    break;
                case 2:
                    fprintf(out, is_signed ? "\tmovswq %%ax, %%rax\n"
                                           : "\tmovzwq %%ax, %%rax\n");
                    break;
                case 4:
                    fprintf(out, is_signed ? "\tmovslq %%eax, %%rax\n"
                                           : "\tmovl %%eax, %%eax\n");
                    break;
                default:
                    break;
                }
            }
            store_reg(out, &ra, ins->dst, "%rax");
            break;

        /* Float conversions (basic) */
        case IR_ITOF:
            load_reg(out, &ra, ins->src1, "%rax");
            fprintf(out, is_double_type(ins->dst.type) ? "\tcvtsi2sdq %%rax, %%xmm0\n"
                                                       : "\tcvtsi2ssq %%rax, %%xmm0\n");
            store_xmm(out, &ra, ins->dst, "%xmm0");
            break;
        case IR_FTOI:
            load_xmm(out, &ra, ins->src1, "%xmm0");
            fprintf(out, is_double_type(ins->src1.type) ? "\tcvttsd2siq %%xmm0, %%rax\n"
                                                        : "\tcvttss2siq %%xmm0, %%rax\n");
            store_reg(out, &ra, ins->dst, "%rax");
            break;

        /* Control flow */
        case IR_JMP:
            fprintf(out, "\tjmp .L%s_%d\n", f->name, ins->label_id);
            break;

        case IR_JZ:
            load_reg(out, &ra, ins->src1, "%rax");
            fprintf(out, "\ttestq %%rax, %%rax\n");
            fprintf(out, "\tje .L%s_%d\n", f->name, ins->label_id);
            break;

        case IR_JNZ:
            load_reg(out, &ra, ins->src1, "%rax");
            fprintf(out, "\ttestq %%rax, %%rax\n");
            fprintf(out, "\tjne .L%s_%d\n", f->name, ins->label_id);
            break;

        case IR_CALL: {
            static const char *param_regs[6] = { "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9" };
            static const char *xmm_regs[8] = { "%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7" };
            int nargs = ins->call_nargs;
            int int_used = ins->call_sret ? 1 : 0;
            int sse_used = 0;
            int stack_args_bytes = 0;

            /* Classify arguments and calculate stack space */
            for (int a = 0; a < nargs; a++) {
                Type *aty = ins->call_args[a].type;
                ABIClassification cls = abi_classify(aty);
                if (abi_in_memory(&cls)) {
                    int sz = type_sizeof(aty);
                    if (sz < 0) sz = 0;
                    stack_args_bytes += (sz + 7) & ~7;
                } else {
                    int n_int = 0, n_sse = 0;
                    for (int j = 0; j < cls.n_eightbytes; j++) {
                        if (cls.classes[j] == ABI_CLASS_INTEGER) n_int++;
                        else if (cls.classes[j] == ABI_CLASS_SSE) n_sse++;
                    }
                    if (int_used + n_int <= 6 && sse_used + n_sse <= 8) {
                        int_used += n_int;
                        sse_used += n_sse;
                    } else {
                        int sz = type_sizeof(aty);
                        if (sz < 0) sz = 0;
                        stack_args_bytes += (sz + 7) & ~7;
                    }
                }
            }

            /* Check 16-byte stack alignment: rbp is 16-aligned, we sub'd ra.frame_size. */
            int align_pad = 0;
            if ((ra.frame_size + stack_args_bytes) % 16 != 0) {
                align_pad = 16 - ((ra.frame_size + stack_args_bytes) % 16);
                fprintf(out, "\tsubq $%d, %%rsp\n", align_pad);
            }

            /* Second pass: Push arguments backwards! */
            int_used = ins->call_sret ? 1 : 0;
            sse_used = 0;
            for (int a = nargs - 1; a >= 0; a--) {
                Type *aty = ins->call_args[a].type;
                ABIClassification cls = abi_classify(aty);
                int goes_to_stack = 0;

                if (abi_in_memory(&cls)) {
                    goes_to_stack = 1;
                } else {
                    int n_int = 0, n_sse = 0;
                    for (int j = 0; j < cls.n_eightbytes; j++) {
                        if (cls.classes[j] == ABI_CLASS_INTEGER) n_int++;
                        else if (cls.classes[j] == ABI_CLASS_SSE) n_sse++;
                    }
                    /* To know if THIS arg is spilled, we must know total registers BEFORE it.
                       So we re-simulate forwards */
                    int sim_int = ins->call_sret ? 1 : 0;
                    int sim_sse = 0;
                    for (int k = 0; k < a; k++) {
                        ABIClassification c = abi_classify(ins->call_args[k].type);
                        if (!abi_in_memory(&c)) {
                            int ki=0, ks=0;
                            for (int m = 0; m < c.n_eightbytes; m++) {
                                if (c.classes[m] == ABI_CLASS_INTEGER) ki++;
                                else if (c.classes[m] == ABI_CLASS_SSE) ks++;
                            }
                            if (sim_int + ki <= 6 && sim_sse + ks <= 8) {
                                sim_int += ki; sim_sse += ks;
                            }
                        }
                    }
                    if (sim_int + n_int > 6 || sim_sse + n_sse > 8) {
                        goes_to_stack = 1;
                    }
                }

                if (goes_to_stack) {
                    int sz = type_sizeof(aty);
                    if (sz < 0) sz = 0;
                    int aligned_sz = (sz + 7) & ~7;
                    fprintf(out, "\tsubq $%d, %%rsp\n", aligned_sz);
                    if (is_aggregate_type(aty)) {
                        load_reg(out, &ra, ins->call_args[a], "%r11");
                        emit_copy_bytes(out, "%rsp", "%r11", sz);
                    } else {
                        load_reg(out, &ra, ins->call_args[a], "%rax");
                        fprintf(out, "\tmovq %%rax, (%%rsp)\n");
                    }
                }
            }

            /* Third pass: Load register arguments */
            int_used = ins->call_sret ? 1 : 0;
            sse_used = 0;
            for (int a = 0; a < nargs; a++) {
                Type *aty = ins->call_args[a].type;
                ABIClassification cls = abi_classify(aty);
                if (abi_in_memory(&cls)) continue;

                int n_int = 0, n_sse = 0;
                for (int j = 0; j < cls.n_eightbytes; j++) {
                    if (cls.classes[j] == ABI_CLASS_INTEGER) n_int++;
                    else if (cls.classes[j] == ABI_CLASS_SSE) n_sse++;
                }
                if (int_used + n_int > 6 || sse_used + n_sse > 8) continue; /* spilled */

                if (is_aggregate_type(aty)) {
                    load_reg(out, &ra, ins->call_args[a], "%r11");
                    for (int j = 0; j < cls.n_eightbytes; j++) {
                        int byte_off = j * 8;
                        int remaining = type_sizeof(aty) - byte_off;
                        if (cls.classes[j] == ABI_CLASS_INTEGER) {
                            emit_load_eightbyte_from_mem(out, "%r11", byte_off, remaining, param_regs[int_used++]);
                        } else if (cls.classes[j] == ABI_CLASS_SSE) {
                            fprintf(out, remaining >= 8 ? "\tmovsd %d(%%r11), %s\n"
                                                       : "\tmovss %d(%%r11), %s\n",
                                    byte_off, xmm_regs[sse_used++]);
                        }
                    }
                } else if (cls.n_eightbytes == 1 && cls.classes[0] == ABI_CLASS_SSE) {
                    load_xmm(out, &ra, ins->call_args[a], xmm_regs[sse_used++]);
                } else if (cls.n_eightbytes == 1 && cls.classes[0] == ABI_CLASS_INTEGER) {
                    load_reg(out, &ra, ins->call_args[a], param_regs[int_used++]);
                } else {
                    load_reg(out, &ra, ins->call_args[a], param_regs[int_used++]);
                }
            }

            /* Sret pointer */
            if (ins->call_sret) {
                if (ins->dst.kind == IRV_TEMP) {
                    fprintf(out, "\tmovq %d(%%rbp), %%rdi\n", ra.slot[ins->dst.temp_id]);
                }
            }

            fprintf(out, "\tmovb $%d, %%al\n", sse_used > 8 ? 8 : sse_used);
            if (ins->src1.kind == IRV_GLOBAL) {
                fprintf(out, "\tcallq "); fprint_sym(out, ins->src1.global); fprintf(out, "\n");
            } else {
                load_reg(out, &ra, ins->src1, "%r10");
                fprintf(out, "\tcallq *%%r10\n");
            }

            if (stack_args_bytes + align_pad > 0) {
                fprintf(out, "\taddq $%d, %%rsp\n", stack_args_bytes + align_pad);
            }

            if (!ins->call_sret && ins->dst.kind == IRV_TEMP) {
                Type *rty = ins->call_ret_ty ? ins->call_ret_ty : ins->dst.type;
                ABIClassification rcls = abi_classify(rty);

                if (!abi_in_memory(&rcls)) {
                    if (is_aggregate_type(rty)) {
                        int slot = ra.slot[ins->dst.temp_id];
                        fprintf(out, "\tmovq %d(%%rbp), %%r10\n", slot);
                        int ri = 0, rs = 0;
                        const char *irets[2] = {"%rax", "%rdx"};
                        const char *srets[2] = {"%xmm0", "%xmm1"};
                        for (int j = 0; j < rcls.n_eightbytes; j++) {
                            int byte_off = j * 8;
                            int remaining = type_sizeof(rty) - byte_off;
                            if (rcls.classes[j] == ABI_CLASS_INTEGER) {
                                const char *src = irets[ri++];
                                if (remaining >= 8) fprintf(out, "\tmovq %s, %d(%%r10)\n", src, byte_off);
                                else if (remaining >= 4) fprintf(out, "\tmovl %s, %d(%%r10)\n", to_32bit(src), byte_off);
                                else if (remaining >= 2) fprintf(out, "\tmovw %s, %d(%%r10)\n", to_16bit(src), byte_off);
                                else fprintf(out, "\tmovb %s, %d(%%r10)\n", to_8bit(src), byte_off);
                            } else if (rcls.classes[j] == ABI_CLASS_SSE) {
                                fprintf(out, "\tmovsd %s, %d(%%r10)\n", srets[rs++], byte_off);
                            }
                        }
                    } else {
                        if (rcls.n_eightbytes == 1 && rcls.classes[0] == ABI_CLASS_SSE) {
                            store_xmm(out, &ra, ins->dst, "%xmm0");
                        } else if (rcls.n_eightbytes == 1 && rcls.classes[0] == ABI_CLASS_INTEGER) {
                            normalize_rax_to_type(out, rty);
                            store_reg(out, &ra, ins->dst, "%rax");
                        }
                    }
                }
            }
            break;
        }

        case IR_RET:
            if (ins->src1.kind != IRV_NONE) {
                Type *rty = (f->func_type && f->func_type->kind == TY_FUNC) ? f->func_type->func.ret : type_int();
                ABIClassification rcls = abi_classify(rty);

                if (abi_in_memory(&rcls)) {
                    /* Memory return logic already handled by sret pointer */
                    IRValue sret = irv_temp(f->sret_temp, NULL);
                    load_reg(out, &ra, sret, "%rdi");
                    if (ins->src1.kind == IRV_TEMP) {
                        fprintf(out, "\tmovq %d(%%rbp), %%rsi\n", ra.slot[ins->src1.temp_id]);
                    } else {
                        load_reg(out, &ra, ins->src1, "%rsi");
                    }
                    fprintf(out, "\tmovq $%d, %%rdx\n", type_sizeof(rty));
                    fprintf(out, "\tcallq "); fprint_sym(out, "memcpy"); fprintf(out, "\n");
                    load_reg(out, &ra, sret, "%rax");
                } else if (is_aggregate_type(rty)) {
                    if (ins->src1.kind == IRV_TEMP) {
                        load_reg(out, &ra, ins->src1, "%r10");
                        int ri = 0, rs = 0;
                        const char *irets[2] = {"%rax", "%rdx"};
                        const char *srets[2] = {"%xmm0", "%xmm1"};
                        for (int j = 0; j < rcls.n_eightbytes; j++) {
                            int byte_off = j * 8;
                            int remaining = type_sizeof(rty) - byte_off;
                            if (rcls.classes[j] == ABI_CLASS_INTEGER) {
                                emit_load_eightbyte_from_mem(out, "%r10", byte_off, remaining, irets[ri++]);
                            } else if (rcls.classes[j] == ABI_CLASS_SSE) {
                                fprintf(out, remaining >= 8 ? "\tmovsd %d(%%r10), %s\n"
                                                           : "\tmovss %d(%%r10), %s\n",
                                        byte_off, srets[rs++]);
                            }
                        }
                    }
                } else if (rcls.n_eightbytes == 1 && rcls.classes[0] == ABI_CLASS_SSE) {
                    load_xmm(out, &ra, ins->src1, "%xmm0");
                } else if (rcls.n_eightbytes == 1 && rcls.classes[0] == ABI_CLASS_INTEGER) {
                    load_reg(out, &ra, ins->src1, "%rax");
                    if (f->func_type && f->func_type->kind == TY_FUNC)
                        normalize_rax_to_type(out, f->func_type->func.ret);
                }
            }
            fprintf(out, "\tleave\n\tretq\n");
            break;

        /* Struct bulk copy */
        case IR_MEMCPY:
            load_reg(out, &ra, ins->dst,  "%rdi");  /* dest */
            load_reg(out, &ra, ins->src1, "%rsi");  /* src  */
            load_reg(out, &ra, ins->src2, "%rdx");  /* size */
            fprintf(out, "\tcallq ");
            fprint_sym(out, "memcpy");
            fprintf(out, "\n");
            break;

        case IR_GLOBAL_DEF:
            /* handled at module level */
            break;

        default:
            break;
        }
    }

    /* --- Epilogue (fallthrough / void return) --- */
    fprintf(out, "\tleave\n\tretq\n");

#ifndef __APPLE__
    fprintf(out, "\t.size ");
    fputs(f->name, out);
    fprintf(out, ", .-");
    fputs(f->name, out);
    fprintf(out, "\n");
#endif
    fprintf(out, "\n");

    arena_destroy(tmp);
}

/* ── Emit a C string as an assembler .asciz (with escaping) ──────── */
static void emit_asciz(FILE *out, const char *s) {
    fputc('"', out);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '\n': fputs("\\n",  out); break;
        case '\r': fputs("\\r",  out); break;
        case '\t': fputs("\\t",  out); break;
        case '\\': fputs("\\\\", out); break;
        case '"':  fputs("\\\"", out); break;
        default:
            if (c < 32 || c > 126) fprintf(out, "\\%03o", c);
            else fputc(c, out);
            break;
        }
    }
    fputc('"', out);
}

/* ── Module-level entry point ─────────────────────────────────────── */
void codegen(const IRModule *m, FILE *out) {
    /* Text section: all function bodies */
    fprintf(out, "\t.text\n\n");
    for (int i = 0; i < m->nfuncs; i++) {
        const IRFunc *f = m->funcs[i];
        if (f->ninstr == 0) continue;
        codegen_func(f, out);
    }

    /* Read-only data section: string literals */
    if (m->str_consts) {
#ifdef __APPLE__
        fprintf(out, "\t.section __TEXT,__cstring,cstring_literals\n");
#else
        fprintf(out, "\t.section .rodata\n");
#endif
        for (IRStrConst *s = m->str_consts; s; s = s->next) {
            fprintf(out, "%s:\n\t.asciz ", s->label);
            emit_asciz(out, s->data);
            fprintf(out, "\n");
        }
    }

    /* Data section: global variable definitions */
    if (m->global_vars) {
        fprintf(out, "\n\t.data\n");
        for (IRGlobalVar *gv = m->global_vars; gv; gv = gv->next) {
            int sz = (gv->type && type_sizeof(gv->type) > 0) ? type_sizeof(gv->type) : 8;
            if (!gv->is_static) {
#ifdef __APPLE__
                fprintf(out, "\t.globl _");
                fputs(gv->name, out);
                fprintf(out, "\n");
                fprintf(out, "_");
                fputs(gv->name, out);
                fprintf(out, ":\n");
#else
                fprintf(out, "\t.globl ");
                fputs(gv->name, out);
                fprintf(out, "\n\t.type ");
                fputs(gv->name, out);
                fprintf(out, ", @object\n");
                fputs(gv->name, out);
                fprintf(out, ":\n");
#endif
            } else {
#ifdef __APPLE__
                fprintf(out, "_");
                fputs(gv->name, out);
                fprintf(out, ":\n");
#else
                fputs(gv->name, out);
                fprintf(out, ":\n");
#endif
            }
            if (gv->init_data && gv->init_size > 0) {
                for (int i = 0; i < gv->init_size; i++) {
                    if (i % 12 == 0) fprintf(out, "\t.byte ");
                    else fprintf(out, ", ");
                    fprintf(out, "%u", (unsigned)gv->init_data[i]);
                    if (i % 12 == 11 || i == gv->init_size - 1)
                        fprintf(out, "\n");
                }
            } else if (gv->has_init && gv->init_val != 0) {
                switch (sz) {
                case 1: fprintf(out, "\t.byte %lld\n",  gv->init_val); break;
                case 2: fprintf(out, "\t.short %lld\n", gv->init_val); break;
                case 4: fprintf(out, "\t.long %lld\n",  gv->init_val); break;
                default:fprintf(out, "\t.quad %lld\n",  gv->init_val); break;
                }
            } else {
                fprintf(out, "\t.zero %d\n", sz);
            }
        }
    }
}
