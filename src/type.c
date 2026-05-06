/*
 * type.c — C99 type system implementation
 */
#include "tinyc/type.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ── Static scalar singletons ─────────────────────────────────────── */

static Type scalar_void    = { TY_VOID,    QUAL_NONE, {{NULL}}, NULL };
static Type scalar_bool    = { TY_BOOL,    QUAL_NONE, {{NULL}}, NULL };
static Type scalar_char    = { TY_CHAR,    QUAL_NONE, {{NULL}}, NULL };
static Type scalar_schar   = { TY_SCHAR,   QUAL_NONE, {{NULL}}, NULL };
static Type scalar_uchar   = { TY_UCHAR,   QUAL_NONE, {{NULL}}, NULL };
static Type scalar_short   = { TY_SHORT,   QUAL_NONE, {{NULL}}, NULL };
static Type scalar_ushort  = { TY_USHORT,  QUAL_NONE, {{NULL}}, NULL };
static Type scalar_int     = { TY_INT,     QUAL_NONE, {{NULL}}, NULL };
static Type scalar_uint    = { TY_UINT,    QUAL_NONE, {{NULL}}, NULL };
static Type scalar_long    = { TY_LONG,    QUAL_NONE, {{NULL}}, NULL };
static Type scalar_ulong   = { TY_ULONG,   QUAL_NONE, {{NULL}}, NULL };
static Type scalar_llong   = { TY_LLONG,   QUAL_NONE, {{NULL}}, NULL };
static Type scalar_ullong  = { TY_ULLONG,  QUAL_NONE, {{NULL}}, NULL };
static Type scalar_float   = { TY_FLOAT,   QUAL_NONE, {{NULL}}, NULL };
static Type scalar_double  = { TY_DOUBLE,  QUAL_NONE, {{NULL}}, NULL };
static Type scalar_ldouble = { TY_LDOUBLE, QUAL_NONE, {{NULL}}, NULL };

void type_table_init(TypeTable *tt, Arena *arena) {
    memset(tt, 0, sizeof(*tt));
    tt->arena = arena;
}

Type *type_void(void)    { return &scalar_void; }
Type *type_bool(void)    { return &scalar_bool; }
Type *type_char(void)    { return &scalar_char; }
Type *type_schar(void)   { return &scalar_schar; }
Type *type_uchar(void)   { return &scalar_uchar; }
Type *type_short(void)   { return &scalar_short; }
Type *type_ushort(void)  { return &scalar_ushort; }
Type *type_int(void)     { return &scalar_int; }
Type *type_uint(void)    { return &scalar_uint; }
Type *type_long(void)    { return &scalar_long; }
Type *type_ulong(void)   { return &scalar_ulong; }
Type *type_llong(void)   { return &scalar_llong; }
Type *type_ullong(void)  { return &scalar_ullong; }
Type *type_float(void)   { return &scalar_float; }
Type *type_double(void)  { return &scalar_double; }
Type *type_ldouble(void) { return &scalar_ldouble; }

/* ── Hash helpers ─────────────────────────────────────────────────── */

static unsigned int ptr_hash(uintptr_t a, uintptr_t b) {
    uintptr_t h = a ^ (a >> 16) ^ b ^ (b >> 8);
    return (unsigned int)(h & (TYPE_TABLE_BUCKETS - 1));
}

/* ── Derived type interning ───────────────────────────────────────── */

Type *type_pointer(TypeTable *tt, Type *pointee, TypeQual qual) {
    unsigned int slot = ptr_hash((uintptr_t)pointee, (uintptr_t)qual);
    for (Type *t = tt->buckets[slot]; t; t = t->next_interned) {
        if (t->kind == TY_POINTER && t->ptr.pointee == pointee && t->qual == qual)
            return t;
    }
    Type *t = (Type *)arena_alloc(tt->arena, sizeof(Type), _Alignof(Type));
    memset(t, 0, sizeof(*t));
    t->kind = TY_POINTER;
    t->qual = qual;
    t->ptr.pointee = pointee;
    t->next_interned = tt->buckets[slot];
    tt->buckets[slot] = t;
    return t;
}

Type *type_array(TypeTable *tt, Type *elem, long long count) {
    unsigned int slot = ptr_hash((uintptr_t)elem, (uintptr_t)count);
    for (Type *t = tt->buckets[slot]; t; t = t->next_interned) {
        if (t->kind == TY_ARRAY && t->arr.elem == elem && t->arr.count == count)
            return t;
    }
    Type *t = (Type *)arena_alloc(tt->arena, sizeof(Type), _Alignof(Type));
    memset(t, 0, sizeof(*t));
    t->kind = TY_ARRAY;
    t->arr.elem = elem;
    t->arr.count = count;
    t->next_interned = tt->buckets[slot];
    tt->buckets[slot] = t;
    return t;
}

Type *type_array_unsized(TypeTable *tt, Type *elem) {
    unsigned int slot = ptr_hash((uintptr_t)elem, 0xFFFF);
    for (Type *t = tt->buckets[slot]; t; t = t->next_interned) {
        if (t->kind == TY_ARRAY_UNSIZED && t->arr.elem == elem)
            return t;
    }
    Type *t = (Type *)arena_alloc(tt->arena, sizeof(Type), _Alignof(Type));
    memset(t, 0, sizeof(*t));
    t->kind = TY_ARRAY_UNSIZED;
    t->arr.elem = elem;
    t->arr.count = -1;
    t->next_interned = tt->buckets[slot];
    tt->buckets[slot] = t;
    return t;
}

Type *type_func(TypeTable *tt, Type *ret, FuncParam *params,
                int n_params, int variadic) {
    /* Hash on ret pointer + n_params + variadic. We still do a linear search
     * for full structural equality because function types with the same
     * parameter count can differ in parameter types. */
    unsigned int slot = ptr_hash((uintptr_t)ret,
                                 (uintptr_t)((n_params << 1) | variadic));
    for (Type *t = tt->buckets[slot]; t; t = t->next_interned) {
        if (t->kind != TY_FUNC) continue;
        if (t->func.ret != ret) continue;
        if (t->func.n_params != n_params) continue;
        if (t->func.variadic != variadic) continue;
        /* Compare parameter types */
        FuncParam *tp = t->func.params;
        FuncParam *np = params;
        int match = 1;
        while (tp && np) {
            if (tp->type != np->type) { match = 0; break; }
            if (tp->name != np->name) { match = 0; break; }
            tp = tp->next; np = np->next;
        }
        if (match && !tp && !np) return t;
    }
    Type *t = (Type *)arena_alloc(tt->arena, sizeof(Type), _Alignof(Type));
    memset(t, 0, sizeof(*t));
    t->kind = TY_FUNC;
    t->func.ret = ret;
    t->func.params = params;
    t->func.n_params = n_params;
    t->func.variadic = variadic;
    t->next_interned = tt->buckets[slot];
    tt->buckets[slot] = t;
    return t;
}

/* Aggregate/enum types — not interned, identity via pointer */

Type *type_struct(TypeTable *tt, const char *tag) {
    Type *t = (Type *)arena_alloc(tt->arena, sizeof(Type), _Alignof(Type));
    memset(t, 0, sizeof(*t));
    t->kind = TY_STRUCT;
    t->agg.tag = tag;
    t->agg.size = -1;  /* incomplete until sealed */
    t->agg.align = 1;
    return t;
}

Type *type_union(TypeTable *tt, const char *tag) {
    Type *t = (Type *)arena_alloc(tt->arena, sizeof(Type), _Alignof(Type));
    memset(t, 0, sizeof(*t));
    t->kind = TY_UNION;
    t->agg.tag = tag;
    t->agg.size = -1;
    t->agg.align = 1;
    return t;
}

Type *type_enum(TypeTable *tt, const char *tag) {
    Type *t = (Type *)arena_alloc(tt->arena, sizeof(Type), _Alignof(Type));
    memset(t, 0, sizeof(*t));
    t->kind = TY_ENUM;
    t->enm.tag = tag;
    t->enm.complete = 0;
    return t;
}

Type *type_qualify(TypeTable *tt, Type *base, TypeQual added) {
    TypeQual combined = (TypeQual)(base->qual | added);
    if (combined == base->qual) return base;

    /* For scalars and aggregates: allocate a thin wrapper with same kind */
    Type *t = (Type *)arena_alloc(tt->arena, sizeof(Type), _Alignof(Type));
    *t = *base;
    t->qual = combined;
    t->next_interned = NULL;
    return t;
}

/* ── Layout ───────────────────────────────────────────────────────── */

void type_seal_struct(Type *ty) {
    int offset = 0, max_align = 1;
    int bit_unit_off = 0;
    int bit_next = 0;
    int bit_unit_size = 0;
    int bit_unit_align = 1;

    for (StructField *f = ty->agg.fields; f; f = f->next) {
        int fa = type_alignof(f->type);
        int fs = type_sizeof(f->type);
        if (fa > max_align) max_align = fa;
        if (f->bit_width >= 0) {
            int unit_bits = fs > 0 ? fs * 8 : 32;
            if (f->bit_width == 0) {
                if (bit_next > 0)
                    offset = bit_unit_off + bit_unit_size;
                offset = (offset + fa - 1) & ~(fa - 1);
                bit_next = 0;
                continue;
            }
            if (bit_next == 0 || bit_next + f->bit_width > unit_bits ||
                fa != bit_unit_align || fs != bit_unit_size) {
                offset = (offset + fa - 1) & ~(fa - 1);
                bit_unit_off = offset;
                bit_unit_size = fs;
                bit_unit_align = fa;
                bit_next = 0;
                offset += fs;
            }
            f->offset = bit_unit_off;
            f->bit_offset = bit_next;
            bit_next += f->bit_width;
            continue;
        }
        bit_next = 0;
        /* align offset up to field alignment */
        offset = (offset + fa - 1) & ~(fa - 1);
        f->offset = offset;
        offset += fs;
    }
    /* pad total size to alignment */
    ty->agg.size = (offset + max_align - 1) & ~(max_align - 1);
    ty->agg.align = max_align;
}

void type_seal_union(Type *ty) {
    int max_size = 0, max_align = 1;
    for (StructField *f = ty->agg.fields; f; f = f->next) {
        int fa = type_alignof(f->type);
        int fs = type_sizeof(f->type);
        f->offset = 0;  /* all members at offset 0 */
        if (f->bit_width >= 0)
            f->bit_offset = 0;
        if (fa > max_align) max_align = fa;
        if (fs > max_size) max_size = fs;
    }
    ty->agg.size = (max_size + max_align - 1) & ~(max_align - 1);
    ty->agg.align = max_align;
}

/* x86-64 LP64 sizes */
int type_sizeof(const Type *ty) {
    switch (ty->kind) {
    case TY_VOID:    return -1;
    case TY_BOOL:    return 1;
    case TY_CHAR:    return 1;
    case TY_SCHAR:   return 1;
    case TY_UCHAR:   return 1;
    case TY_SHORT:   return 2;
    case TY_USHORT:  return 2;
    case TY_INT:     return 4;
    case TY_UINT:    return 4;
    case TY_LONG:    return 8;
    case TY_ULONG:   return 8;
    case TY_LLONG:   return 8;
    case TY_ULLONG:  return 8;
    case TY_FLOAT:   return 4;
    case TY_DOUBLE:  return 8;
    case TY_LDOUBLE: return 16;
    case TY_POINTER: return 8;
    case TY_ARRAY:
        if (ty->arr.count <= 0) return -1;
        return type_sizeof(ty->arr.elem) * (int)ty->arr.count;
    case TY_ARRAY_UNSIZED: return -1;
    case TY_FUNC:    return -1;
    case TY_STRUCT:
    case TY_UNION:   return ty->agg.size;
    case TY_ENUM:    return 4; /* enum underlying type is int */
    }
    return -1;
}

int type_alignof(const Type *ty) {
    switch (ty->kind) {
    case TY_VOID:    return 1;
    case TY_BOOL:    return 1;
    case TY_CHAR:    return 1;
    case TY_SCHAR:   return 1;
    case TY_UCHAR:   return 1;
    case TY_SHORT:   return 2;
    case TY_USHORT:  return 2;
    case TY_INT:     return 4;
    case TY_UINT:    return 4;
    case TY_LONG:    return 8;
    case TY_ULONG:   return 8;
    case TY_LLONG:   return 8;
    case TY_ULLONG:  return 8;
    case TY_FLOAT:   return 4;
    case TY_DOUBLE:  return 8;
    case TY_LDOUBLE: return 16;
    case TY_POINTER: return 8;
    case TY_ARRAY:
    case TY_ARRAY_UNSIZED: return type_alignof(ty->arr.elem);
    case TY_FUNC:    return 1;
    case TY_STRUCT:
    case TY_UNION:   return ty->agg.align > 0 ? ty->agg.align : 1;
    case TY_ENUM:    return 4;
    }
    return 1;
}

/* ── Predicates ───────────────────────────────────────────────────── */

int type_is_integer(const Type *ty) {
    switch (ty->kind) {
    case TY_BOOL: case TY_CHAR: case TY_SCHAR: case TY_UCHAR:
    case TY_SHORT: case TY_USHORT: case TY_INT: case TY_UINT:
    case TY_LONG: case TY_ULONG: case TY_LLONG: case TY_ULLONG:
    case TY_ENUM:
        return 1;
    default: return 0;
    }
}

int type_is_arithmetic(const Type *ty) {
    return type_is_integer(ty) ||
           ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE ||
           ty->kind == TY_LDOUBLE;
}

int type_is_scalar(const Type *ty) {
    return type_is_arithmetic(ty) || ty->kind == TY_POINTER;
}

int type_is_pointer(const Type *ty) {
    return ty->kind == TY_POINTER;
}

int type_is_complete(const Type *ty) {
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION)
        return ty->agg.size >= 0;
    if (ty->kind == TY_ARRAY_UNSIZED) return 0;
    if (ty->kind == TY_VOID) return 0;
    if (ty->kind == TY_ENUM) return ty->enm.complete;
    return 1;
}

int type_is_signed(const Type *ty) {
    switch (ty->kind) {
    case TY_CHAR:   /* char signedness is implementation-defined; treat as signed */
    case TY_SCHAR: case TY_SHORT: case TY_INT:
    case TY_LONG:  case TY_LLONG:
        return 1;
    default: return 0;
    }
}

int type_is_unsigned_integer(const Type *ty) {
    switch (ty->kind) {
    case TY_BOOL: case TY_UCHAR: case TY_USHORT: case TY_UINT:
    case TY_ULONG: case TY_ULLONG:
        return 1;
    default: return 0;
    }
}

/* ── Compatibility (C99 §6.2.7) ──────────────────────────────────── */

int type_compatible(const Type *a, const Type *b) {
    /* Strip top-level qualifiers */
    if (a == b) return 1;
    if (a->kind != b->kind) {
        /* enum is compatible with its underlying int */
        if (a->kind == TY_ENUM && b->kind == TY_INT) return 1;
        if (a->kind == TY_INT  && b->kind == TY_ENUM) return 1;
        return 0;
    }
    switch (a->kind) {
    case TY_POINTER:
        return type_compatible(a->ptr.pointee, b->ptr.pointee);
    case TY_ARRAY:
        return a->arr.count == b->arr.count &&
               type_compatible(a->arr.elem, b->arr.elem);
    case TY_ARRAY_UNSIZED:
        return type_compatible(a->arr.elem, b->arr.elem);
    case TY_FUNC: {
        if (!type_compatible(a->func.ret, b->func.ret)) return 0;
        if (a->func.n_params != b->func.n_params) return 0;
        if (a->func.variadic != b->func.variadic) return 0;
        FuncParam *pa = a->func.params, *pb = b->func.params;
        while (pa && pb) {
            if (!type_compatible(pa->type, pb->type)) return 0;
            pa = pa->next; pb = pb->next;
        }
        return !pa && !pb;
    }
    case TY_STRUCT:
    case TY_UNION:
    case TY_ENUM:
        /* Struct/union/enum types are compatible only if identical (same decl) */
        return a == b;
    default:
        /* Scalar kinds: compatible iff same kind (already checked above) */
        return 1;
    }
}

/* ── Pretty printer ───────────────────────────────────────────────── */

static void type_print_inner(const Type *ty, char *buf, size_t bufsz, size_t *pos) {
#define APPEND(s) do { \
    size_t _n = strlen(s); \
    if (*pos + _n < bufsz) { memcpy(buf + *pos, s, _n); *pos += _n; } \
    else if (*pos < bufsz - 1) { *pos = bufsz - 1; } \
} while(0)
#define APPENDF(fmt, ...) do { \
    char _tmp[64]; \
    snprintf(_tmp, sizeof(_tmp), fmt, __VA_ARGS__); \
    APPEND(_tmp); \
} while(0)

    if (ty->qual & QUAL_CONST)    APPEND("const ");
    if (ty->qual & QUAL_VOLATILE) APPEND("volatile ");
    if (ty->qual & QUAL_RESTRICT) APPEND("restrict ");

    switch (ty->kind) {
    case TY_VOID:    APPEND("void"); break;
    case TY_BOOL:    APPEND("_Bool"); break;
    case TY_CHAR:    APPEND("char"); break;
    case TY_SCHAR:   APPEND("signed char"); break;
    case TY_UCHAR:   APPEND("unsigned char"); break;
    case TY_SHORT:   APPEND("short"); break;
    case TY_USHORT:  APPEND("unsigned short"); break;
    case TY_INT:     APPEND("int"); break;
    case TY_UINT:    APPEND("unsigned int"); break;
    case TY_LONG:    APPEND("long"); break;
    case TY_ULONG:   APPEND("unsigned long"); break;
    case TY_LLONG:   APPEND("long long"); break;
    case TY_ULLONG:  APPEND("unsigned long long"); break;
    case TY_FLOAT:   APPEND("float"); break;
    case TY_DOUBLE:  APPEND("double"); break;
    case TY_LDOUBLE: APPEND("long double"); break;
    case TY_POINTER:
        type_print_inner(ty->ptr.pointee, buf, bufsz, pos);
        APPEND("*");
        break;
    case TY_ARRAY:
        type_print_inner(ty->arr.elem, buf, bufsz, pos);
        APPENDF("[%lld]", ty->arr.count);
        break;
    case TY_ARRAY_UNSIZED:
        type_print_inner(ty->arr.elem, buf, bufsz, pos);
        APPEND("[]");
        break;
    case TY_FUNC:
        type_print_inner(ty->func.ret, buf, bufsz, pos);
        APPEND("(");
        {
            FuncParam *p = ty->func.params;
            int first = 1;
            while (p) {
                if (!first) APPEND(", ");
                first = 0;
                type_print_inner(p->type, buf, bufsz, pos);
                if (p->name) { APPEND(" "); APPEND(p->name); }
                p = p->next;
            }
            if (ty->func.variadic) {
                if (!first) APPEND(", ");
                APPEND("...");
            }
        }
        APPEND(")");
        break;
    case TY_STRUCT:
        APPEND("struct ");
        APPEND(ty->agg.tag ? ty->agg.tag : "<anon>");
        break;
    case TY_UNION:
        APPEND("union ");
        APPEND(ty->agg.tag ? ty->agg.tag : "<anon>");
        break;
    case TY_ENUM:
        APPEND("enum ");
        APPEND(ty->enm.tag ? ty->enm.tag : "<anon>");
        break;
    }
#undef APPEND
#undef APPENDF
}

void type_print(const Type *ty, char *buf, size_t bufsz) {
    size_t pos = 0;
    type_print_inner(ty, buf, bufsz, &pos);
    if (bufsz > 0) buf[pos < bufsz ? pos : bufsz - 1] = '\0';
}
