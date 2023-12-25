#ifndef venom_parser_h
#define venom_parser_h

#include <stdbool.h>
#include <stddef.h>

#include "dynarray.h"
#include "tokenizer.h"

typedef enum {
  EXPR_LIT,
  EXPR_VAR,
  EXPR_STR,
  EXPR_UNA,
  EXPR_BIN,
  EXPR_CALL,
  EXPR_GET,
  EXPR_ASS,
  EXPR_LOG,
  EXPR_STRUCT,
  EXPR_S_INIT,
  EXPR_ARRAY,
  EXPR_SUBSCRIPT,
} __attribute__((__packed__)) ExprKind;

typedef struct Expr Expr;

typedef DynArray(Expr) DynArray_Expr;

typedef enum {
  LIT_BOOL,
  LIT_NUM,
  LIT_STR,
  LIT_NULL,
} LiteralKind;

typedef struct ExprLit {
  LiteralKind kind;
  union {
    bool bval;
    double dval;
    char *sval;
  } as;
} ExprLit;

typedef struct ExprVar {
  char *name;
} ExprVar;

typedef struct ExprUnary {
  Expr *exp;
  char *op;
} ExprUnary;

typedef struct ExprBin {
  Expr *lhs;
  Expr *rhs;
  char *op;
} ExprBin;

typedef struct ExprCall {
  Expr *callee;
  DynArray_Expr arguments;
} ExprCall;

typedef struct ExprGet {
  Expr *exp;
  char *property_name;
  char *op;
} ExprGet;

typedef struct ExprAssign {
  Expr *lhs;
  Expr *rhs;
  char *op;
} ExprAssign;

typedef struct ExprLogic {
  Expr *lhs;
  Expr *rhs;
  char *op;
} ExprLogic;

typedef struct ExprStruct {
  char *name;
  DynArray_Expr initializers;
} ExprStruct;

typedef struct ExprStructInit {
  Expr *property;
  Expr *value;
} ExprStructInit;

typedef struct ExprArray {
  DynArray_Expr elements;
} ExprArray;

typedef struct ExprSubscript {
  Expr *expr;
  Expr *index;
} ExprSubscript;

typedef DynArray(ExprStructInit) DynArray_ExprStructInit;

typedef struct Expr {
  ExprKind kind;
  union {
    ExprLit expr_lit;
    ExprVar expr_var;
    ExprUnary expr_una;
    ExprBin expr_bin;
    ExprCall expr_call;
    ExprGet expr_get;
    ExprAssign expr_ass;
    ExprLogic expr_log;
    ExprStruct expr_struct;
    ExprStructInit expr_s_init;
    ExprArray expr_array;
    ExprSubscript expr_subscript;
  } as;
} Expr;

#define TO_EXPR_LIT(exp) ((exp).as.expr_lit)
#define TO_EXPR_VAR(exp) ((exp).as.expr_var)
#define TO_EXPR_UNA(exp) ((exp).as.expr_una)
#define TO_EXPR_BIN(exp) ((exp).as.expr_bin)
#define TO_EXPR_CALL(exp) ((exp).as.expr_call)
#define TO_EXPR_GET(exp) ((exp).as.expr_get)
#define TO_EXPR_ASS(exp) ((exp).as.expr_ass)
#define TO_EXPR_LOG(exp) ((exp).as.expr_log)
#define TO_EXPR_STRUCT(exp) ((exp).as.expr_struct)
#define TO_EXPR_S_INIT(exp) ((exp).as.expr_s_init)
#define TO_EXPR_ARRAY(exp) ((exp).as.expr_array)
#define TO_EXPR_SUBSCRIPT(exp) ((exp).as.expr_subscript)

#define AS_EXPR_LIT(exp) ((Expr){.kind = EXPR_LIT, .as.expr_lit = (exp)})
#define AS_EXPR_VAR(exp) ((Expr){.kind = EXPR_VAR, .as.expr_var = (exp)})
#define AS_EXPR_UNA(exp) ((Expr){.kind = EXPR_UNA, .as.expr_una = (exp)})
#define AS_EXPR_BIN(exp) ((Expr){.kind = EXPR_BIN, .as.expr_bin = (exp)})
#define AS_EXPR_CALL(exp) ((Expr){.kind = EXPR_CALL, .as.expr_call = (exp)})
#define AS_EXPR_GET(exp) ((Expr){.kind = EXPR_GET, .as.expr_get = (exp)})
#define AS_EXPR_ASS(exp) ((Expr){.kind = EXPR_ASS, .as.expr_ass = (exp)})
#define AS_EXPR_LOG(exp) ((Expr){.kind = EXPR_LOG, .as.expr_log = (exp)})
#define AS_EXPR_STRUCT(exp)                                                    \
  ((Expr){.kind = EXPR_STRUCT, .as.expr_struct = (exp)})
#define AS_EXPR_S_INIT(exp)                                                    \
  ((Expr){.kind = EXPR_S_INIT, .as.expr_s_init = (exp)})
#define AS_EXPR_ARRAY(exp) ((Expr){.kind = EXPR_ARRAY, .as.expr_array = (exp)})
#define AS_EXPR_SUBSCRIPT(exp)                                                 \
  ((Expr){.kind = EXPR_SUBSCRIPT, .as.expr_subscript = (exp)})

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
  STMT_RETURN,
  STMT_STRUCT,
  STMT_IMPL,
} __attribute__((__packed__)) StmtKind;

typedef struct Stmt Stmt;

typedef DynArray(Stmt) DynArray_Stmt;

typedef struct {
  char *name;
  Expr initializer;
} StmtLet;

typedef struct {
  Expr exp;
} StmtPrint;

typedef struct {
  Expr exp;
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
  Expr condition;
  Stmt *then_branch;
  Stmt *else_branch;
} StmtIf;

typedef struct {
  Expr condition;
  Stmt *body;
} StmtWhile;

typedef struct {
  Expr initializer;
  Expr condition;
  Expr advancement;
  Stmt *body;
} StmtFor;

typedef struct {
  Expr returnval;
} StmtRet;

typedef struct {
  char *name;
  DynArray_char_ptr properties;
} StmtStruct;

typedef struct {
  char *name;
  DynArray_Stmt methods;
} StmtImpl;

typedef struct {
  char dummy;
} StmtBreak;

typedef struct {
  char dummy;
} StmtContinue;

typedef struct Stmt {
  StmtKind kind;
  union {
    StmtPrint stmt_print;
    StmtLet stmt_let;
    StmtExpr stmt_expr;
    StmtBlock stmt_block;
    StmtFn stmt_fn;
    StmtIf stmt_if;
    StmtWhile stmt_while;
    StmtFor stmt_for;
    StmtBreak stmt_break;
    StmtContinue stmt_continue;
    StmtRet stmt_return;
    StmtStruct stmt_struct;
    StmtImpl stmt_impl;
  } as;
} Stmt;

#define TO_STMT_PRINT(stmt) ((stmt).as.stmt_print)
#define TO_STMT_LET(stmt) ((stmt).as.stmt_let)
#define TO_STMT_EXPR(stmt) ((stmt).as.stmt_expr)
#define TO_STMT_BLOCK(stmt) ((stmt)->as.stmt_block)
#define TO_STMT_FN(stmt) ((stmt).as.stmt_fn)
#define TO_STMT_IF(stmt) ((stmt).as.stmt_if)
#define TO_STMT_WHILE(stmt) ((stmt).as.stmt_while)
#define TO_STMT_FOR(stmt) ((stmt).as.stmt_for)
#define TO_STMT_RETURN(stmt) ((stmt).as.stmt_return)
#define TO_STMT_STRUCT(stmt) ((stmt).as.stmt_struct)
#define TO_STMT_IMPL(stmt) ((stmt).as.stmt_impl)

#define AS_STMT_PRINT(stmt)                                                    \
  ((Stmt){.kind = STMT_PRINT, .as.stmt_print = (stmt)})
#define AS_STMT_LET(stmt) ((Stmt){.kind = STMT_LET, .as.stmt_let = (stmt)})
#define AS_STMT_EXPR(stmt) ((Stmt){.kind = STMT_EXPR, .as.stmt_expr = (stmt)})
#define AS_STMT_BLOCK(stmt)                                                    \
  ((Stmt){.kind = STMT_BLOCK, .as.stmt_block = (stmt)})
#define AS_STMT_FN(stmt) ((Stmt){.kind = STMT_FN, .as.stmt_fn = (stmt)})
#define AS_STMT_IF(stmt) ((Stmt){.kind = STMT_IF, .as.stmt_if = (stmt)})
#define AS_STMT_WHILE(stmt)                                                    \
  ((Stmt){.kind = STMT_WHILE, .as.stmt_while = (stmt)})
#define AS_STMT_FOR(stmt) ((Stmt){.kind = STMT_FOR, .as.stmt_for = (stmt)})
#define AS_STMT_BREAK(stmt)                                                    \
  ((Stmt){.kind = STMT_BREAK, .as.stmt_break = (stmt)})
#define AS_STMT_CONTINUE(stmt)                                                 \
  ((Stmt){.kind = STMT_CONTINUE, .as.stmt_continue = (stmt)})
#define AS_STMT_RETURN(stmt)                                                   \
  ((Stmt){.kind = STMT_RETURN, .as.stmt_return = (stmt)})
#define AS_STMT_STRUCT(stmt)                                                   \
  ((Stmt){.kind = STMT_STRUCT, .as.stmt_struct = (stmt)})
#define AS_STMT_IMPL(stmt) ((Stmt){.kind = STMT_IMPL, .as.stmt_impl = (stmt)})

typedef struct {
  Token current;
  Token previous;
  size_t depth;
} Parser;

DynArray_Stmt parse(Parser *parser, Tokenizer *tokenizer);
void free_stmt(Stmt stmt);
void init_parser(Parser *parser);

#endif
