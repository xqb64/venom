#ifndef venom_parser_h
#define venom_parser_h

#include <stdbool.h>
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
    EXPR_GET,
    EXP_ASSIGN,
    EXP_LOGICAL,
    EXP_STRUCT,
    EXP_STRUCT_INIT,
} ExpressionKind;

typedef struct LiteralExpression LiteralExpression;
typedef struct VariableExpression VariableExpression;
typedef struct StringExpression StringExpression;
typedef struct UnaryExpression UnaryExpression;
typedef struct BinaryExpression BinaryExpression;
typedef struct CallExpression CallExpression;
typedef struct GetExpression GetExpression;
typedef struct AssignExpression AssignExpression;
typedef struct LogicalExpression LogicalExpression;
typedef struct StructExpression StructExpression;
typedef struct StructInitializerExpression StructInitializerExpression;
typedef struct Expression Expression;

typedef DynArray(Expression) Expression_DynArray;

typedef struct Expression {
    ExpressionKind kind;
    union {
        LiteralExpression *expr_literal;
        VariableExpression *expr_variable;
        StringExpression *expr_string;
        UnaryExpression *expr_unary;
        BinaryExpression *expr_binary;
        CallExpression *expr_call;
        GetExpression *expr_get;
        AssignExpression *expr_assign;
        LogicalExpression *expr_logical;
        StructExpression *expr_struct;
        StructInitializerExpression *expr_struct_init;
    } as;
} Expression;

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
    Expression lhs;
    Expression rhs;    
    char *operator;
} BinaryExpression;

typedef struct CallExpression {
    VariableExpression *var;
    Expression_DynArray arguments;
} CallExpression;

typedef struct GetExpression {
    Expression exp;
    char *property_name;
} GetExpression;

typedef struct AssignExpression {
    Expression lhs;
    Expression rhs;
} AssignExpression;

typedef struct LogicalExpression {
    Expression lhs;
    Expression rhs;    
    char *operator;
} LogicalExpression;

typedef DynArray(StructInitializerExpression) StructInitializerExpressionDynArray;

typedef struct StructExpression {
    char *name;
    Expression_DynArray initializers;
} StructExpression;

typedef struct StructInitializerExpression {
    Expression property;
    Expression value;
} StructInitializerExpression;

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

typedef enum {
    STMT_LET,
    STMT_EXPR,
    STMT_PRINT,
    STMT_BLOCK,
    STMT_IF,
    STMT_WHILE,
    STMT_FN,
    STMT_RETURN,
    STMT_STRUCT,
} StatementKind;

typedef struct Statement Statement;

typedef DynArray(Statement) Statement_DynArray;

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
    Statement_DynArray stmts;
} BlockStatement;

typedef struct {
    char *name;
    Statement_DynArray stmts;
    String_DynArray parameters;
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
    String_DynArray properties;
} StructStatement;

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
        ReturnStatement stmt_return;
        StructStatement stmt_struct;
    } as;
} Statement;

#define TO_STMT_PRINT(stmt) ((stmt).as.stmt_print)
#define TO_STMT_LET(stmt) ((stmt).as.stmt_let)
#define TO_STMT_EXPR(stmt) ((stmt).as.stmt_expr)
#define TO_STMT_BLOCK(stmt) ((stmt).as.stmt_block)
#define TO_STMT_FN(stmt) ((stmt).as.stmt_fn)
#define TO_STMT_IF(stmt) ((stmt).as.stmt_if)
#define TO_STMT_WHILE(stmt) ((stmt).as.stmt_while)
#define TO_STMT_RETURN(stmt) ((stmt).as.stmt_return)
#define TO_STMT_STRUCT(stmt) ((stmt).as.stmt_struct)

typedef struct {
    Token current;
    Token previous;
    bool had_error;
} Parser;

void parse(Parser *parser, Tokenizer *tokenizer, Statement_DynArray *stmts);
void free_expression(Expression e);
void free_stmt(Statement stmt);

#endif