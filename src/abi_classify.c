#include "tinyc/abi_classify.h"
#include "tinyc/type.h"
#include <string.h>

static ABIClass merge_class(ABIClass a, ABIClass b) {
    if (a == b)                    return a;
    if (a == ABI_CLASS_NONE)       return b;
    if (b == ABI_CLASS_NONE)       return a;
    if (a == ABI_CLASS_MEMORY || b == ABI_CLASS_MEMORY) return ABI_CLASS_MEMORY;
    if (a == ABI_CLASS_INTEGER || b == ABI_CLASS_INTEGER) return ABI_CLASS_INTEGER;
    if (a == ABI_CLASS_X87 || b == ABI_CLASS_X87 ||
        a == ABI_CLASS_X87UP || b == ABI_CLASS_X87UP)    return ABI_CLASS_MEMORY;
    return ABI_CLASS_SSE;
}

/* Recursive classifier — fills classes[] for the bytes [offset, offset+size) */
static void classify_range(const Type *ty, int offset,
                            ABIClass *classes, int n_eb) {
    int eb = offset / 8;  /* which eightbyte we're in */
    if (eb >= n_eb) return;

    switch (ty->kind) {
    /* Void — nothing */
    case TY_VOID:
        return;

    /* Boolean, char, short, int, long, long long — all INTEGER */
    case TY_BOOL:
    case TY_CHAR: case TY_SCHAR: case TY_UCHAR:
    case TY_SHORT: case TY_USHORT:
    case TY_INT: case TY_UINT:
    case TY_LONG: case TY_ULONG:
    case TY_LLONG: case TY_ULLONG:
    case TY_ENUM:
        classes[eb] = merge_class(classes[eb], ABI_CLASS_INTEGER);
        return;

    /* Pointer — INTEGER (pointer is just an integer in the ABI) */
    case TY_POINTER:
        classes[eb] = merge_class(classes[eb], ABI_CLASS_INTEGER);
        return;

    /* Float, double — SSE */
    case TY_FLOAT:
    case TY_DOUBLE:
        classes[eb] = merge_class(classes[eb], ABI_CLASS_SSE);
        return;

    /* long double — X87 + X87UP (always passed in memory on Linux/macOS typically, but ABI says X87) */
    case TY_LDOUBLE:
        classes[eb]     = merge_class(classes[eb],     ABI_CLASS_X87);
        if (eb + 1 < n_eb)
            classes[eb + 1] = merge_class(classes[eb + 1], ABI_CLASS_X87UP);
        return;

    /* Array — classify element type at each element offset */
    case TY_ARRAY: {
        if (!ty->arr.count) return;  /* incomplete array → memory (shouldn't happen here) */
        int elem_size = type_sizeof(ty->arr.elem);
        for (int i = 0; i < ty->arr.count; i++) {
            classify_range(ty->arr.elem, offset + i * elem_size, classes, n_eb);
        }
        return;
    }

    /* Struct / union */
    case TY_STRUCT:
    case TY_UNION: {
        int total = type_sizeof(ty);

        /* This backend handles aggregate register passing/returning only for
         * the normal SysV two-eightbyte path. Larger aggregates use memory. */
        if ((total + 7) / 8 > MAX_EIGHTBYTES) {
            for (int i = 0; i < n_eb; i++)
                classes[i] = ABI_CLASS_MEMORY;
            return;
        }

        /* Rule 2: any unaligned field → MEMORY */
        for (StructField *f = ty->agg.fields; f; f = f->next) {
            if (f->offset % type_alignof(f->type) != 0) {
                for (int j = 0; j < n_eb; j++)
                    classes[j] = ABI_CLASS_MEMORY;
                return;
            }
        }

        /* Classify each field at its actual offset within the struct */
        for (StructField *f = ty->agg.fields; f; f = f->next) {
            classify_range(f->type, offset + f->offset, classes, n_eb);
        }

        /* Post-merger fixup rules (ABI §3.2.3 point 5): */
        for (int i = 0; i < n_eb; i++) {
            /* If any eightbyte is MEMORY, the whole thing is MEMORY */
            if (classes[i] == ABI_CLASS_MEMORY) {
                for (int j = 0; j < n_eb; j++)
                    classes[j] = ABI_CLASS_MEMORY;
                return;
            }
            /* X87UP preceded by something other than X87 → MEMORY */
            if (classes[i] == ABI_CLASS_X87UP &&
                (i == 0 || classes[i-1] != ABI_CLASS_X87)) {
                for (int j = 0; j < n_eb; j++)
                    classes[j] = ABI_CLASS_MEMORY;
                return;
            }
        }
        return;
    }

    /* Function pointer — pointer → INTEGER */
    case TY_FUNC:
        classes[eb] = merge_class(classes[eb], ABI_CLASS_INTEGER);
        return;

    default:
        /* Unknown type → conservative: MEMORY */
        for (int i = 0; i < n_eb; i++)
            classes[i] = ABI_CLASS_MEMORY;
        return;
    }
}

ABIClassification abi_classify(const Type *ty) {
    ABIClassification result;
    memset(&result, 0, sizeof(result));

    int total = type_sizeof(ty);
    if (total <= 0) {
        result.n_eightbytes = 0;
        return result;
    }

    result.n_eightbytes = (total + 7) / 8;
    if (result.n_eightbytes > MAX_EIGHTBYTES) {
        /* Too big — all MEMORY */
        result.n_eightbytes = 1;
        result.classes[0] = ABI_CLASS_MEMORY;
        return result;
    }

    /* Initialize to NONE, then classify */
    for (int i = 0; i < result.n_eightbytes; i++)
        result.classes[i] = ABI_CLASS_NONE;

    classify_range(ty, 0, result.classes, result.n_eightbytes);
    return result;
}

bool abi_in_memory(const ABIClassification *c) {
    for (int i = 0; i < c->n_eightbytes; i++)
        if (c->classes[i] == ABI_CLASS_MEMORY)
            return true;
    return false;
}
