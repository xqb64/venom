#ifndef venom_ast_h
#define venom_ast_h

#include <stdbool.h>

#include "dynarray.h"
#include "table.h"

typedef struct Expr Expr;
typedef DynArray(Expr) DynArray_Expr;

typedef struct Stmt Stmt;
typedef DynArray(Stmt) DynArray_Stmt;

typedef enum
{
    EXPR_LIT,
    EXPR_VAR,
    EXPR_UNA,
    EXPR_BIN,
    EXPR_CALL,
    EXPR_GET,
    EXPR_ASS,
    EXPR_STRUCT,
    EXPR_S_INIT,
    EXPR_ARRAY,
    EXPR_SUBSCRIPT,
} ExprKind;

typedef enum
{
    LIT_BOOL,
    LIT_NUM,
    LIT_STR,
    LIT_NULL,
} LiteralKind;

typedef struct ExprLit
{
    LiteralKind kind;
    union {
        bool _bool;
        double _double;
        char *str;
    } as;
} ExprLit;

typedef struct ExprVar
{
    char *name;
} ExprVar;

typedef struct ExprUnary
{
    Expr *exp;
    char *op;
} ExprUnary;

typedef struct ExprBin
{
    Expr *lhs;
    Expr *rhs;
    char *op;
} ExprBin;

typedef struct ExprCall
{
    Expr *callee;
    DynArray_Expr arguments;
} ExprCall;

typedef struct ExprGet
{
    Expr *exp;
    char *property_name;
    char *op;
} ExprGet;

typedef struct ExprAssign
{
    Expr *lhs;
    Expr *rhs;
    char *op;
} ExprAssign;

typedef struct ExprStruct
{
    char *name;
    DynArray_Expr initializers;
} ExprStruct;

typedef struct ExprStructInit
{
    Expr *property;
    Expr *value;
} ExprStructInit;

typedef struct ExprArray
{
    DynArray_Expr elements;
} ExprArray;

typedef struct ExprSubscript
{
    Expr *expr;
    Expr *index;
} ExprSubscript;

typedef DynArray(ExprStructInit) DynArray_ExprStructInit;

typedef struct Expr
{
    ExprKind kind;
    union {
        ExprLit expr_lit;
        ExprVar expr_var;
        ExprUnary expr_una;
        ExprBin expr_bin;
        ExprCall expr_call;
        ExprGet expr_get;
        ExprAssign expr_ass;
        ExprStruct expr_struct;
        ExprStructInit expr_s_init;
        ExprArray expr_array;
        ExprSubscript expr_subscript;
    } as;
} Expr;

typedef Table(Expr) Table_Expr;
Table_Expr clone_table_expr(const Table_Expr *table);
void free_table_expr(const Table_Expr *table);

#define AS_EXPR_LIT(exp)       ((Expr) {.kind = EXPR_LIT, .as.expr_lit = (exp)})
#define AS_EXPR_VAR(exp)       ((Expr) {.kind = EXPR_VAR, .as.expr_var = (exp)})
#define AS_EXPR_UNA(exp)       ((Expr) {.kind = EXPR_UNA, .as.expr_una = (exp)})
#define AS_EXPR_BIN(exp)       ((Expr) {.kind = EXPR_BIN, .as.expr_bin = (exp)})
#define AS_EXPR_CALL(exp)      ((Expr) {.kind = EXPR_CALL, .as.expr_call = (exp)})
#define AS_EXPR_GET(exp)       ((Expr) {.kind = EXPR_GET, .as.expr_get = (exp)})
#define AS_EXPR_ASS(exp)       ((Expr) {.kind = EXPR_ASS, .as.expr_ass = (exp)})
#define AS_EXPR_LOG(exp)       ((Expr) {.kind = EXPR_LOG, .as.expr_log = (exp)})
#define AS_EXPR_STRUCT(exp)    ((Expr) {.kind = EXPR_STRUCT, .as.expr_struct = (exp)})
#define AS_EXPR_S_INIT(exp)    ((Expr) {.kind = EXPR_S_INIT, .as.expr_s_init = (exp)})
#define AS_EXPR_ARRAY(exp)     ((Expr) {.kind = EXPR_ARRAY, .as.expr_array = (exp)})
#define AS_EXPR_SUBSCRIPT(exp) ((Expr) {.kind = EXPR_SUBSCRIPT, .as.expr_subscript = (exp)})

typedef enum
{
    STMT_LET,
    STMT_EXPR,
    STMT_PRINT,
    STMT_BLOCK,
    STMT_IF,
    STMT_WHILE,
    STMT_FOR,
    STMT_BREAK,
    STMT_CONTINUE,
    STMT_FN,
    STMT_DECO,
    STMT_RETURN,
    STMT_STRUCT,
    STMT_IMPL,
    STMT_USE,
    STMT_YIELD,
    STMT_ASSERT,
} StmtKind;

typedef struct
{
    char *name;
    Expr initializer;
} StmtLet;

typedef struct
{
    Expr exp;
} StmtPrint;

typedef struct
{
    Expr exp;
} StmtExpr;

typedef struct
{
    DynArray_Stmt stmts;
    size_t depth;
} StmtBlock;

typedef struct
{
    DynArray_char_ptr parameters;
    char *name;
    Stmt *body;
} StmtFn;

typedef struct
{
    char *name;
    Stmt *fn;
} StmtDeco;

typedef struct
{
    Expr condition;
    Stmt *then_branch;
    Stmt *else_branch;
} StmtIf;

typedef struct
{
    Expr condition;
    Stmt *body;
    char *label;
} StmtWhile;

typedef struct
{
    Expr initializer;
    Expr condition;
    Expr advancement;
    Stmt *body;
    char *label;
} StmtFor;

typedef struct
{
    Expr returnval;
} StmtRet;

typedef struct
{
    char *name;
    DynArray_char_ptr properties;
} StmtStruct;

typedef struct
{
    char *name;
    DynArray_Stmt methods;
} StmtImpl;

typedef struct
{
    char *label;
} StmtBreak;

typedef struct
{
    char *label;
} StmtContinue;

typedef struct
{
    char *path;
} StmtUse;

typedef struct
{
    Expr exp;
} StmtYield;

typedef struct
{
    Expr exp;
} StmtAssert;

typedef struct Stmt
{
    StmtKind kind;
    union {
        StmtPrint stmt_print;
        StmtLet stmt_let;
        StmtExpr stmt_expr;
        StmtBlock stmt_block;
        StmtFn stmt_fn;
        StmtDeco stmt_deco;
        StmtIf stmt_if;
        StmtWhile stmt_while;
        StmtFor stmt_for;
        StmtBreak stmt_break;
        StmtContinue stmt_continue;
        StmtRet stmt_return;
        StmtStruct stmt_struct;
        StmtImpl stmt_impl;
        StmtUse stmt_use;
        StmtYield stmt_yield;
        StmtAssert stmt_assert;
    } as;
} Stmt;

#define AS_STMT_PRINT(stmt)    ((Stmt) {.kind = STMT_PRINT, .as.stmt_print = (stmt)})
#define AS_STMT_LET(stmt)      ((Stmt) {.kind = STMT_LET, .as.stmt_let = (stmt)})
#define AS_STMT_EXPR(stmt)     ((Stmt) {.kind = STMT_EXPR, .as.stmt_expr = (stmt)})
#define AS_STMT_BLOCK(stmt)    ((Stmt) {.kind = STMT_BLOCK, .as.stmt_block = (stmt)})
#define AS_STMT_FN(stmt)       ((Stmt) {.kind = STMT_FN, .as.stmt_fn = (stmt)})
#define AS_STMT_DECO(stmt)     ((Stmt) {.kind = STMT_DECO, .as.stmt_deco = (stmt)})
#define AS_STMT_IF(stmt)       ((Stmt) {.kind = STMT_IF, .as.stmt_if = (stmt)})
#define AS_STMT_WHILE(stmt)    ((Stmt) {.kind = STMT_WHILE, .as.stmt_while = (stmt)})
#define AS_STMT_FOR(stmt)      ((Stmt) {.kind = STMT_FOR, .as.stmt_for = (stmt)})
#define AS_STMT_BREAK(stmt)    ((Stmt) {.kind = STMT_BREAK, .as.stmt_break = (stmt)})
#define AS_STMT_CONTINUE(stmt) ((Stmt) {.kind = STMT_CONTINUE, .as.stmt_continue = (stmt)})
#define AS_STMT_RETURN(stmt)   ((Stmt) {.kind = STMT_RETURN, .as.stmt_return = (stmt)})
#define AS_STMT_STRUCT(stmt)   ((Stmt) {.kind = STMT_STRUCT, .as.stmt_struct = (stmt)})
#define AS_STMT_IMPL(stmt)     ((Stmt) {.kind = STMT_IMPL, .as.stmt_impl = (stmt)})
#define AS_STMT_USE(stmt)      ((Stmt) {.kind = STMT_USE, .as.stmt_use = (stmt)})
#define AS_STMT_YIELD(stmt)    ((Stmt) {.kind = STMT_YIELD, .as.stmt_yield = (stmt)})
#define AS_STMT_ASSERT(stmt)   ((Stmt) {.kind = STMT_ASSERT, .as.stmt_assert = (stmt)})

void print_expression(const Expr *expr, int indent);
void print_stmt(const Stmt *stmt, int indent, bool continuation);
void pretty_print(const DynArray_Stmt *ast);
void free_stmt(const Stmt *stmt);
void free_expression(const Expr *expr);
ExprLit clone_literal(const ExprLit *literal);
Expr clone_expr(const Expr *expr);
Stmt clone_stmt(const Stmt *stmt);
DynArray_Stmt clone_ast(const DynArray_Stmt *ast);

#endif
