#ifndef venom_ast_h
#define venom_ast_h

#include <stdbool.h>

#include "dynarray.h"
#include "table.h"

typedef struct Expr Expr;
typedef DynArray(Expr) DynArray_Expr;

typedef struct Stmt Stmt;
typedef DynArray(Stmt) DynArray_Stmt;

typedef enum {
  EXPR_LITERAL,
  EXPR_VARIABLE,
  EXPR_UNARY,
  EXPR_BINARY,
  EXPR_CALL,
  EXPR_GET,
  EXPR_ASSIGN,
  EXPR_STRUCT,
  EXPR_STRUCT_INITIALIZER,
  EXPR_ARRAY,
  EXPR_SUBSCRIPT,
} ExprKind;

typedef enum {
  LIT_BOOLEAN,
  LIT_NUMBER,
  LIT_STRING,
  LIT_NULL,
} LiteralKind;

typedef struct ExprLiteral {
  LiteralKind kind;
  union {
    bool _bool;
    double _double;
    char *str;
  } as;
} ExprLiteral;

typedef struct ExprVariable {
  char *name;
} ExprVariable;

typedef struct ExprUnary {
  Expr *expr;
  char *op;
} ExprUnary;

typedef struct ExprBinary {
  Expr *lhs;
  Expr *rhs;
  char *op;
} ExprBinary;

typedef struct ExprCall {
  Expr *callee;
  DynArray_Expr arguments;
} ExprCall;

typedef struct ExprGet {
  Expr *expr;
  char *property_name;
  char *op;
} ExprGet;

typedef struct ExprAssign {
  Expr *lhs;
  Expr *rhs;
  char *op;
} ExprAssign;

typedef struct ExprStruct {
  char *name;
  DynArray_Expr initializers;
} ExprStruct;

typedef struct ExprStructInitializer {
  Expr *property;
  Expr *value;
} ExprStructInitializer;

typedef struct ExprArray {
  DynArray_Expr elements;
} ExprArray;

typedef struct ExprSubscript {
  Expr *expr;
  Expr *index;
} ExprSubscript;

typedef DynArray(ExprStructInitializer) DynArray_ExprStructInitalizer;

typedef struct Expr {
  ExprKind kind;
  union {
    ExprLiteral expr_literal;
    ExprVariable expr_variable;
    ExprUnary expr_unary;
    ExprBinary expr_binary;
    ExprCall expr_call;
    ExprGet expr_get;
    ExprAssign expr_assign;
    ExprStruct expr_struct;
    ExprStructInitializer expr_struct_initializer;
    ExprArray expr_array;
    ExprSubscript expr_subscript;
  } as;
} Expr;

typedef Table(Expr) Table_Expr;
Table_Expr clone_table_expr(const Table_Expr *table);
void free_table_expr(const Table_Expr *table);

#define AS_EXPR_LITERAL(exp) \
  ((Expr) {.kind = EXPR_LITERAL, .as.expr_literal = (exp)})
#define AS_EXPR_VARIABLE(exp) \
  ((Expr) {.kind = EXPR_VARIABLE, .as.expr_variable = (exp)})
#define AS_EXPR_UNARY(exp) ((Expr) {.kind = EXPR_UNARY, .as.expr_unary = (exp)})
#define AS_EXPR_BINARY(exp) \
  ((Expr) {.kind = EXPR_BINARY, .as.expr_binary = (exp)})
#define AS_EXPR_CALL(exp) ((Expr) {.kind = EXPR_CALL, .as.expr_call = (exp)})
#define AS_EXPR_GET(exp) ((Expr) {.kind = EXPR_GET, .as.expr_get = (exp)})
#define AS_EXPR_ASSIGN(exp) \
  ((Expr) {.kind = EXPR_ASSIGN, .as.expr_assign = (exp)})
#define AS_EXPR_STRUCT(exp) \
  ((Expr) {.kind = EXPR_STRUCT, .as.expr_struct = (exp)})
#define AS_EXPR_STRUCT_INITIALIZER(exp)     \
  ((Expr) {.kind = EXPR_STRUCT_INITIALIZER, \
           .as.expr_struct_initializer = (exp)})
#define AS_EXPR_ARRAY(exp) ((Expr) {.kind = EXPR_ARRAY, .as.expr_array = (exp)})
#define AS_EXPR_SUBSCRIPT(exp) \
  ((Expr) {.kind = EXPR_SUBSCRIPT, .as.expr_subscript = (exp)})

typedef enum {
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
  STMT_DECORATOR,
  STMT_RETURN,
  STMT_STRUCT,
  STMT_IMPL,
  STMT_USE,
  STMT_YIELD,
  STMT_ASSERT,
} StmtKind;

typedef struct {
  char *name;
  Expr initializer;
} StmtLet;

typedef struct {
  Expr expr;
} StmtPrint;

typedef struct {
  Expr expr;
} StmtExpr;

typedef struct {
  DynArray_Stmt stmts;
  size_t depth;
} StmtBlock;

typedef struct {
  DynArray_char_ptr parameters;
  char *name;
  Stmt *body;
} StmtFn;

typedef struct {
  char *name;
  Stmt *fn;
} StmtDecorator;

typedef struct {
  Expr condition;
  Stmt *then_branch;
  Stmt *else_branch;
} StmtIf;

typedef struct {
  Expr condition;
  Stmt *body;
  char *label;
} StmtWhile;

typedef struct {
  Expr initializer;
  Expr condition;
  Expr advancement;
  Stmt *body;
  char *label;
} StmtFor;

typedef struct {
  Expr expr;
} StmtReturn;

typedef struct {
  char *name;
  DynArray_char_ptr properties;
} StmtStruct;

typedef struct {
  char *name;
  DynArray_Stmt methods;
} StmtImpl;

typedef struct {
  char *label;
} StmtBreak;

typedef struct {
  char *label;
} StmtContinue;

typedef struct {
  char *path;
} StmtUse;

typedef struct {
  Expr expr;
} StmtYield;

typedef struct {
  Expr expr;
} StmtAssert;

typedef struct Stmt {
  StmtKind kind;
  union {
    StmtPrint stmt_print;
    StmtLet stmt_let;
    StmtExpr stmt_expr;
    StmtBlock stmt_block;
    StmtFn stmt_fn;
    StmtDecorator stmt_decorator;
    StmtIf stmt_if;
    StmtWhile stmt_while;
    StmtFor stmt_for;
    StmtBreak stmt_break;
    StmtContinue stmt_continue;
    StmtReturn stmt_return;
    StmtStruct stmt_struct;
    StmtImpl stmt_impl;
    StmtUse stmt_use;
    StmtYield stmt_yield;
    StmtAssert stmt_assert;
  } as;
} Stmt;

#define AS_STMT_PRINT(stmt) \
  ((Stmt) {.kind = STMT_PRINT, .as.stmt_print = (stmt)})
#define AS_STMT_LET(stmt) ((Stmt) {.kind = STMT_LET, .as.stmt_let = (stmt)})
#define AS_STMT_EXPR(stmt) ((Stmt) {.kind = STMT_EXPR, .as.stmt_expr = (stmt)})
#define AS_STMT_BLOCK(stmt) \
  ((Stmt) {.kind = STMT_BLOCK, .as.stmt_block = (stmt)})
#define AS_STMT_FN(stmt) ((Stmt) {.kind = STMT_FN, .as.stmt_fn = (stmt)})
#define AS_STMT_DECORATOR(stmt) \
  ((Stmt) {.kind = STMT_DECORATOR, .as.stmt_decorator = (stmt)})
#define AS_STMT_IF(stmt) ((Stmt) {.kind = STMT_IF, .as.stmt_if = (stmt)})
#define AS_STMT_WHILE(stmt) \
  ((Stmt) {.kind = STMT_WHILE, .as.stmt_while = (stmt)})
#define AS_STMT_FOR(stmt) ((Stmt) {.kind = STMT_FOR, .as.stmt_for = (stmt)})
#define AS_STMT_BREAK(stmt) \
  ((Stmt) {.kind = STMT_BREAK, .as.stmt_break = (stmt)})
#define AS_STMT_CONTINUE(stmt) \
  ((Stmt) {.kind = STMT_CONTINUE, .as.stmt_continue = (stmt)})
#define AS_STMT_RETURN(stmt) \
  ((Stmt) {.kind = STMT_RETURN, .as.stmt_return = (stmt)})
#define AS_STMT_STRUCT(stmt) \
  ((Stmt) {.kind = STMT_STRUCT, .as.stmt_struct = (stmt)})
#define AS_STMT_IMPL(stmt) ((Stmt) {.kind = STMT_IMPL, .as.stmt_impl = (stmt)})
#define AS_STMT_USE(stmt) ((Stmt) {.kind = STMT_USE, .as.stmt_use = (stmt)})
#define AS_STMT_YIELD(stmt) \
  ((Stmt) {.kind = STMT_YIELD, .as.stmt_yield = (stmt)})
#define AS_STMT_ASSERT(stmt) \
  ((Stmt) {.kind = STMT_ASSERT, .as.stmt_assert = (stmt)})

void print_expr(const Expr *expr, int indent);
void print_stmt(const Stmt *stmt, int indent, bool continuation);
void print_ast(const DynArray_Stmt *ast);
void free_expr(const Expr *expr);
void free_stmt(const Stmt *stmt);
void free_ast(const DynArray_Stmt *ast);
ExprLiteral clone_literal(const ExprLiteral *literal);
Expr clone_expr(const Expr *expr);
Stmt clone_stmt(const Stmt *stmt);
DynArray_Stmt clone_ast(const DynArray_Stmt *ast);

#endif
