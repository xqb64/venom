#ifndef venom_parser_h
#define venom_parser_h

#include <stdlib.h>
#include "dynarray.h"
#include "tokenizer.h"
#include "object.h"

typedef enum {
    EXP_LITERAL,
    EXP_VARIABLE,
    EXP_STRING,
    EXP_UNARY,
    EXP_BINARY,
    EXP_CALL,
    EXP_GET,
    EXP_ASSIGN,
    EXP_LOGICAL,
    EXP_STRUCT,
    EXP_STRUCT_INIT,
} __attribute__ ((__packed__)) ExpressionKind;

typedef struct Expression Expression;

typedef DynArray(Expression) DynArray_Expression;

typedef struct LiteralExpression {
    double dval;
    char *specval;
} LiteralExpression;

typedef struct VariableExpression {
    char *name;
} VariableExpression;

typedef struct StringExpression {
    char *str;
} StringExpression;

typedef struct UnaryExpression {
    Expression *exp;
} UnaryExpression;

typedef struct BinaryExpression {
    Expression *lhs;
    Expression *rhs;
    char *operator;
} BinaryExpression;

typedef struct CallExpression {
    VariableExpression var;
    DynArray_Expression arguments;
} CallExpression;

typedef struct GetExpression {
    Expression *exp;
    char *property_name;
} GetExpression;

typedef struct AssignExpression {
    Expression *lhs;
    Expression *rhs;
} AssignExpression;

typedef struct LogicalExpression {
    Expression *lhs;
    Expression *rhs;
    char *operator;
} LogicalExpression;

typedef struct StructExpression {
    char *name;
    DynArray_Expression initializers;
} StructExpression;

typedef struct StructInitializerExpression {
    Expression *property;
    Expression *value;
} StructInitializerExpression;

typedef DynArray(StructInitializerExpression) DynArray_StructInitializerExpression;

typedef struct Expression {
    ExpressionKind kind;
    union {
        LiteralExpression expr_literal;
        VariableExpression expr_variable;
        StringExpression expr_string;
        UnaryExpression expr_unary;
        BinaryExpression expr_binary;
        CallExpression expr_call;
        GetExpression expr_get;
        AssignExpression expr_assign;
        LogicalExpression expr_logical;
        StructExpression expr_struct;
        StructInitializerExpression expr_struct_init;
    } as;
} Expression;

#define TO_EXPR_LITERAL(exp) ((exp).as.expr_literal)
#define TO_EXPR_STRING(exp) ((exp).as.expr_string)
#define TO_EXPR_VARIABLE(exp) ((exp).as.expr_variable)
#define TO_EXPR_UNARY(exp) ((exp).as.expr_unary)
#define TO_EXPR_BINARY(exp) ((exp).as.expr_binary)
#define TO_EXPR_CALL(exp) ((exp).as.expr_call)
#define TO_EXPR_GET(exp) ((exp).as.expr_get)
#define TO_EXPR_ASSIGN(exp) ((exp).as.expr_assign)
#define TO_EXPR_LOGICAL(exp) ((exp).as.expr_logical)
#define TO_EXPR_STRUCT(exp) ((exp).as.expr_struct)
#define TO_EXPR_STRUCT_INIT(exp) ((exp).as.expr_struct_init)

#define AS_EXPR_LITERAL(exp) ((Expression){ .kind = EXP_LITERAL, .as.expr_literal = (exp) })
#define AS_EXPR_STRING(exp) ((Expression){ .kind = EXP_STRING, .as.expr_string = (exp) })
#define AS_EXPR_VARIABLE(exp) ((Expression){ .kind = EXP_VARIABLE, .as.expr_variable = (exp) })
#define AS_EXPR_UNARY(exp) ((Expression){ .kind = EXP_UNARY, .as.expr_unary = (exp) })
#define AS_EXPR_BINARY(exp) ((Expression){ .kind = EXP_BINARY, .as.expr_binary = (exp) })
#define AS_EXPR_CALL(exp) ((Expression){ .kind = EXP_CALL, .as.expr_call = (exp) })
#define AS_EXPR_GET(exp) ((Expression){ .kind = EXP_GET, .as.expr_get = (exp) })
#define AS_EXPR_ASSIGN(exp) ((Expression){ .kind = EXP_ASSIGN, .as.expr_assign = (exp) })
#define AS_EXPR_LOGICAL(exp) ((Expression){ .kind = EXP_LOGICAL, .as.expr_logical = (exp) })
#define AS_EXPR_STRUCT(exp) ((Expression){ .kind = EXP_STRUCT, .as.expr_struct = (exp) })
#define AS_EXPR_STRUCT_INIT(exp) ((Expression){ .kind = EXP_STRUCT_INIT, .as.expr_struct_init = (exp) })

typedef enum {
    STMT_LET,
    STMT_EXPR,
    STMT_PRINT,
    STMT_BLOCK,
    STMT_IF,
    STMT_WHILE,
    STMT_BREAK,
    STMT_CONTINUE,
    STMT_FN,
    STMT_RETURN,
    STMT_STRUCT,
} __attribute__ ((__packed__)) StatementKind;

typedef struct Statement Statement;

typedef DynArray(Statement) DynArray_Statement;

typedef struct {
    char *name;
    Expression initializer;
} LetStatement;

typedef struct {
    Expression exp;
} PrintStatement;

typedef struct {
    Expression exp;
} ExpressionStatement;

typedef struct {
    DynArray_Statement stmts;
    size_t depth;
} BlockStatement;

typedef struct {
    DynArray_char_ptr parameters;
    char *name;
    Statement *body;
} FunctionStatement;

typedef struct {
    Expression condition;
    Statement *then_branch;
    Statement *else_branch;
} IfStatement;

typedef struct {
    Expression condition;
    Statement *body;
} WhileStatement;

typedef struct {
    Expression returnval;
} ReturnStatement;

typedef struct {
    char *name;
    DynArray_char_ptr properties;
} StructStatement;

typedef struct {} BreakStatement;
typedef struct {} ContinueStatement;

typedef struct Statement {
    StatementKind kind;
    union {
        PrintStatement stmt_print;
        LetStatement stmt_let;
        ExpressionStatement stmt_expr;
        BlockStatement stmt_block;
        FunctionStatement stmt_fn;
        IfStatement stmt_if;
        WhileStatement stmt_while;
        BreakStatement stmt_break;
        ContinueStatement stmt_continue;
        ReturnStatement stmt_return;
        StructStatement stmt_struct;
    } as;
} Statement;

#define TO_STMT_PRINT(stmt) ((stmt).as.stmt_print)
#define TO_STMT_LET(stmt) ((stmt).as.stmt_let)
#define TO_STMT_EXPR(stmt) ((stmt).as.stmt_expr)
#define TO_STMT_BLOCK(stmt) ((stmt)->as.stmt_block)
#define TO_STMT_FN(stmt) ((stmt).as.stmt_fn)
#define TO_STMT_IF(stmt) ((stmt).as.stmt_if)
#define TO_STMT_WHILE(stmt) ((stmt).as.stmt_while)
#define TO_STMT_RETURN(stmt) ((stmt).as.stmt_return)
#define TO_STMT_STRUCT(stmt) ((stmt).as.stmt_struct)

#define AS_STMT_PRINT(stmt) ((Statement){ .kind = STMT_PRINT, .as.stmt_print = (stmt) })
#define AS_STMT_LET(stmt) ((Statement){ .kind = STMT_LET, .as.stmt_let = (stmt) })
#define AS_STMT_EXPR(stmt) ((Statement){ .kind = STMT_EXPR, .as.stmt_expr = (stmt) })
#define AS_STMT_BLOCK(stmt) ((Statement){ .kind = STMT_BLOCK, .as.stmt_block = (stmt) })
#define AS_STMT_FN(stmt) ((Statement){ .kind = STMT_FN, .as.stmt_fn = (stmt) })
#define AS_STMT_IF(stmt) ((Statement){ .kind = STMT_IF, .as.stmt_if = (stmt) })
#define AS_STMT_WHILE(stmt) ((Statement){ .kind = STMT_WHILE, .as.stmt_while = (stmt) })
#define AS_STMT_BREAK(stmt) ((Statement){ .kind = STMT_BREAK, .as.stmt_break = (stmt) })
#define AS_STMT_CONTINUE(stmt) ((Statement){ .kind = STMT_CONTINUE, .as.stmt_continue = (stmt) })
#define AS_STMT_RETURN(stmt) ((Statement){ .kind = STMT_RETURN, .as.stmt_return = (stmt) })
#define AS_STMT_STRUCT(stmt) ((Statement){ .kind = STMT_STRUCT, .as.stmt_struct = (stmt) })

typedef struct {
    Token current;
    Token previous;
    size_t depth;
} Parser;

DynArray_Statement parse(Parser *parser, Tokenizer *tokenizer);
void free_stmt(Statement stmt);
void init_parser(Parser *parser);

#endif