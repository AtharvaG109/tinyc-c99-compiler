/*
 * type.h — C99 type representation
 *
 * Types are heap-interned DAG nodes allocated from an Arena. Derived types
 * (pointer, array, function) are structurally interned so that
 * `int *` constructed twice returns the same pointer — enabling O(1)
 * type-equality tests for those kinds. Struct/union/enum types are
 * allocated but NOT interned (identity comes from tag lookup).
 */
#ifndef TINYC_TYPE_H
#define TINYC_TYPE_H

#include "tinyc/arena.h"
#include <stddef.h>

/* ── Type qualifiers ──────────────────────────────────────────────── */
typedef enum {
    QUAL_NONE     = 0,
    QUAL_CONST    = 1 << 0,
    QUAL_VOLATILE = 1 << 1,
    QUAL_RESTRICT = 1 << 2,  /* parsed, semantics ignored */
} TypeQual;

/* ── Type kind ────────────────────────────────────────────────────── */
typedef enum {
    /* Scalars */
    TY_VOID,
    TY_BOOL,     /* _Bool */
    TY_CHAR,
    TY_SCHAR,    /* signed char */
    TY_UCHAR,    /* unsigned char */
    TY_SHORT,
    TY_USHORT,
    TY_INT,
    TY_UINT,
    TY_LONG,
    TY_ULONG,
    TY_LLONG,    /* long long */
    TY_ULLONG,   /* unsigned long long */
    TY_FLOAT,
    TY_DOUBLE,
    TY_LDOUBLE,  /* long double */
    /* Derived */
    TY_POINTER,
    TY_ARRAY,         /* fixed-size: int x[10] */
    TY_ARRAY_UNSIZED, /* incomplete: int x[] */
    TY_FUNC,
    TY_STRUCT,
    TY_UNION,
    TY_ENUM,
} TypeKind;

/* Forward declarations */
typedef struct Type       Type;
typedef struct StructField StructField;
typedef struct EnumConst   EnumConst;
typedef struct FuncParam   FuncParam;

/* ── Struct/union field ───────────────────────────────────────────── */
struct StructField {
    const char  *name;    /* interned; NULL for anonymous members */
    Type        *type;
    int          offset;  /* byte offset within struct/union */
    int          bit_width;  /* -1 for ordinary fields, 0 for zero-width */
    int          bit_offset; /* bit offset inside storage unit */
    StructField *next;
};

/* ── Enum constant ────────────────────────────────────────────────── */
struct EnumConst {
    const char *name;   /* interned */
    long long   value;
    EnumConst  *next;
};

/* ── Function parameter ───────────────────────────────────────────── */
struct FuncParam {
    const char *name;   /* interned; NULL for unnamed/abstract params */
    Type       *type;
    FuncParam  *next;
};

/* ── The type node ────────────────────────────────────────────────── */
struct Type {
    TypeKind kind;
    TypeQual qual;

    union {
        /* TY_POINTER */
        struct {
            Type *pointee;
        } ptr;

        /* TY_ARRAY, TY_ARRAY_UNSIZED */
        struct {
            Type      *elem;
            long long  count;  /* element count; -1 for unsized */
        } arr;

        /* TY_FUNC */
        struct {
            Type      *ret;
            FuncParam *params;   /* linked list */
            int        n_params;
            int        variadic; /* 1 if ends with ... */
        } func;

        /* TY_STRUCT, TY_UNION */
        struct {
            const char  *tag;     /* interned; NULL if anonymous */
            StructField *fields;  /* NULL if incomplete (forward decl) */
            int          size;    /* -1 if incomplete */
            int          align;
        } agg;

        /* TY_ENUM */
        struct {
            const char *tag;      /* interned; NULL if anonymous */
            EnumConst  *consts;
            int         complete; /* 0 if forward-declared only */
        } enm;
    };

    /* Intern chain — for TypeTable hash buckets (derived types only) */
    Type *next_interned;
};

/* ── Type intern table ────────────────────────────────────────────── */
#define TYPE_TABLE_BUCKETS 256

typedef struct {
    Type  *buckets[TYPE_TABLE_BUCKETS];
    Arena *arena;
} TypeTable;

/* ── API ──────────────────────────────────────────────────────────── */

/* Initialize the type table and the static scalar singletons. */
void type_table_init(TypeTable *tt, Arena *arena);

/* Scalar singletons — no allocation, always returns the same pointer. */
Type *type_void(void);
Type *type_bool(void);
Type *type_char(void);
Type *type_schar(void);
Type *type_uchar(void);
Type *type_short(void);
Type *type_ushort(void);
Type *type_int(void);
Type *type_uint(void);
Type *type_long(void);
Type *type_ulong(void);
Type *type_llong(void);
Type *type_ullong(void);
Type *type_float(void);
Type *type_double(void);
Type *type_ldouble(void);

/* Derived types — interned by structural identity. */
Type *type_pointer(TypeTable *tt, Type *pointee, TypeQual qual);
Type *type_array(TypeTable *tt, Type *elem, long long count);
Type *type_array_unsized(TypeTable *tt, Type *elem);
Type *type_func(TypeTable *tt, Type *ret, FuncParam *params,
                int n_params, int variadic);

/* Aggregate/enum — allocated but NOT interned.
 * Returns incomplete type (size=-1, fields=NULL) if tag not yet defined. */
Type *type_struct(TypeTable *tt, const char *tag);
Type *type_union(TypeTable *tt, const char *tag);
Type *type_enum(TypeTable *tt, const char *tag);

/* Return a type with `added` qualifiers set (may return same type if
 * qual already covers `added`; otherwise allocates a new qualified node). */
Type *type_qualify(TypeTable *tt, Type *base, TypeQual added);

/* Size and alignment in bytes for x86-64 LP64.
 * Returns -1 for incomplete types. */
int type_sizeof(const Type *ty);
int type_alignof(const Type *ty);

/* Predicates */
int type_is_integer(const Type *ty);
int type_is_arithmetic(const Type *ty);
int type_is_scalar(const Type *ty);
int type_is_pointer(const Type *ty);
int type_is_complete(const Type *ty);
int type_is_signed(const Type *ty);
int type_is_unsigned_integer(const Type *ty);

/* C99 §6.2.7 compatible-type check (ignores top-level qualifiers).
 * Pointer equality suffices for interned types. */
int type_compatible(const Type *a, const Type *b);

/* Compute struct/union layout (size + field offsets). Must be called
 * once all fields have been added to the aggregate. */
void type_seal_struct(Type *ty);
void type_seal_union(Type *ty);

/* Pretty-print a type into buf for --dump-ast.
 * Always NUL-terminates; truncates if bufsz is too small. */
void type_print(const Type *ty, char *buf, size_t bufsz);

#endif /* TINYC_TYPE_H */
