/*
 * ast.h — Abstract Syntax Tree node definitions for C99
 *
 * Every node is a fixed-size tagged union allocated from an Arena.
 * Children are stored as pointers; lists use the intrusive `next` field.
 * The `type` field is NULL during parsing and filled in by the sema pass.
 */
#ifndef TINYC_AST_H
#define TINYC_AST_H

#include "tinyc/type.h"
#include "tinyc/arena.h"
#include "tinyc/token.h"

/* ── Node kind ────────────────────────────────────────────────────── */
typedef enum {
    /* ── Top level ─────────────────────────────────────── */
    AST_TRANSLATION_UNIT,   /* root; decls linked via ->next */

    /* ── Declarations ──────────────────────────────────── */
    AST_FUNC_DEF,     /* function definition */
    AST_VAR_DECL,     /* variable / global declaration */
    AST_TYPEDEF_DECL, /* typedef name = type */
    AST_STRUCT_DECL,  /* struct/union type definition (standalone) */
    AST_ENUM_DECL,    /* enum type definition (standalone) */
    AST_PARAM_DECL,   /* function parameter */

    /* ── Statements ────────────────────────────────────── */
    AST_COMPOUND,     /* { stmts... } */
    AST_EXPR_STMT,    /* expr ; */
    AST_DECL_STMT,    /* declaration inside a block */
    AST_IF,
    AST_WHILE,
    AST_DO_WHILE,
    AST_FOR,
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,
    AST_GOTO,
    AST_LABEL,        /* label: stmt */
    AST_SWITCH,
    AST_CASE,         /* case expr: stmt */
    AST_DEFAULT,      /* default: stmt */

    /* ── Expressions ───────────────────────────────────── */
    AST_INT_LIT,
    AST_FLOAT_LIT,
    AST_CHAR_LIT,
    AST_STR_LIT,
    AST_IDENT,

    AST_UNOP,         /* prefix: -, !, ~, &, *, ++x, --x */
    AST_POSTFIX,      /* postfix: x++, x-- */
    AST_BINOP,        /* +, -, *, /, %, &, |, ^, <<, >>, ==, !=, <, >, <=, >= */
    AST_ASSIGN,       /* =, +=, -=, *=, /=, %=, &=, |=, ^=, <<=, >>= */
    AST_TERNARY,      /* cond ? then : else */
    AST_CALL,         /* f(args...) */
    AST_SUBSCRIPT,    /* a[i] */
    AST_MEMBER,       /* a.field */
    AST_MEMBER_PTR,   /* a->field */
    AST_CAST,         /* (Type)expr */
    AST_SIZEOF_EXPR,  /* sizeof expr */
    AST_SIZEOF_TYPE,  /* sizeof(type) */
    AST_COMMA,        /* a, b (comma operator) */

    /* ── Initializers ──────────────────────────────────── */
    AST_INIT_LIST,    /* { expr, ... } or { .f=v, [i]=v, ... } */
    AST_DESIGNATOR,   /* .field = or [idx] = within an init list */
    AST_COMPOUND_LIT, /* (Type){ init-list } */
} ASTNodeKind;

/* ── Storage class / function specifier ──────────────────────────── */
typedef enum {
    SC_NONE,
    SC_AUTO,
    SC_STATIC,
    SC_EXTERN,
    SC_REGISTER,
    SC_TYPEDEF,  /* used during parsing only; resolved to AST_TYPEDEF_DECL */
} StorageClass;

typedef enum {
    FS_NONE,
    FS_INLINE,
} FuncSpec;

/* ── The node ─────────────────────────────────────────────────────── */
typedef struct ASTNode ASTNode;

struct ASTNode {
    ASTNodeKind kind;
    int line;       /* source line of first token */
    int col;        /* source column of first token */
    Type *type;     /* NULL during parsing; filled by sema */
    ASTNode *next;  /* intrusive list link (stmts, args, members, etc.) */

    union {
        /* AST_TRANSLATION_UNIT */
        struct {
            ASTNode *decls;   /* linked via ->next */
        } tu;

        /* AST_FUNC_DEF */
        struct {
            const char  *name;
            Type        *func_type; /* TY_FUNC */
            ASTNode     *params;    /* AST_PARAM_DECL list */
            ASTNode     *body;      /* AST_COMPOUND */
            StorageClass storage;
            FuncSpec     spec;
        } func_def;

        /* AST_VAR_DECL */
        struct {
            const char  *name;
            Type        *decl_type;
            ASTNode     *init;      /* expression or AST_INIT_LIST; NULL if none */
            StorageClass storage;
        } var_decl;

        /* AST_TYPEDEF_DECL */
        struct {
            const char *name;
            Type       *aliased;
        } typedef_decl;

        /* AST_PARAM_DECL */
        struct {
            const char *name;       /* NULL for abstract declarators */
            Type       *param_type;
        } param_decl;

        /* AST_STRUCT_DECL, AST_ENUM_DECL */
        struct {
            Type    *the_type;  /* TY_STRUCT/TY_UNION/TY_ENUM */
        } type_decl;

        /* AST_COMPOUND */
        struct {
            ASTNode *stmts;   /* linked via ->next */
        } compound;

        /* AST_EXPR_STMT */
        struct {
            ASTNode *expr;
        } expr_stmt;

        /* AST_DECL_STMT */
        struct {
            ASTNode *decl;   /* AST_VAR_DECL or AST_TYPEDEF_DECL */
        } decl_stmt;

        /* AST_IF */
        struct {
            ASTNode *cond;
            ASTNode *then_stmt;
            ASTNode *else_stmt;  /* NULL if no else */
        } if_stmt;

        /* AST_WHILE, AST_DO_WHILE */
        struct {
            ASTNode *cond;
            ASTNode *body;
        } while_stmt;

        /* AST_FOR */
        struct {
            ASTNode *init;  /* expr stmt or decl stmt; NULL for ;; */
            ASTNode *cond;  /* NULL for for(;;) */
            ASTNode *incr;  /* NULL if absent */
            ASTNode *body;
        } for_stmt;

        /* AST_RETURN */
        struct {
            ASTNode *expr;  /* NULL for bare return */
        } ret;

        /* AST_GOTO */
        struct {
            const char *label;
        } go;

        /* AST_LABEL */
        struct {
            const char *label;
            ASTNode    *stmt;
        } label;

        /* AST_SWITCH */
        struct {
            ASTNode *expr;
            ASTNode *body;
        } switch_stmt;

        /* AST_CASE */
        struct {
            ASTNode *expr;  /* constant integer expression */
            ASTNode *stmt;
        } case_stmt;

        /* AST_DEFAULT */
        struct {
            ASTNode *stmt;
        } default_stmt;

        /* AST_INT_LIT, AST_CHAR_LIT */
        struct {
            unsigned long long val;
            int                suffix;   /* IntSuffix flags */
            int                is_wide;
        } int_lit;

        /* AST_FLOAT_LIT */
        struct {
            double val;
            int    suffix;  /* 0=double, 'f'=float, 'l'=long double */
        } float_lit;

        /* AST_STR_LIT */
        struct {
            const char *str;     /* NUL-terminated decoded value (interned) */
            int         is_wide;
        } str_lit;

        /* AST_IDENT */
        struct {
            const char *name;
        } ident;

        /* AST_UNOP — prefix */
        struct {
            TokenKind  op;  /* TOK_MINUS, TOK_BANG, TOK_TILDE, TOK_AMP,
                               TOK_STAR, TOK_PLUSPLUS, TOK_MINUSMINUS */
            ASTNode   *operand;
        } unop;

        /* AST_POSTFIX */
        struct {
            TokenKind  op;  /* TOK_PLUSPLUS, TOK_MINUSMINUS */
            ASTNode   *operand;
        } postfix;

        /* AST_BINOP */
        struct {
            TokenKind  op;
            ASTNode   *left;
            ASTNode   *right;
        } binop;

        /* AST_ASSIGN */
        struct {
            TokenKind  op;   /* TOK_ASSIGN, TOK_PLUS_ASSIGN, etc. */
            ASTNode   *left;
            ASTNode   *right;
        } assign;

        /* AST_TERNARY */
        struct {
            ASTNode *cond;
            ASTNode *then_expr;
            ASTNode *else_expr;
        } ternary;

        /* AST_CALL */
        struct {
            ASTNode *func;    /* callee expression */
            ASTNode *args;    /* argument list linked via ->next */
            int      n_args;
        } call;

        /* AST_SUBSCRIPT */
        struct {
            ASTNode *base;
            ASTNode *index;
        } subscript;

        /* AST_MEMBER, AST_MEMBER_PTR */
        struct {
            ASTNode    *base;
            const char *field;
        } member;

        /* AST_CAST */
        struct {
            Type    *cast_type;
            ASTNode *expr;
        } cast;

        /* AST_SIZEOF_EXPR */
        struct {
            ASTNode *expr;
        } sizeof_expr;

        /* AST_SIZEOF_TYPE */
        struct {
            Type *sizeof_type;
        } sizeof_type;

        /* AST_COMMA */
        struct {
            ASTNode *left;
            ASTNode *right;
        } comma;

        /* AST_INIT_LIST */
        struct {
            ASTNode *items;  /* expr or AST_DESIGNATOR nodes, linked via ->next */
            int      n_items;
        } init_list;

        /* AST_DESIGNATOR */
        struct {
            int         is_field;  /* 1 = .field, 0 = [idx] */
            const char *field;     /* used when is_field == 1 */
            ASTNode    *index;     /* used when is_field == 0 */
            ASTNode    *value;     /* the assigned expression */
        } designator;

        /* AST_COMPOUND_LIT */
        struct {
            Type    *lit_type;
            ASTNode *init;     /* AST_INIT_LIST */
        } compound_lit;
    };
};

/* ── Arena allocation helpers ────────────────────────────────────── */

/* Allocate a zero-initialized ASTNode from the arena. */
ASTNode *ast_alloc(Arena *arena, ASTNodeKind kind, int line, int col);

/* Append `node` to a singly-linked list.
 * `*head` and `*tail` are updated. Pass NULL for tail_out to accept O(n). */
void ast_list_append(ASTNode **head, ASTNode **tail_inout, ASTNode *node);

/* Count nodes in a linked list. */
int ast_list_len(const ASTNode *head);

/* ── AST printer ─────────────────────────────────────────────────── */

/* Print a human-readable indented tree to stdout. */
void ast_print(const ASTNode *node, int indent);

#endif /* TINYC_AST_H */
