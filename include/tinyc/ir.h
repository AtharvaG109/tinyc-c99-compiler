/*
 * ir.h — Three-address code (TAC) intermediate representation
 *
 * Each instruction has at most one operator, one result, and two operands.
 * Virtual registers are unlimited; the register allocator maps them to
 * physical registers or stack slots.
 */
#ifndef TINYC_IR_H
#define TINYC_IR_H

#include "tinyc/type.h"
#include "tinyc/arena.h"
#include <stdio.h>

/* ── IR value (operand) ──────────────────────────────────────────── */
typedef enum {
    IRV_TEMP,        /* virtual register */
    IRV_CONST_INT,   /* immediate integer */
    IRV_CONST_FLOAT, /* immediate float (stored as double) */
    IRV_GLOBAL,      /* global variable or function label */
    IRV_NONE,        /* absent operand */
} IRValueKind;

typedef struct {
    IRValueKind kind;
    Type       *type;  /* may be NULL for IRV_NONE */
    union {
        int         temp_id;
        long long   const_int;
        double      const_float;
        const char *global;   /* interned name */
    };
} IRValue;

/* Convenience constructors */
static inline IRValue irv_none(void) { IRValue v; v.kind=IRV_NONE; v.type=NULL; v.temp_id=0; return v; }
static inline IRValue irv_temp(int id, Type *t) { IRValue v; v.kind=IRV_TEMP; v.type=t; v.temp_id=id; return v; }
static inline IRValue irv_int(long long c, Type *t) { IRValue v; v.kind=IRV_CONST_INT; v.type=t; v.const_int=c; return v; }
static inline IRValue irv_float(double c, Type *t) { IRValue v; v.kind=IRV_CONST_FLOAT; v.type=t; v.const_float=c; return v; }
static inline IRValue irv_global(const char *name, Type *t) { IRValue v; v.kind=IRV_GLOBAL; v.type=t; v.global=name; return v; }

/* ── IR opcode ───────────────────────────────────────────────────── */
typedef enum {
    IR_NOP,

    /* Data movement */
    IR_COPY,      /* dst = src1 */
    IR_LOAD,      /* dst = *src1 (load from address) */
    IR_STORE,     /* *dst = src1 (store to address) */
    IR_ADDR,      /* dst = address of local/global */
    IR_LOAD_IMM,  /* dst = immediate (alias for COPY with const) */

    /* Arithmetic / bitwise */
    IR_ADD,  IR_SUB,  IR_MUL,  IR_DIV,  IR_MOD,
    IR_AND,  IR_OR,   IR_XOR,
    IR_SHL,  IR_SHR,
    IR_NEG,  IR_NOT,  /* unary */

    /* Comparison — result is 0 or 1 in dst */
    IR_EQ, IR_NE, IR_LT, IR_LE, IR_GT, IR_GE,

    /* Type conversion */
    IR_SEXT,   /* sign-extend src1 to dst width */
    IR_ZEXT,   /* zero-extend */
    IR_TRUNC,  /* truncate */
    IR_ITOF,   /* int → float */
    IR_FTOI,   /* float → int */

    /* Control flow */
    IR_LABEL,  /* label definition */
    IR_JMP,    /* unconditional jump to label_id */
    IR_JZ,     /* jump if src1 == 0 */
    IR_JNZ,    /* jump if src1 != 0 */
    IR_CALL,   /* dst = call src1(args...) */
    IR_RET,    /* return src1 (or void) */

    /* Stack allocation */
    IR_ALLOCA, /* dst = pointer to n bytes on stack */

    /* Bulk copy (struct assignment) */
    IR_MEMCPY, /* memcpy(dst_addr, src1_addr, src2=size_bytes) */

    /* Global data */
    IR_GLOBAL_DEF, /* define a global variable */
} IROp;

/* ── IR instruction ──────────────────────────────────────────────── */
typedef struct {
    IROp    op;
    IRValue dst;
    IRValue src1;
    IRValue src2;
    /* Extra fields */
    int     label_id;       /* for IR_LABEL, IR_JMP, IR_JZ, IR_JNZ */
    int     alloca_size;    /* for IR_ALLOCA */
    int     alloca_align;
    /* For IR_CALL: arguments stored as a separate flat array */
    IRValue *call_args;
    int      call_nargs;
    int      call_sret;     /* call uses hidden struct-return pointer in dst */
    Type    *call_ret_ty;   /* actual return type of the function */
    int      line;          /* source line for debugging */
} IRInstr;

/* ── IR function ─────────────────────────────────────────────────── */
typedef struct {
    const char *name;
    Type       *func_type;   /* TY_FUNC */
    IRInstr    *instrs;
    int         ninstr;
    int         cap;
    int         next_temp;   /* next virtual register id */
    int         next_label;  /* next label id */
    int         is_static;   /* 0 = global linkage */
    int         sret_temp;   /* hidden aggregate-return pointer temp, 0 if none */
    Arena      *arena;       /* for growing the instrs array */
    /* Stack frame tracking (filled by regalloc) */
    int         frame_size;
} IRFunc;

/* ── String constant (for string literal storage in .rodata) ─────── */
typedef struct IRStrConst {
    const char       *label;  /* assembler label, e.g. ".Lstr0" */
    const char       *data;   /* raw bytes (null-terminated) */
    struct IRStrConst *next;
} IRStrConst;

/* ── Global variable definition (for .data / .bss section) ──────── */
typedef struct IRGlobalVar {
    const char         *name;
    Type               *type;
    long long           init_val;  /* integer initializer (0 if zero-init) */
    int                 has_init;  /* non-zero if explicit initializer given */
    unsigned char      *init_data; /* full object bytes for aggregate init */
    int                 init_size;
    int                 is_static; /* 0 = global linkage */
    struct IRGlobalVar  *next;
} IRGlobalVar;

/* ── IR module ───────────────────────────────────────────────────── */
typedef struct {
    IRFunc     **funcs;
    int          nfuncs;
    int          cap;
    Arena       *arena;
    IRStrConst  *str_consts;   /* linked list of string literals */
    int          next_str_id;  /* counter for unique labels */
    IRGlobalVar *global_vars;  /* linked list of global variable definitions */
} IRModule;

/* ── API ─────────────────────────────────────────────────────────── */

IRModule *ir_module_new(Arena *arena);
IRFunc   *ir_func_new(IRModule *m, const char *name, Type *func_type, int is_static);

/* Append an instruction and return a pointer to it (stable in arena). */
IRInstr  *ir_emit(IRFunc *f, IROp op, IRValue dst, IRValue src1, IRValue src2);

/* Allocate a new virtual register id. */
int       ir_new_temp(IRFunc *f);

/* Allocate a new label id. */
int       ir_new_label(IRFunc *f);

/* Emit a label definition instruction. */
void      ir_emit_label(IRFunc *f, int label_id);

/* Emit a jump. */
void      ir_emit_jmp(IRFunc *f, int label_id);
void      ir_emit_jz(IRFunc *f, IRValue cond, int label_id);
void      ir_emit_jnz(IRFunc *f, IRValue cond, int label_id);

/* Register a string literal, returning its assembler label (interned). */
const char *ir_add_string_const(IRModule *m, const char *data);

/* Register a global variable definition (skipped for extern declarations). */
IRGlobalVar *ir_add_global_var(IRModule *m, const char *name, Type *type,
                                long long init_val, int has_init, int is_static);

/* Pretty-print IR to a file. */
void      ir_print(const IRModule *m, FILE *out);
void      ir_print_func(const IRFunc *f, FILE *out);

#endif /* TINYC_IR_H */
