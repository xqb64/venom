#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dynarray.h"
#include "parser.h"
#include "tokenizer.h"
#include "util.h"

void free_stmt(Statement stmt) {
    switch (stmt.kind) {
        case STMT_PRINT: {
            free_expression(stmt.as.stmt_print.exp);
            break;
        }
        case STMT_LET: {
            free_expression(stmt.as.stmt_let.initializer);
            free(stmt.as.stmt_let.name);
            break;
        }
        case STMT_BLOCK: {
            for (size_t i = 0; i < stmt.as.stmt_block.stmts.count; i++) {
                free_stmt(stmt.as.stmt_block.stmts.data[i]);
            }
            dynarray_free(&stmt.as.stmt_block.stmts);
            break;
        }
        case STMT_IF: {
            free_expression(stmt.as.stmt_if.condition);

            free_stmt(*stmt.as.stmt_if.then_branch);
            free(stmt.as.stmt_if.then_branch);

            if (stmt.as.stmt_if.else_branch != NULL) {
                free_stmt(*stmt.as.stmt_if.else_branch);
                free(stmt.as.stmt_if.else_branch);
            }

            break;
        }
        case STMT_WHILE: {
            free_expression(stmt.as.stmt_while.condition);
            free_stmt(*stmt.as.stmt_while.body);
            free(stmt.as.stmt_while.body);
            break;
        }
        case STMT_RETURN: {
            free_expression(stmt.as.stmt_return.returnval);
            break;
        }
        case STMT_EXPR: {
            free_expression(stmt.as.stmt_expr.exp);
            break;
        }
        case STMT_FN: {
            free(stmt.as.stmt_fn.name);
            for (size_t i = 0; i < stmt.as.stmt_fn.parameters.count; i++) {
                free(stmt.as.stmt_fn.parameters.data[i]);
            }
            dynarray_free(&stmt.as.stmt_fn.parameters);
            for (size_t i = 0; i < stmt.as.stmt_fn.stmts.count; i++) {
                free_stmt(stmt.as.stmt_fn.stmts.data[i]);
            }
            dynarray_free(&stmt.as.stmt_fn.stmts);
            break;
        }
        case STMT_STRUCT: {
            free(stmt.as.stmt_struct.name);
            for (size_t i = 0; i < stmt.as.stmt_struct.properties.count; i++) {
                free(stmt.as.stmt_struct.properties.data[i]);
            }
            dynarray_free(&stmt.as.stmt_struct.properties);
        }
        default: break;
    }
}

void free_expression(Expression e) {
    switch (e.kind) {
        case EXP_LITERAL: {
            break;
        }
        case EXP_VARIABLE: {
            free(e.as.expr_variable.name);
            break;
        }
        case EXP_STRING: {
            free(e.as.expr_string.str);
            break;
        }
        case EXP_UNARY: {
            free_expression(*e.as.expr_unary.exp);
            free(e.as.expr_unary.exp);
            break;
        }
        case EXP_BINARY: {
            free_expression(*e.as.expr_binary.lhs);
            free_expression(*e.as.expr_binary.rhs);
            free(e.as.expr_binary.lhs);
            free(e.as.expr_binary.rhs);
            break;
        }
        case EXP_ASSIGN: {
            free_expression(*e.as.expr_assign.lhs);
            free_expression(*e.as.expr_assign.rhs);
            free(e.as.expr_assign.lhs);
            free(e.as.expr_assign.rhs);
            break;
        }
        case EXP_LOGICAL: {
            free_expression(*e.as.expr_logical.lhs);
            free_expression(*e.as.expr_logical.rhs);
            free(e.as.expr_logical.lhs);
            free(e.as.expr_logical.rhs);
            break;
        }
        case EXP_CALL: {
            free(e.as.expr_call.var.name);
            for (size_t i = 0; i < e.as.expr_call.arguments.count; i++) {
                free_expression(e.as.expr_call.arguments.data[i]);
            }
            dynarray_free(&e.as.expr_call.arguments);
            break;
        }
        case EXP_STRUCT: {
            free(e.as.expr_struct.name);
            for (size_t i = 0; i < e.as.expr_struct.initializers.count; i++) {
                free_expression(e.as.expr_struct.initializers.data[i]);
            }
            dynarray_free(&e.as.expr_struct.initializers);
            break;
        }
        case EXP_STRUCT_INIT: {
            free_expression(*e.as.expr_struct_init.property);
            free_expression(*e.as.expr_struct_init.value);
            free(e.as.expr_struct_init.value);
            free(e.as.expr_struct_init.property);
            break;
        }
        case EXPR_GET: {
            free_expression(*e.as.expr_get.exp);
            free(e.as.expr_get.property_name);
            free(e.as.expr_get.exp);
            break;
        }
    }
}

static void parse_error(Parser *parser, char *message) {
    parser->had_error = true;
    fprintf(stderr, "parse error: %s\n", message);
}

static Token advance(Parser *parser, Tokenizer *tokenizer) {
    parser->previous = parser->current;
    parser->current = get_token(tokenizer);

#ifdef venom_debug
    print_token(parser->current);
#endif

    return parser->previous;
}

static bool check(Parser *parser, TokenType type) {
    return parser->current.type == type;
}

static bool match(Parser *parser, Tokenizer *tokenizer, int size, ...) {
    va_list ap;
    va_start(ap, size);
    for (int i = 0; i < size; i++) {
        TokenType type = va_arg(ap, TokenType);
        if (check(parser, type)) {
            advance(parser, tokenizer);
            va_end(ap);
            return true;
        }
    }
    va_end(ap);
    return false;
}

static Token consume(Parser *parser, Tokenizer *tokenizer, TokenType type, char *message) {
    if (check(parser, type)) return advance(parser, tokenizer);
    else {
        parse_error(parser, message);
        assert(0);
    };
}

static Expression number(Parser *parser) {
    LiteralExpression e = (LiteralExpression){
        .dval = strtod(parser->previous.start, NULL),
        .specval = NULL,
    };
    return AS_EXPR_LITERAL(e);
}

static Expression string(Parser *parser) {
    StringExpression e = (StringExpression){
        .str = own_string_n(
            parser->previous.start,
            parser->previous.length - 1
        ),
    };
    return AS_EXPR_STRING(e);
}

static Expression variable(Parser *parser) {
    VariableExpression e = (VariableExpression){
        .name = own_string_n(
            parser->previous.start,
            parser->previous.length
        ),
    };
    return AS_EXPR_VARIABLE(e);
}

static Expression special_literal(char *specval) {
    LiteralExpression e = (LiteralExpression){
        .specval = specval,
    };
    return AS_EXPR_LITERAL(e);
}

static Expression primary();
static Expression expression(Parser *parser, Tokenizer *tokenizer);
static Statement statement(Parser *parser, Tokenizer *tokenizer);

static char *operator(Token token) {
    switch (token.type) {
        case TOKEN_PLUS: return "+";
        case TOKEN_MINUS: return "-";
        case TOKEN_STAR: return "*";
        case TOKEN_SLASH: return "/";
        case TOKEN_MOD: return "%%";
        case TOKEN_DOUBLE_EQUAL: return "==";
        case TOKEN_BANG_EQUAL: return "!=";
        case TOKEN_GREATER: return ">";
        case TOKEN_GREATER_EQUAL: return ">=";
        case TOKEN_LESS: return "<";
        case TOKEN_LESS_EQUAL: return "<=";
        default: assert(0);
     }
}

static Expression finish_call(Parser *parser, Tokenizer *tokenizer, Expression exp) {
    Expression_DynArray arguments = {0};
    if (!check(parser, TOKEN_RIGHT_PAREN)) {
        do {
            dynarray_insert(&arguments, expression(parser, tokenizer));
        } while (match(parser, tokenizer, 1, TOKEN_COMMA));
    }
    consume(
        parser, tokenizer,
        TOKEN_RIGHT_PAREN,
        "Expected ')' after expression."
    );
    CallExpression e = (CallExpression){
        .arguments = arguments,
        .var = exp.as.expr_variable,
    };
    return AS_EXPR_CALL(e);
}

static Expression call(Parser *parser, Tokenizer *tokenizer) {
    Expression expr = primary(parser, tokenizer);
    for (;;) {
        if (match(parser, tokenizer, 1, TOKEN_LEFT_PAREN)) {
            expr = finish_call(parser, tokenizer, expr);
        } else if (match(parser, tokenizer, 1, TOKEN_DOT)) {
            Token property_name = consume(parser, tokenizer, TOKEN_IDENTIFIER, "Expected property name after '.'");

            GetExpression get_expr = (GetExpression){
                .exp = ALLOC(expr),
                .property_name = own_string_n(
                    property_name.start,
                    property_name.length
                ),
            };

            expr = AS_EXPR_GET(get_expr);
        } else {
            break;
        }
    }
    return expr;
}

static Expression unary(Parser *parser, Tokenizer *tokenizer) {
    if (match(parser, tokenizer, 1, TOKEN_MINUS)) {
        Expression *right = malloc(sizeof(Expression));
        *right = unary(parser, tokenizer);
        UnaryExpression e = (UnaryExpression){ .exp = right };
        return AS_EXPR_UNARY(e);
    }
    return call(parser, tokenizer);
}

static Expression factor(Parser *parser, Tokenizer *tokenizer) {
    Expression expr = unary(parser, tokenizer);
    while (match(parser, tokenizer, 3, TOKEN_STAR, TOKEN_SLASH, TOKEN_MOD)) {
        char *op = operator(parser->previous);
        Expression right = unary(parser, tokenizer);
        BinaryExpression binexp = (BinaryExpression){
            .lhs = ALLOC(expr),
            .rhs = ALLOC(right),
            .operator = op,
        };
        expr = AS_EXPR_BINARY(binexp);
    }
    return expr;
}

static Expression term(Parser *parser, Tokenizer *tokenizer) {
    Expression expr = factor(parser, tokenizer);
    while (match(parser, tokenizer, 2, TOKEN_PLUS, TOKEN_MINUS)) {
        char *op = operator(parser->previous);
        Expression right = factor(parser, tokenizer);
        BinaryExpression binexp = (BinaryExpression){
            .lhs = ALLOC(expr),
            .rhs = ALLOC(right),
            .operator = op,
        };
        expr = AS_EXPR_BINARY(binexp);
    }
    return expr;
}

static Expression comparison(Parser *parser, Tokenizer *tokenizer) {
    Expression expr = term(parser, tokenizer);
    while (match(parser, tokenizer, 4,
        TOKEN_GREATER, TOKEN_LESS,
        TOKEN_GREATER_EQUAL, TOKEN_LESS_EQUAL
    )) {
        char *op = operator(parser->previous);
        Expression right = term(parser, tokenizer);
        BinaryExpression binexp = (BinaryExpression){
            .lhs = ALLOC(expr),
            .rhs = ALLOC(right),
            .operator = op,
        };
        expr = AS_EXPR_BINARY(binexp);
    }
    return expr;
}

static Expression equality(Parser *parser, Tokenizer *tokenizer) {
    Expression expr = comparison(parser, tokenizer);
    while (match(parser, tokenizer, 2, TOKEN_DOUBLE_EQUAL, TOKEN_BANG_EQUAL)) {
        char *op = operator(parser->previous);
        Expression right = comparison(parser, tokenizer);
        BinaryExpression binexp = (BinaryExpression){
            .lhs = ALLOC(expr),
            .rhs = ALLOC(right),
            .operator = op,
        };
        expr = AS_EXPR_BINARY(binexp);
    }
    return expr;
}

static Expression and_(Parser *parser, Tokenizer *tokenizer) {
    Expression expr = equality(parser, tokenizer);
    if (match(parser, tokenizer, 1, TOKEN_DOUBLE_AMPERSAND)) {
        char *op = own_string_n(parser->previous.start, parser->previous.length);
        Expression right = equality(parser, tokenizer);
        LogicalExpression logexp = (LogicalExpression){
            .lhs = ALLOC(expr),
            .rhs = ALLOC(right),
            .operator = op,
        };
        expr = AS_EXPR_LOGICAL(logexp);
    }
    return expr;
}

static Expression or_(Parser *parser, Tokenizer *tokenizer) {
    Expression expr = and_(parser, tokenizer);
    if (match(parser, tokenizer, 1, TOKEN_DOUBLE_PIPE)) {
        char *op = own_string_n(parser->previous.start, parser->previous.length);
        Expression right = and_(parser, tokenizer);
        LogicalExpression logexp = (LogicalExpression){
            .lhs = ALLOC(expr),
            .rhs = ALLOC(right),
            .operator = op,
        };
        expr = AS_EXPR_LOGICAL(logexp);
    }
    return expr;
}

static Expression assignment(Parser *parser, Tokenizer *tokenizer) {
    Expression expr = or_(parser, tokenizer);
    if (match(parser, tokenizer, 1, TOKEN_EQUAL)) {
        Expression right = assignment(parser, tokenizer);
        AssignExpression assignexp = (AssignExpression){
            .lhs = ALLOC(expr),
            .rhs = ALLOC(right),
        };
        expr = AS_EXPR_ASSIGN(assignexp);
    }
    return expr;
}

static Expression expression(Parser *parser, Tokenizer *tokenizer) {
    return assignment(parser, tokenizer);
}

static Expression grouping(Parser *parser, Tokenizer *tokenizer) {
    Expression exp = expression(parser, tokenizer);
    consume(
        parser, tokenizer,
        TOKEN_RIGHT_PAREN,
        "Unmatched closing parentheses."
    );
    return exp;
}

static Statement_DynArray block(Parser *parser, Tokenizer *tokenizer) {
    Statement_DynArray stmts = {0};
    while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
        dynarray_insert(&stmts, statement(parser, tokenizer));
    }
    consume(
        parser, tokenizer,
        TOKEN_RIGHT_BRACE,
        "Expected '}' at the end of the block."
    );
    return stmts;
}

static Expression struct_initializer(Parser *parser, Tokenizer *tokenizer) {
    char *name = own_string_n(parser->previous.start, parser->previous.length);
    consume(
        parser, tokenizer,
        TOKEN_LEFT_BRACE,
        "Expected '{' after struct name."
    );
    Expression_DynArray initializers = {0};
    do {
        Expression property = expression(parser, tokenizer);
        consume(
            parser, tokenizer,
            TOKEN_COLON,
            "Expected ':' after property name."
        );
        Expression value = primary(parser, tokenizer);

        StructInitializerExpression structinitexp = (StructInitializerExpression){
            .property = ALLOC(property),
            .value = ALLOC(value),
        };

        dynarray_insert(&initializers, AS_EXPR_STRUCT_INIT(structinitexp));
    } while (match(parser, tokenizer, 1, TOKEN_COMMA));
    consume(
        parser, tokenizer,
        TOKEN_RIGHT_BRACE,
        "Expected '}' after struct initialization."
    );
    StructExpression structexp = (StructExpression){
        .initializers = initializers,
        .name = name,
    };
    return AS_EXPR_STRUCT(structexp);
}

static Expression primary(Parser *parser, Tokenizer *tokenizer) {
    if (match(parser, tokenizer, 1, TOKEN_NUMBER)) {
        return number(parser);
    } else if (match(parser, tokenizer, 1, TOKEN_STRING)) {
        return string(parser);
    } else if (match(parser, tokenizer, 1, TOKEN_IDENTIFIER)) {
        if (check(parser, TOKEN_LEFT_BRACE)) {
            return struct_initializer(parser, tokenizer);
        } 
        return variable(parser);
    } else if (match(parser, tokenizer, 1, TOKEN_LEFT_PAREN)) {
        return grouping(parser, tokenizer);
    } else if (match(parser, tokenizer, 1, TOKEN_TRUE)) {
        return special_literal("true");
    } else if (match(parser, tokenizer, 1, TOKEN_FALSE)) {
        return special_literal("false");
    } else if (match(parser, tokenizer, 1, TOKEN_NULL)) {
        return special_literal("null");
    } else {
        assert(0);
    }
}
#define venom_debug
#ifdef venom_debug
static void print_expression(Expression e) {
    printf("(");
    switch (e.kind) {
        case EXP_LITERAL: {
            printf("%f", TO_EXPR_LITERAL(e).dval);
            break;
        }
        case EXP_VARIABLE: {
            printf("%s", TO_EXPR_VARIABLE(e).name);
            break;
        }
        case EXP_UNARY: {
            printf("-");
            print_expression(*TO_EXPR_UNARY(e).exp);
            break;
        }
        case EXP_BINARY: {
            print_expression(*TO_EXPR_BINARY(e).lhs);
            printf(" %s ", TO_EXPR_BINARY(e).operator);
            print_expression(*TO_EXPR_BINARY(e).rhs);
            break;
        }
        case EXP_CALL: {
            printf("%s", TO_EXPR_CALL(e).var.name);
            for (size_t i = 0; i < TO_EXPR_CALL(e).arguments.count; i++) {
                print_expression(TO_EXPR_CALL(e).arguments.data[i]);
            }
            printf("()");
            break;
        }
        case EXP_STRING: {
            printf("%s", TO_EXPR_STRING(e).str);
            break;
        }
        case EXP_STRUCT: {
            printf("%s", TO_EXPR_STRUCT(e).name);
            for (size_t i = 0; i < TO_EXPR_STRUCT(e).initializers.count; i++) {
                print_expression(TO_EXPR_STRUCT(e).initializers.data[i]);
            }
            break;
        }
        case EXP_STRUCT_INIT: {
            // print_expression(e.as.expr_struct_init->property);
            // printf(": ");
            // print_expression(e.as.expr_struct_init->value);
            break;
        }
        case EXPR_GET: {
            break;
        }
        default: assert(0);
    }
    printf(")");
}
#endif

static Statement print_statement(Parser *parser, Tokenizer *tokenizer) {
    Expression exp = expression(parser, tokenizer);
#ifdef venom_debug
    print_expression(exp);
    printf("\n");
#endif
    consume(
        parser, tokenizer,
        TOKEN_SEMICOLON,
        "Expected semicolon at the end of the expression."
    );
    PrintStatement stmt = { .exp = exp };
    return AS_STMT_PRINT(stmt);
}

static Statement let_statement(Parser *parser, Tokenizer *tokenizer) {
    Token identifier = consume(
        parser, tokenizer,
        TOKEN_IDENTIFIER,
        "Expected identifier after 'let'."
    );
    char *name = own_string_n(identifier.start, identifier.length);
    Expression initializer;
    if (match(parser, tokenizer, 1, TOKEN_EQUAL)) {
        initializer = expression(parser, tokenizer);
    }
#ifdef venom_debug
    print_expression(initializer);
    printf("\n");
#endif
    consume(
        parser, tokenizer,
        TOKEN_SEMICOLON,
        "Expected semicolon at the end of the statement."
    );
    LetStatement stmt = {
        .name = name,
        .initializer = initializer
    };
    return AS_STMT_LET(stmt);
}

static Statement expression_statement(Parser *parser, Tokenizer *tokenizer) {
    Expression expr = expression(parser, tokenizer);
    consume(
        parser, tokenizer,
        TOKEN_SEMICOLON,
        "Expected ';' after expression"
    );
    ExpressionStatement stmt = { .exp = expr };
    return AS_STMT_EXPR(stmt);
}

static Statement if_statement(Parser *parser, Tokenizer *tokenizer) {
    consume(
        parser, tokenizer,
        TOKEN_LEFT_PAREN,
        "Expected '(' after if."
    );
    Expression condition = expression(parser, tokenizer);
    consume(
        parser, tokenizer,
        TOKEN_RIGHT_PAREN,
        "Expected ')' after the condition."
    );

    Statement *then_branch = malloc(sizeof(Statement));
    Statement *else_branch = NULL;

    *then_branch = statement(parser, tokenizer);

    if (match(parser, tokenizer, 1, TOKEN_ELSE)) {
        else_branch = malloc(sizeof(Statement));
        *else_branch = statement(parser, tokenizer);
    }

    IfStatement stmt = {
        .then_branch = then_branch,
        .else_branch = else_branch,
        .condition = condition,
    };
    return AS_STMT_IF(stmt);
}

static Statement while_statement(Parser *parser, Tokenizer *tokenizer) {
    consume(
        parser, tokenizer,
        TOKEN_LEFT_PAREN,
        "Expected '(' after while."
    );
    Expression condition = expression(parser, tokenizer);
    consume(
        parser, tokenizer,
        TOKEN_RIGHT_PAREN,
        "Expected ')' after condition."
    );
    WhileStatement stmt = {
        .condition = condition,
        .body = malloc(sizeof(Statement)),
    };
    *stmt.body = statement(parser, tokenizer);
    return AS_STMT_WHILE(stmt);
 }

static Statement function_statement(Parser *parser, Tokenizer *tokenizer) {
    Token name = consume(
        parser, tokenizer,
        TOKEN_IDENTIFIER,
        "Expected identifier after 'fn'."
    );
    consume(
        parser, tokenizer,
        TOKEN_LEFT_PAREN,
        "Expected '(' after identifier."
    );
    String_DynArray parameters = {0};
    if (!check(parser, TOKEN_RIGHT_PAREN)) {
        do {
            Token parameter = consume(
                parser, tokenizer,
                TOKEN_IDENTIFIER,
                "Expected parameter name."
            );
            dynarray_insert(
                &parameters,
                own_string_n(parameter.start, parameter.length)
            );
        } while (match(parser, tokenizer, 1, TOKEN_COMMA));
    }
    consume(
        parser, tokenizer,
        TOKEN_RIGHT_PAREN,
        "Expected ')' after the parameter list."
    );
    consume(
        parser, tokenizer,
        TOKEN_LEFT_BRACE,
        "Expected '{' after the ')'."
    );
    FunctionStatement stmt = {
        .name = own_string_n(name.start, name.length),
        .stmts = block(parser, tokenizer),
        .parameters = parameters,
    };
    return AS_STMT_FN(stmt);
}

static Statement return_statement(Parser *parser, Tokenizer *tokenizer) {
    Expression expr = expression(parser, tokenizer);
    consume(
        parser, tokenizer,
        TOKEN_SEMICOLON,
        "Expected ';' after return."
    );
    ReturnStatement stmt = {
        .returnval = expr,
    };
    return AS_STMT_RETURN(stmt);
}

static Statement struct_statement(Parser *parser, Tokenizer *tokenizer) {
    Token name = consume(
        parser, tokenizer,
        TOKEN_IDENTIFIER,
        "Expected identifier after 'struct'."
    ); 
    consume(
        parser, tokenizer,
        TOKEN_LEFT_BRACE,
        "Expected '{' after 'struct'."
    );
    String_DynArray properties = {0};
    do {
        Token property = consume(
            parser, tokenizer,
            TOKEN_IDENTIFIER,
            "Expected property name."
        );
        consume(
            parser, tokenizer,
            TOKEN_SEMICOLON,
            "Expected semicolon after property."
        );
        dynarray_insert(
            &properties,
            own_string_n(property.start, property.length)
        );
    } while (!match(parser, tokenizer, 1, TOKEN_RIGHT_BRACE));
    StructStatement stmt = {
        .name = own_string_n(name.start, name.length),
        .properties = properties,
    };
    return AS_STMT_STRUCT(stmt);
}

static Statement statement(Parser *parser, Tokenizer *tokenizer) {
    if (match(parser, tokenizer, 1, TOKEN_PRINT)) {
        return print_statement(parser, tokenizer);
    } else if (match(parser, tokenizer, 1, TOKEN_LET)) {
        return let_statement(parser, tokenizer);
    } else if (match(parser, tokenizer, 1, TOKEN_LEFT_BRACE)) {
        BlockStatement stmt = { .stmts = block(parser, tokenizer) };
        return AS_STMT_BLOCK(stmt);
    } else if (match(parser, tokenizer, 1, TOKEN_IF)) {
        return if_statement(parser, tokenizer);
    } else if (match(parser, tokenizer, 1, TOKEN_WHILE)) {
        return while_statement(parser, tokenizer);
    } else if (match(parser, tokenizer, 1, TOKEN_FN)) {
        return function_statement(parser, tokenizer);
    } else if (match(parser, tokenizer, 1, TOKEN_RETURN)) {
        return return_statement(parser, tokenizer);
    } else if (match(parser, tokenizer, 1, TOKEN_STRUCT)) {
        return struct_statement(parser, tokenizer);
    } else {
        return expression_statement(parser, tokenizer);
    }
}

static Statement parse_statement(Parser *parser, Tokenizer *tokenizer) {
    return statement(parser, tokenizer);
}

void parse(Parser *parser, Tokenizer *tokenizer, Statement_DynArray *stmts) {
    parser->had_error = false;
    advance(parser, tokenizer);
    while (parser->current.type != TOKEN_EOF) {
        dynarray_insert(stmts, parse_statement(parser, tokenizer));
    }
}
