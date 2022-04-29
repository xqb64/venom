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

#define venom_debug

void free_ast(Expression e) {
    if (e.kind == LITERAL) return;
    else if (e.kind == VARIABLE) free(e.name);
    else if (e.kind == UNARY) free_ast(*e.data.exp);
    else {
        free_ast(e.data.binexp->lhs);
        free_ast(e.data.binexp->rhs);
        free(e.data.binexp);
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
    for (int i = 0; i < size; ++i) {
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
    else parse_error(parser, message);
}

static Expression number(Parser *parser) {
    Expression expr;
    expr.kind = LITERAL;
    expr.data.dval = strtod(parser->previous.start, NULL);
    return expr;
}

static Expression variable(Parser *parser) {
    Expression expr = { .kind = VARIABLE, .name = malloc(255) };
    snprintf(expr.name, parser->previous.length + 1, "%s", parser->previous.start);
    return expr;
}

static Expression primary();

static char *operator(Token token) {
    switch (token.type) {
        case TOKEN_PLUS:   return "+";
        case TOKEN_MINUS:  return "-";
        case TOKEN_STAR:   return "*";
        case TOKEN_SLASH:  return "/";
        default:
            assert(0);
     }
}

static Expression unary(Parser *parser, Tokenizer *tokenizer) {
    if (match(parser, tokenizer, 1, TOKEN_MINUS)) {
        Expression *right = malloc(sizeof(Expression));
        *right = unary(parser, tokenizer);
        Expression result = { .kind = UNARY, .data.exp = right };
        return result;
    }
    return primary(parser, tokenizer);
}

static Expression factor(Parser *parser, Tokenizer *tokenizer) {
    Expression expr = unary(parser, tokenizer);
    while (match(parser, tokenizer, 2, TOKEN_STAR, TOKEN_SLASH)) {
        char *op = operator(parser->previous);
        Expression right = unary(parser, tokenizer);
        Expression result = { 
            .kind = BINARY, 
            .data.binexp = malloc(sizeof(BinaryExpression)), 
            .operator = op, 
        };
        result.data.binexp->lhs = expr;
        result.data.binexp->rhs = right;
        expr = result;
    }
    return expr;
}

static Expression term(Parser *parser, Tokenizer *tokenizer) {
    Expression expr = factor(parser, tokenizer);
    while (match(parser, tokenizer, 2, TOKEN_PLUS, TOKEN_MINUS)) {
        char *op = operator(parser->previous);
        Expression right = factor(parser, tokenizer);
        Expression result = { 
            .kind = BINARY,
            .data.binexp = malloc(sizeof(BinaryExpression)),
            .operator = op,
        };
        result.data.binexp->lhs = expr;
        result.data.binexp->rhs = right;
        expr = result;
    }
    return expr;
}

static Expression expression(Parser *parser, Tokenizer *tokenizer) {
    return term(parser, tokenizer);
}

static Expression grouping(Parser *parser, Tokenizer *tokenizer) {
    Expression exp = expression(parser, tokenizer);
    consume(parser, tokenizer, TOKEN_RIGHT_PAREN, "Unmatched closing parentheses.");
    return exp;
}

static Expression primary(Parser *parser, Tokenizer *tokenizer) {
    if (match(parser, tokenizer, 1, TOKEN_NUMBER)) return number(parser);
    else if (match(parser, tokenizer, 1, TOKEN_IDENTIFIER)) return variable(parser);
    else if (match(parser, tokenizer, 1, TOKEN_LEFT_PAREN)) return grouping(parser, tokenizer);
}

#ifdef venom_debug
static void print_expression(Expression e) {
    printf("(");
    
    if (e.kind == LITERAL) { 
        printf("%f", e.data.dval);
    } else if (e.kind == VARIABLE) {
        printf("%s", e.name);
    } else if (e.kind == UNARY) {
        printf("-");
        print_expression(*e.data.exp);
    } else {
        print_expression(e.data.binexp->lhs);

        switch (e.kind) {
            case BINARY: printf(" %s ", e.operator); break;
            default:
                break;
        }

        print_expression(e.data.binexp->rhs);
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

    Statement stmt = { .kind = STATEMENT_PRINT, .exp = exp, .name = NULL };
    consume(parser, tokenizer, TOKEN_SEMICOLON, "Expected semicolon at the end of the expression.");
    return stmt;
}

static Statement variable_declaration(Parser *parser, Tokenizer *tokenizer) {
    Token identifier = consume(parser, tokenizer, TOKEN_IDENTIFIER, "Expected identifier after 'let'.");
    char *name = own_string_n(identifier.start, identifier.length);
  
    Expression initializer;
    if (match(parser, tokenizer, 1, TOKEN_EQUALS)) {
        initializer = expression(parser, tokenizer);
    }

#ifdef venom_debug
    print_expression(initializer);
    printf("\n");
#endif

    Statement stmt = { .kind = STATEMENT_LET, .name = name, .exp = initializer };
    consume(parser, tokenizer, TOKEN_SEMICOLON, "Expected semicolon at the end of the statement.");
    return stmt;
}

static Statement statement(Parser *parser, Tokenizer *tokenizer) {
    if (match(parser, tokenizer, 1, TOKEN_PRINT)) {
        return print_statement(parser, tokenizer);
    } else if (match(parser, tokenizer, 1, TOKEN_LET)) {
        return variable_declaration(parser, tokenizer);
    }
}

Statement parse_statement(Parser *parser, Tokenizer *tokenizer) {
    return statement(parser, tokenizer);
}

void parse(Parser *parser, Tokenizer *tokenizer, Statement_DynArray *stmts) {
    parser->had_error = false;
    advance(parser, tokenizer);
    while (parser->current.type != TOKEN_EOF) {
        dynarray_insert(stmts, parse_statement(parser, tokenizer));
    }
}
