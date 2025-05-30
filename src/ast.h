#ifndef venom_ast_h
#define venom_ast_h

#include <stdbool.h>
#include <stddef.h>

#include "dynarray.h"
#include "table.h"
#include "tokenizer.h"

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
  EXPR_CONDITIONAL,
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
  Span span;
} ExprLiteral;

typedef struct ExprVariable {
  char *name;
  Span span;
} ExprVariable;

typedef struct ExprUnary {
  Expr *expr;
  char *op;
  Span span;
} ExprUnary;

typedef struct ExprBinary {
  Expr *lhs;
  Expr *rhs;
  char *op;
  Span span;
} ExprBinary;

typedef struct ExprCall {
  Expr *callee;
  DynArray_Expr arguments;
  Span span;
} ExprCall;

typedef struct ExprGet {
  Expr *expr;
  char *property_name;
  char *op;
  Span span;
} ExprGet;

typedef struct ExprAssign {
  Expr *lhs;
  Expr *rhs;
  char *op;
  Span span;
} ExprAssign;

typedef struct ExprStruct {
  char *name;
  DynArray_Expr initializers;
  Span span;
} ExprStruct;

typedef struct ExprStructInitializer {
  Expr *property;
  Expr *value;
  Span span;
} ExprStructInitializer;

typedef struct ExprArray {
  DynArray_Expr elements;
  Span span;
} ExprArray;

typedef struct ExprSubscript {
  Expr *expr;
  Expr *index;
  Span span;
} ExprSubscript;

typedef struct ExprConditional {
  Expr *condition;
  Expr *then_branch;
  Expr *else_branch;
  Span span;
} ExprConditional;

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
    ExprConditional expr_conditional;
  } as;
  Span span;
} Expr;

typedef Table(Expr) Table_Expr;
Table_Expr clone_table_expr(const Table_Expr *table);
void free_table_expr(const Table_Expr *table);

#define AS_EXPR_LITERAL(exp) \
  ((Expr) {.kind = EXPR_LITERAL, .as.expr_literal = (exp), .span = (exp).span})
#define AS_EXPR_VARIABLE(exp) \
  ((Expr) {                   \
      .kind = EXPR_VARIABLE, .as.expr_variable = (exp), .span = (exp).span})
#define AS_EXPR_UNARY(exp) \
  ((Expr) {.kind = EXPR_UNARY, .as.expr_unary = (exp), .span = (exp).span})
#define AS_EXPR_BINARY(exp) \
  ((Expr) {.kind = EXPR_BINARY, .as.expr_binary = (exp), .span = (exp).span})
#define AS_EXPR_CALL(exp) \
  ((Expr) {.kind = EXPR_CALL, .as.expr_call = (exp), .span = (exp).span})
#define AS_EXPR_GET(exp) \
  ((Expr) {.kind = EXPR_GET, .as.expr_get = (exp), .span = (exp).span})
#define AS_EXPR_ASSIGN(exp) \
  ((Expr) {.kind = EXPR_ASSIGN, .as.expr_assign = (exp), .span = (exp).span})
#define AS_EXPR_STRUCT(exp) \
  ((Expr) {.kind = EXPR_STRUCT, .as.expr_struct = (exp), .span = (exp).span})
#define AS_EXPR_STRUCT_INITIALIZER(exp)         \
  ((Expr) {.kind = EXPR_STRUCT_INITIALIZER,     \
           .as.expr_struct_initializer = (exp), \
           .span = (exp).span})
#define AS_EXPR_ARRAY(exp) \
  ((Expr) {.kind = EXPR_ARRAY, .as.expr_array = (exp), .span = (exp).span})
#define AS_EXPR_SUBSCRIPT(exp) \
  ((Expr) {                    \
      .kind = EXPR_SUBSCRIPT, .as.expr_subscript = (exp), .span = (exp).span})
#define AS_EXPR_CONDITIONAL(exp)         \
  ((Expr) {.kind = EXPR_CONDITIONAL,     \
           .as.expr_conditional = (exp), \
           .span = (exp).span})

typedef enum {
  STMT_LET,
  STMT_EXPR,
  STMT_PRINT,
  STMT_BLOCK,
  STMT_IF,
  STMT_DO_WHILE,
  STMT_WHILE,
  STMT_FOR,
  STMT_BREAK,
  STMT_CONTINUE,
  STMT_GOTO,
  STMT_LABELED,
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
  Span span;
} StmtLet;

typedef struct {
  Expr expr;
  Span span;
} StmtPrint;

typedef struct {
  Expr expr;
  Span span;
} StmtExpr;

typedef struct {
  DynArray_Stmt stmts;
  size_t depth;
  Span span;
} StmtBlock;

typedef struct {
  DynArray_char_ptr parameters;
  char *name;
  Stmt *body;
  Span span;
} StmtFn;

typedef struct {
  char *name;
  Stmt *fn;
  Span span;
} StmtDecorator;

typedef struct {
  Expr condition;
  Stmt *then_branch;
  Stmt *else_branch;
  Span span;
} StmtIf;

typedef struct {
  Expr condition;
  Stmt *body;
  char *label;
  Span span;
} StmtWhile;

typedef struct {
  Expr condition;
  Stmt *body;
  char *label;
  Span span;
} StmtDoWhile;

typedef struct {
  Expr initializer;
  Expr condition;
  Expr advancement;
  Stmt *body;
  char *label;
  Span span;
} StmtFor;

typedef struct {
  Expr expr;
  Span span;
} StmtReturn;

typedef struct {
  char *name;
  DynArray_char_ptr properties;
  Span span;
} StmtStruct;

typedef struct {
  char *name;
  DynArray_Stmt methods;
  Span span;
} StmtImpl;

typedef struct {
  char *label;
  Span span;
} StmtBreak;

typedef struct {
  char *label;
  Span span;
} StmtGoto;

typedef struct {
  Stmt *stmt;
  char *label;
  Span span;
} StmtLabeled;
typedef struct {
  char *label;
  Span span;
} StmtContinue;

typedef struct {
  char *path;
  Span span;
} StmtUse;

typedef struct {
  Expr expr;
  Span span;
} StmtYield;

typedef struct {
  Expr expr;
  Span span;
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
    StmtDoWhile stmt_do_while;
    StmtFor stmt_for;
    StmtBreak stmt_break;
    StmtContinue stmt_continue;
    StmtGoto stmt_goto;
    StmtLabeled stmt_labeled;
    StmtReturn stmt_return;
    StmtStruct stmt_struct;
    StmtImpl stmt_impl;
    StmtUse stmt_use;
    StmtYield stmt_yield;
    StmtAssert stmt_assert;
  } as;
  Span span;
} Stmt;

#define AS_STMT_PRINT(stmt) \
  ((Stmt) {.kind = STMT_PRINT, .as.stmt_print = (stmt), .span = (stmt).span})
#define AS_STMT_LET(stmt) \
  ((Stmt) {.kind = STMT_LET, .as.stmt_let = (stmt), .span = (stmt).span})
#define AS_STMT_EXPR(stmt) \
  ((Stmt) {.kind = STMT_EXPR, .as.stmt_expr = (stmt), .span = (stmt).span})
#define AS_STMT_BLOCK(stmt) \
  ((Stmt) {.kind = STMT_BLOCK, .as.stmt_block = (stmt), .span = (stmt).span})
#define AS_STMT_FN(stmt) \
  ((Stmt) {.kind = STMT_FN, .as.stmt_fn = (stmt), .span = (stmt).span})
#define AS_STMT_DECORATOR(stmt)         \
  ((Stmt) {.kind = STMT_DECORATOR,      \
           .as.stmt_decorator = (stmt), \
           .span = (stmt).span})
#define AS_STMT_IF(stmt) \
  ((Stmt) {.kind = STMT_IF, .as.stmt_if = (stmt), .span = (stmt).span})
#define AS_STMT_WHILE(stmt) \
  ((Stmt) {.kind = STMT_WHILE, .as.stmt_while = (stmt), .span = (stmt).span})
#define AS_STMT_DO_WHILE(stmt) \
  ((Stmt) {                    \
      .kind = STMT_DO_WHILE, .as.stmt_do_while = (stmt), .span = (stmt).span})
#define AS_STMT_FOR(stmt) \
  ((Stmt) {.kind = STMT_FOR, .as.stmt_for = (stmt), .span = (stmt).span})
#define AS_STMT_BREAK(stmt) \
  ((Stmt) {.kind = STMT_BREAK, .as.stmt_break = (stmt), .span = (stmt).span})
#define AS_STMT_CONTINUE(stmt) \
  ((Stmt) {                    \
      .kind = STMT_CONTINUE, .as.stmt_continue = (stmt), .span = (stmt).span})
#define AS_STMT_GOTO(stmt) \
  ((Stmt) {.kind = STMT_GOTO, .as.stmt_goto = (stmt), .span = (stmt).span})
#define AS_STMT_LABELED(stmt) \
  ((Stmt) {                   \
      .kind = STMT_LABELED, .as.stmt_labeled = (stmt), .span = (stmt).span})
#define AS_STMT_RETURN(stmt) \
  ((Stmt) {.kind = STMT_RETURN, .as.stmt_return = (stmt), .span = (stmt).span})
#define AS_STMT_STRUCT(stmt) \
  ((Stmt) {.kind = STMT_STRUCT, .as.stmt_struct = (stmt), .span = (stmt).span})
#define AS_STMT_IMPL(stmt) \
  ((Stmt) {.kind = STMT_IMPL, .as.stmt_impl = (stmt), .span = (stmt).span})
#define AS_STMT_USE(stmt) \
  ((Stmt) {.kind = STMT_USE, .as.stmt_use = (stmt), .span = (stmt).span})
#define AS_STMT_YIELD(stmt) \
  ((Stmt) {.kind = STMT_YIELD, .as.stmt_yield = (stmt), .span = (stmt).span})
#define AS_STMT_ASSERT(stmt) \
  ((Stmt) {.kind = STMT_ASSERT, .as.stmt_assert = (stmt), .span = (stmt).span})

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
