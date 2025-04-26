#include "parser.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dynarray.h"
#include "tokenizer.h"
#include "util.h"

void init_parser(Parser *parser, DynArray_Token *tokens)
{
    memset(parser, 0, sizeof(Parser));
    parser->tokens = tokens;
}

void free_parser(Parser *parser)
{
    dynarray_free(parser->tokens);
}

static void parse_error(Parser *parser, char *message)
{
    fprintf(stderr, "parser: %s\n", message);
}

static Token pop_front(Parser *parser)
{
    if (parser->idx < parser->tokens->count)
        return parser->tokens->data[parser->idx++];
    return (Token) {.type = TOKEN_EOF};
}

static Token next_token(Parser *parser)
{
    return pop_front(parser);
}

static Token advance(Parser *parser)
{
    parser->previous = parser->current;
    parser->current = next_token(parser);
    return parser->previous;
}

static bool check(Parser *parser, TokenType type)
{
    return parser->current.type == type;
}

static bool match(Parser *parser, int size, ...)
{
    va_list ap;
    va_start(ap, size);
    for (int i = 0; i < size; i++)
    {
        TokenType type = va_arg(ap, TokenType);
        if (check(parser, type))
        {
            advance(parser);
            va_end(ap);
            return true;
        }
    }
    va_end(ap);
    return false;
}

static Token consume(Parser *parser, TokenType type, char *message)
{
    if (check(parser, type))
        return advance(parser);
    else
    {
        parse_error(parser, message);
        assert(0);
    };
}

static Expr boolean(Parser *parser)
{
    bool b;
    switch (parser->previous.type)
    {
        case TOKEN_TRUE: {
            b = true;
            break;
        }
        case TOKEN_FALSE: {
            b = false;
            break;
        }
        default:
            assert(0);
    }
    ExprLit e = {
        .kind = LIT_BOOL,
        .as.bval = b,
    };
    return AS_EXPR_LIT(e);
}

static Expr null(Parser *parser)
{
    return AS_EXPR_LIT((ExprLit) {.kind = LIT_NULL});
}

static Expr number(Parser *parser)
{
    ExprLit e = {
        .kind = LIT_NUM,
        .as.dval = strtod(parser->previous.start, NULL),
    };
    return AS_EXPR_LIT(e);
}

static Expr string(Parser *parser)
{
    ExprLit e = {
        .kind = LIT_STR,
        .as.sval = own_string_n(parser->previous.start, parser->previous.length - 1),
    };
    return AS_EXPR_LIT(e);
}

static Expr variable(Parser *parser)
{
    ExprVar e = {
        .name = own_string_n(parser->previous.start, parser->previous.length),
    };
    return AS_EXPR_VAR(e);
}

static Expr literal(Parser *parser)
{
    switch (parser->previous.type)
    {
        case TOKEN_NUMBER:
            return number(parser);
        case TOKEN_STRING:
            return string(parser);
        case TOKEN_TRUE:
        case TOKEN_FALSE:
            return boolean(parser);
        case TOKEN_NULL:
            return null(parser);
        default:
            assert(0);
    }
}

static Expr primary();
static Expr expression(Parser *parser);
static Stmt statement(Parser *parser);

static char *operator(Token token)
{
    switch (token.type)
    {
        case TOKEN_EQUAL:
            return "=";
        case TOKEN_PLUS:
            return "+";
        case TOKEN_PLUS_EQUAL:
            return "+=";
        case TOKEN_MINUS:
            return "-";
        case TOKEN_MINUS_EQUAL:
            return "-=";
        case TOKEN_STAR:
            return "*";
        case TOKEN_STAR_EQUAL:
            return "*=";
        case TOKEN_SLASH:
            return "/";
        case TOKEN_SLASH_EQUAL:
            return "/=";
        case TOKEN_AMPERSAND:
            return "&";
        case TOKEN_AMPERSAND_EQUAL:
            return "&=";
        case TOKEN_PIPE:
            return "|";
        case TOKEN_PIPE_EQUAL:
            return "|=";
        case TOKEN_CARET:
            return "^";
        case TOKEN_CARET_EQUAL:
            return "^=";
        case TOKEN_MOD:
            return "%%";
        case TOKEN_MOD_EQUAL:
            return "%%=";
        case TOKEN_DOUBLE_EQUAL:
            return "==";
        case TOKEN_BANG_EQUAL:
            return "!=";
        case TOKEN_GREATER:
            return ">";
        case TOKEN_GREATER_EQUAL:
            return ">=";
        case TOKEN_LESS:
            return "<";
        case TOKEN_LESS_EQUAL:
            return "<=";
        case TOKEN_PLUSPLUS:
            return "++";
        case TOKEN_GREATER_GREATER:
            return ">>";
        case TOKEN_GREATER_GREATER_EQUAL:
            return ">>=";
        case TOKEN_LESS_LESS:
            return "<<";
        case TOKEN_LESS_LESS_EQUAL:
            return "<<=";
        default:
            assert(0);
    }
}

static Expr finish_call(Parser *parser, Expr callee)
{
    DynArray_Expr arguments = {0};
    if (!check(parser, TOKEN_RIGHT_PAREN))
    {
        do
        {
            dynarray_insert(&arguments, expression(parser));
        } while (match(parser, 1, TOKEN_COMMA));
    }
    consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after expression.");
    ExprCall e = {
        .callee = ALLOC(callee),
        .arguments = arguments,
    };
    return AS_EXPR_CALL(e);
}

static Expr call(Parser *parser)
{
    Expr expr = primary(parser);
    for (;;)
    {
        if (match(parser, 1, TOKEN_LEFT_PAREN))
        {
            expr = finish_call(parser, expr);
        }
        else if (match(parser, 2, TOKEN_DOT, TOKEN_ARROW))
        {
            char *op = own_string_n(parser->previous.start, parser->previous.length);
            Token property_name =
                consume(parser, TOKEN_IDENTIFIER, "Expected property name after '.'");

            ExprGet get_expr = {
                .exp = ALLOC(expr),
                .property_name = own_string_n(property_name.start, property_name.length),
                .op = op,
            };

            expr = AS_EXPR_GET(get_expr);
        }
        else if (match(parser, 1, TOKEN_LEFT_BRACKET))
        {
            Expr index = expression(parser);
            consume(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after index.");
            ExprSubscript subscript_expr = {
                .expr = ALLOC(expr),
                .index = ALLOC(index),
            };
            expr = AS_EXPR_SUBSCRIPT(subscript_expr);
        }
        else
        {
            break;
        }
    }
    return expr;
}

static Expr unary(Parser *parser)
{
    if (match(parser, 5, TOKEN_MINUS, TOKEN_AMPERSAND, TOKEN_STAR, TOKEN_BANG, TOKEN_TILDE))
    {
        char *op = own_string_n(parser->previous.start, parser->previous.length);
        Expr right = unary(parser);
        ExprUnary e = {.exp = ALLOC(right), .op = op};
        return AS_EXPR_UNA(e);
    }
    return call(parser);
}

static Expr factor(Parser *parser)
{
    Expr expr = unary(parser);
    while (match(parser, 3, TOKEN_STAR, TOKEN_SLASH, TOKEN_MOD))
    {
        char *op = operator(parser->previous);
        Expr right = unary(parser);
        ExprBin binexp = {
            .lhs = ALLOC(expr),
            .rhs = ALLOC(right),
            .op = op,
        };
        expr = AS_EXPR_BIN(binexp);
    }
    return expr;
}

static Expr term(Parser *parser)
{
    Expr expr = factor(parser);
    while (match(parser, 3, TOKEN_PLUS, TOKEN_MINUS, TOKEN_PLUSPLUS))
    {
        char *op = operator(parser->previous);
        Expr right = factor(parser);
        ExprBin binexp = {
            .lhs = ALLOC(expr),
            .rhs = ALLOC(right),
            .op = op,
        };
        expr = AS_EXPR_BIN(binexp);
    }
    return expr;
}

static Expr bitwise_shift(Parser *parser)
{
    Expr expr = term(parser);
    while (match(parser, 2, TOKEN_GREATER_GREATER, TOKEN_LESS_LESS))
    {
        char *op = operator(parser->previous);
        Expr right = term(parser);
        ExprBin binexp = {
            .lhs = ALLOC(expr),
            .rhs = ALLOC(right),
            .op = op,
        };
        expr = AS_EXPR_BIN(binexp);
    }
    return expr;
}

static Expr comparison(Parser *parser)
{
    Expr expr = bitwise_shift(parser);
    while (match(parser, 4, TOKEN_GREATER, TOKEN_LESS, TOKEN_GREATER_EQUAL, TOKEN_LESS_EQUAL))
    {
        char *op = operator(parser->previous);
        Expr right = bitwise_shift(parser);
        ExprBin binexp = {
            .lhs = ALLOC(expr),
            .rhs = ALLOC(right),
            .op = op,
        };
        expr = AS_EXPR_BIN(binexp);
    }
    return expr;
}

static Expr equality(Parser *parser)
{
    Expr expr = comparison(parser);
    while (match(parser, 2, TOKEN_DOUBLE_EQUAL, TOKEN_BANG_EQUAL))
    {
        char *op = operator(parser->previous);
        Expr right = comparison(parser);
        ExprBin binexp = {
            .lhs = ALLOC(expr),
            .rhs = ALLOC(right),
            .op = op,
        };
        expr = AS_EXPR_BIN(binexp);
    }
    return expr;
}

static Expr bitwise_and(Parser *parser)
{
    Expr expr = equality(parser);
    while (match(parser, 1, TOKEN_AMPERSAND))
    {
        char *op = operator(parser->previous);
        Expr right = equality(parser);
        ExprBin binexp = {
            .lhs = ALLOC(expr),
            .rhs = ALLOC(right),
            .op = op,
        };
        expr = AS_EXPR_BIN(binexp);
    }
    return expr;
}

static Expr bitwise_xor(Parser *parser)
{
    Expr expr = bitwise_and(parser);
    while (match(parser, 1, TOKEN_CARET))
    {
        char *op = operator(parser->previous);
        Expr right = bitwise_and(parser);
        ExprBin binexp = {
            .lhs = ALLOC(expr),
            .rhs = ALLOC(right),
            .op = op,
        };
        expr = AS_EXPR_BIN(binexp);
    }
    return expr;
}

static Expr bitwise_or(Parser *parser)
{
    Expr expr = bitwise_xor(parser);
    while (match(parser, 1, TOKEN_PIPE))
    {
        char *op = operator(parser->previous);
        Expr right = bitwise_xor(parser);
        ExprBin binexp = {
            .lhs = ALLOC(expr),
            .rhs = ALLOC(right),
            .op = op,
        };
        expr = AS_EXPR_BIN(binexp);
    }
    return expr;
}

static Expr and_(Parser *parser)
{
    Expr expr = bitwise_or(parser);
    while (match(parser, 1, TOKEN_DOUBLE_AMPERSAND))
    {
        char *op = own_string_n(parser->previous.start, parser->previous.length);
        Expr right = bitwise_or(parser);
        ExprLogic logexp = {
            .lhs = ALLOC(expr),
            .rhs = ALLOC(right),
            .op = op,
        };
        expr = AS_EXPR_LOG(logexp);
    }
    return expr;
}

static Expr or_(Parser *parser)
{
    Expr expr = and_(parser);
    while (match(parser, 1, TOKEN_DOUBLE_PIPE))
    {
        char *op = own_string_n(parser->previous.start, parser->previous.length);
        Expr right = and_(parser);
        ExprLogic logexp = {
            .lhs = ALLOC(expr),
            .rhs = ALLOC(right),
            .op = op,
        };
        expr = AS_EXPR_LOG(logexp);
    }
    return expr;
}

static Expr assignment(Parser *parser)
{
    Expr expr = or_(parser);
    if (match(parser, 11, TOKEN_EQUAL, TOKEN_PLUS_EQUAL, TOKEN_MINUS_EQUAL, TOKEN_STAR_EQUAL,
              TOKEN_SLASH_EQUAL, TOKEN_MOD_EQUAL, TOKEN_AMPERSAND_EQUAL, TOKEN_PIPE_EQUAL,
              TOKEN_CARET_EQUAL, TOKEN_GREATER_GREATER_EQUAL, TOKEN_LESS_LESS_EQUAL))
    {
        char *op = operator(parser->previous);
        Expr right = or_(parser);
        ExprAssign assignexp = {
            .lhs = ALLOC(expr),
            .rhs = ALLOC(right),
            .op = op,
        };
        expr = AS_EXPR_ASS(assignexp);
    }
    return expr;
}

static Expr expression(Parser *parser)
{
    return assignment(parser);
}

static Expr grouping(Parser *parser)
{
    Expr exp = expression(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Unmatched closing parentheses.");
    return exp;
}

static Stmt block(Parser *parser)
{
    parser->depth++;
    DynArray_Stmt stmts = {0};
    while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF))
    {
        dynarray_insert(&stmts, statement(parser));
    }
    consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' at the end of the block.");
    StmtBlock body = {
        .depth = parser->depth,
        .stmts = stmts,
    };
    parser->depth--;
    return AS_STMT_BLOCK(body);
}

static Expr struct_initializer(Parser *parser)
{
    char *name = own_string_n(parser->previous.start, parser->previous.length);
    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' after struct name.");
    DynArray_Expr initializers = {0};
    do
    {
        Expr property = expression(parser);
        consume(parser, TOKEN_COLON, "Expected ':' after property name.");
        Expr value = expression(parser);
        ExprStructInit structinitexp = {
            .property = ALLOC(property),
            .value = ALLOC(value),
        };
        dynarray_insert(&initializers, AS_EXPR_S_INIT(structinitexp));
    } while (match(parser, 1, TOKEN_COMMA));
    consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after struct initialization.");
    ExprStruct structexp = {
        .initializers = initializers,
        .name = name,
    };
    return AS_EXPR_STRUCT(structexp);
}

static Expr array_initializer(Parser *parser)
{
    DynArray_Expr initializers = {0};
    do
    {
        dynarray_insert(&initializers, expression(parser));
    } while (match(parser, 1, TOKEN_COMMA));
    consume(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after array initialization.");
    ExprArray arrayexp = {
        .elements = initializers,
    };
    return AS_EXPR_ARRAY(arrayexp);
}

static Expr primary(Parser *parser)
{
    if (match(parser, 1, TOKEN_IDENTIFIER))
    {
        if (check(parser, TOKEN_LEFT_BRACE))
        {
            return struct_initializer(parser);
        }
        else
        {
            return variable(parser);
        }
    }
    else if (match(parser, 1, TOKEN_LEFT_PAREN))
    {
        return grouping(parser);
    }
    else if (match(parser, 5, TOKEN_TRUE, TOKEN_FALSE, TOKEN_NULL, TOKEN_NUMBER, TOKEN_STRING))
    {
        return literal(parser);
    }
    else if (match(parser, 1, TOKEN_LEFT_BRACKET))
    {
        return array_initializer(parser);
    }
    else
    {
        assert(0);
    }
}

static Stmt print_statement(Parser *parser)
{
    Expr exp = expression(parser);
    consume(parser, TOKEN_SEMICOLON, "Expected semicolon at the end of the expression.");
    StmtPrint stmt = {.exp = exp};
    return AS_STMT_PRINT(stmt);
}

static Stmt let_statement(Parser *parser)
{
    Token identifier = consume(parser, TOKEN_IDENTIFIER, "Expected identifier after 'let'.");
    char *name = own_string_n(identifier.start, identifier.length);

    consume(parser, TOKEN_EQUAL, "Expected '=' after variable name.");

    Expr initializer = expression(parser);

    consume(parser, TOKEN_SEMICOLON, "Expected semicolon at the end of the statement.");
    StmtLet stmt = {.name = name, .initializer = initializer};
    return AS_STMT_LET(stmt);
}

static Stmt expression_statement(Parser *parser)
{
    Expr expr = expression(parser);
    consume(parser, TOKEN_SEMICOLON, "Expected ';' after expression");
    StmtExpr stmt = {.exp = expr};
    return AS_STMT_EXPR(stmt);
}

static Stmt if_statement(Parser *parser)
{
    consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after if.");
    Expr condition = expression(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after the condition.");

    Stmt *then_branch = malloc(sizeof(Stmt));
    Stmt *else_branch = NULL;

    *then_branch = statement(parser);

    if (match(parser, 1, TOKEN_ELSE))
    {
        else_branch = malloc(sizeof(Stmt));
        *else_branch = statement(parser);
    }

    StmtIf stmt = {
        .then_branch = then_branch,
        .else_branch = else_branch,
        .condition = condition,
    };
    return AS_STMT_IF(stmt);
}

static Stmt while_statement(Parser *parser)
{
    consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after while.");

    Expr condition = expression(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after condition.");

    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' after the while condition.");

    Stmt body = block(parser);

    StmtWhile stmt = {
        .condition = condition,
        .body = ALLOC(body),
    };
    return AS_STMT_WHILE(stmt);
}

static Stmt for_statement(Parser *parser)
{
    consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after for.");

    consume(parser, TOKEN_LET, "Expected 'let' in initializer");

    Expr initializer = expression(parser);
    consume(parser, TOKEN_SEMICOLON, "Expected ';' after initializer.");

    Expr condition = expression(parser);
    consume(parser, TOKEN_SEMICOLON, "Expected ';' after condition.");

    Expr advancement = expression(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after advancement.");

    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' after ')'.");

    Stmt body = block(parser);

    StmtFor stmt = {
        .initializer = initializer,
        .condition = condition,
        .advancement = advancement,
        .body = ALLOC(body),
    };

    return AS_STMT_FOR(stmt);
}

static Stmt function_statement(Parser *parser)
{
    Token name = consume(parser, TOKEN_IDENTIFIER, "Expected identifier after 'fn'.");
    consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after identifier.");
    DynArray_char_ptr parameters = {0};
    if (!check(parser, TOKEN_RIGHT_PAREN))
    {
        do
        {
            Token parameter = consume(parser, TOKEN_IDENTIFIER, "Expected parameter name.");
            dynarray_insert(&parameters, own_string_n(parameter.start, parameter.length));
        } while (match(parser, 1, TOKEN_COMMA));
    }
    consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after the parameter list.");
    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' after the ')'.");
    Stmt body = block(parser);
    StmtFn stmt = {
        .name = own_string_n(name.start, name.length),
        .body = ALLOC(body),
        .parameters = parameters,
    };
    return AS_STMT_FN(stmt);
}

static Stmt decorator_statement(Parser *parser)
{
    Token decorator = consume(parser, TOKEN_IDENTIFIER, "Expected identifier after '@'.");
    consume(parser, TOKEN_FN, "Expected 'fn' after '@deco'.");
    Stmt fn = function_statement(parser);
    StmtDeco stmt = {
        .name = own_string_n(decorator.start, decorator.length),
        .fn = ALLOC(fn),
    };
    return AS_STMT_DECO(stmt);
}

static Stmt return_statement(Parser *parser)
{
    Expr expr = expression(parser);
    consume(parser, TOKEN_SEMICOLON, "Expected ';' after return.");
    StmtRet stmt = {
        .returnval = expr,
    };
    return AS_STMT_RETURN(stmt);
}

static Stmt break_statement(Parser *parser)
{
    consume(parser, TOKEN_SEMICOLON, "Expected ';' after break.");
    StmtBreak stmt = {0};
    return AS_STMT_BREAK(stmt);
}

static Stmt continue_statement(Parser *parser)
{
    consume(parser, TOKEN_SEMICOLON, "Expected ';' after continue.");
    StmtContinue stmt = {0};
    return AS_STMT_CONTINUE(stmt);
}

static Stmt struct_statement(Parser *parser)
{
    Token name = consume(parser, TOKEN_IDENTIFIER, "Expected identifier after 'struct'.");
    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' after 'struct'.");
    DynArray_char_ptr properties = {0};
    do
    {
        Token property = consume(parser, TOKEN_IDENTIFIER, "Expected property name.");
        consume(parser, TOKEN_SEMICOLON, "Expected semicolon after property.");
        dynarray_insert(&properties, own_string_n(property.start, property.length));
    } while (!match(parser, 1, TOKEN_RIGHT_BRACE));
    StmtStruct stmt = {
        .name = own_string_n(name.start, name.length),
        .properties = properties,
    };
    return AS_STMT_STRUCT(stmt);
}

static Stmt impl_statement(Parser *parser)
{
    Token name = consume(parser, TOKEN_IDENTIFIER, "Expected identifier after 'impl'.");
    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' after identifier.");
    DynArray_Stmt methods = {0};
    while (!match(parser, 1, TOKEN_RIGHT_BRACE))
    {
        dynarray_insert(&methods, statement(parser));
    }
    StmtImpl stmt = {
        .name = own_string_n(name.start, name.length),
        .methods = methods,
    };
    return AS_STMT_IMPL(stmt);
}

static Stmt use_statement(Parser *parser)
{
    Token path = consume(parser, TOKEN_STRING, "Module path should be a string");
    consume(parser, TOKEN_SEMICOLON, "expected semicolon at the end of use statement");
    StmtUse stmt = {.path = own_string_n(path.start, path.length - 1)};
    return AS_STMT_USE(stmt);
}

static Stmt yield_statement(Parser *parser)
{
    Expr expr = expression(parser);
    consume(parser, TOKEN_SEMICOLON, "Expected ';' after yield statement.");
    StmtYield stmt = {.exp = expr};
    return AS_STMT_YIELD(stmt);
}

static Stmt assert_statement(Parser *parser)
{
    Expr expr = expression(parser);
    consume(parser, TOKEN_SEMICOLON, "Expected ';' after assert statement.");
    StmtAssert stmt = {.exp = expr};
    return AS_STMT_ASSERT(stmt);
}

static Stmt statement(Parser *parser)
{
    if (match(parser, 1, TOKEN_PRINT))
    {
        return print_statement(parser);
    }
    else if (match(parser, 1, TOKEN_LET))
    {
        return let_statement(parser);
    }
    else if (match(parser, 1, TOKEN_LEFT_BRACE))
    {
        return block(parser);
    }
    else if (match(parser, 1, TOKEN_IF))
    {
        return if_statement(parser);
    }
    else if (match(parser, 1, TOKEN_WHILE))
    {
        return while_statement(parser);
    }
    else if (match(parser, 1, TOKEN_FOR))
    {
        return for_statement(parser);
    }
    else if (match(parser, 1, TOKEN_BREAK))
    {
        return break_statement(parser);
    }
    else if (match(parser, 1, TOKEN_CONTINUE))
    {
        return continue_statement(parser);
    }
    else if (match(parser, 1, TOKEN_FN))
    {
        return function_statement(parser);
    }
    else if (match(parser, 1, TOKEN_RETURN))
    {
        return return_statement(parser);
    }
    else if (match(parser, 1, TOKEN_STRUCT))
    {
        return struct_statement(parser);
    }
    else if (match(parser, 1, TOKEN_USE))
    {
        return use_statement(parser);
    }
    else if (match(parser, 1, TOKEN_AT))
    {
        return decorator_statement(parser);
    }
    else if (match(parser, 1, TOKEN_IMPL))
    {
        return impl_statement(parser);
    }
    else if (match(parser, 1, TOKEN_YIELD))
    {
        return yield_statement(parser);
    }
    else if (match(parser, 1, TOKEN_ASSERT))
    {
        return assert_statement(parser);
    }
    else
    {
        return expression_statement(parser);
    }
}

static void free_expression(Expr e)
{
    switch (e.kind)
    {
        case EXPR_LIT: {
            ExprLit litexpr = TO_EXPR_LIT(e);
            if (litexpr.kind == LIT_STR)
            {
                free(litexpr.as.sval);
            }
            break;
        }
        case EXPR_VAR: {
            ExprVar varexpr = TO_EXPR_VAR(e);
            free(varexpr.name);
            break;
        }
        case EXPR_UNA: {
            ExprUnary unaryexpr = TO_EXPR_UNA(e);
            free_expression(*unaryexpr.exp);
            free(unaryexpr.exp);
            free(unaryexpr.op);
            break;
        }
        case EXPR_BIN: {
            ExprBin binexpr = TO_EXPR_BIN(e);
            free_expression(*binexpr.lhs);
            free_expression(*binexpr.rhs);
            free(binexpr.lhs);
            free(binexpr.rhs);
            break;
        }
        case EXPR_ASS: {
            ExprAssign assignexpr = TO_EXPR_ASS(e);
            free_expression(*assignexpr.lhs);
            free_expression(*assignexpr.rhs);
            free(assignexpr.lhs);
            free(assignexpr.rhs);
            break;
        }
        case EXPR_LOG: {
            ExprLogic logicexpr = TO_EXPR_LOG(e);
            free_expression(*logicexpr.lhs);
            free_expression(*logicexpr.rhs);
            free(logicexpr.lhs);
            free(logicexpr.rhs);
            free(logicexpr.op);
            break;
        }
        case EXPR_CALL: {
            ExprCall callexpr = TO_EXPR_CALL(e);
            free_expression(*callexpr.callee);
            free(callexpr.callee);
            for (size_t i = 0; i < callexpr.arguments.count; i++)
            {
                free_expression(callexpr.arguments.data[i]);
            }
            dynarray_free(&callexpr.arguments);
            break;
        }
        case EXPR_STRUCT: {
            ExprStruct structexpr = TO_EXPR_STRUCT(e);
            free(structexpr.name);
            for (size_t i = 0; i < structexpr.initializers.count; i++)
            {
                free_expression(structexpr.initializers.data[i]);
            }
            dynarray_free(&structexpr.initializers);
            break;
        }
        case EXPR_S_INIT: {
            ExprStructInit structinitexpr = TO_EXPR_S_INIT(e);
            free_expression(*structinitexpr.property);
            free_expression(*structinitexpr.value);
            free(structinitexpr.value);
            free(structinitexpr.property);
            break;
        }
        case EXPR_GET: {
            ExprGet getexpr = TO_EXPR_GET(e);
            free_expression(*getexpr.exp);
            free(getexpr.property_name);
            free(getexpr.exp);
            free(getexpr.op);
            break;
        }
        case EXPR_ARRAY: {
            ExprArray arrayexpr = TO_EXPR_ARRAY(e);
            for (size_t i = 0; i < arrayexpr.elements.count; i++)
            {
                free_expression(arrayexpr.elements.data[i]);
            }
            dynarray_free(&arrayexpr.elements);
            break;
        }
        case EXPR_SUBSCRIPT: {
            ExprSubscript subscriptexpr = TO_EXPR_SUBSCRIPT(e);
            free_expression(*subscriptexpr.expr);
            free_expression(*subscriptexpr.index);
            free(subscriptexpr.expr);
            free(subscriptexpr.index);
            break;
        }
        default:
            break;
    }
}

void free_stmt(Stmt stmt)
{
    switch (stmt.kind)
    {
        case STMT_PRINT: {
            free_expression(TO_STMT_PRINT(stmt).exp);
            break;
        }
        case STMT_IMPL: {
            free(TO_STMT_IMPL(stmt).name);
            for (size_t i = 0; i < TO_STMT_IMPL(stmt).methods.count; i++)
            {
                free_stmt(TO_STMT_IMPL(stmt).methods.data[i]);
            }
            dynarray_free(&TO_STMT_IMPL(stmt).methods);
            break;
        }
        case STMT_LET: {
            free_expression(TO_STMT_LET(stmt).initializer);
            free(stmt.as.stmt_let.name);
            break;
        }
        case STMT_BLOCK: {
            for (size_t i = 0; i < TO_STMT_BLOCK(&stmt).stmts.count; i++)
            {
                free_stmt(TO_STMT_BLOCK(&stmt).stmts.data[i]);
            }
            dynarray_free(&TO_STMT_BLOCK(&stmt).stmts);
            break;
        }
        case STMT_IF: {
            free_expression(TO_STMT_IF(stmt).condition);

            free_stmt(*TO_STMT_IF(stmt).then_branch);
            free(TO_STMT_IF(stmt).then_branch);

            if (TO_STMT_IF(stmt).else_branch != NULL)
            {
                free_stmt(*TO_STMT_IF(stmt).else_branch);
                free(TO_STMT_IF(stmt).else_branch);
            }

            break;
        }
        case STMT_WHILE: {
            free(TO_STMT_WHILE(stmt).label);
            free_expression(TO_STMT_WHILE(stmt).condition);
            for (size_t i = 0; i < TO_STMT_BLOCK(TO_STMT_WHILE(stmt).body).stmts.count; i++)
            {
                free_stmt(TO_STMT_BLOCK(TO_STMT_WHILE(stmt).body).stmts.data[i]);
            }
            dynarray_free(&TO_STMT_BLOCK(TO_STMT_WHILE(stmt).body).stmts);
            free(TO_STMT_WHILE(stmt).body);
            break;
        }
        case STMT_FOR: {
            free(TO_STMT_FOR(stmt).label);
            free_expression(TO_STMT_FOR(stmt).initializer);
            free_expression(TO_STMT_FOR(stmt).condition);
            free_expression(TO_STMT_FOR(stmt).advancement);
            for (size_t i = 0; i < TO_STMT_BLOCK(TO_STMT_FOR(stmt).body).stmts.count; i++)
            {
                free_stmt(TO_STMT_BLOCK(TO_STMT_FOR(stmt).body).stmts.data[i]);
            }
            dynarray_free(&TO_STMT_BLOCK(TO_STMT_FOR(stmt).body).stmts);
            free(TO_STMT_FOR(stmt).body);
            break;
        }
        case STMT_RETURN: {
            free_expression(TO_STMT_RETURN(stmt).returnval);
            break;
        }
        case STMT_EXPR: {
            free_expression(TO_STMT_EXPR(stmt).exp);
            break;
        }
        case STMT_FN: {
            free(TO_STMT_FN(stmt).name);
            for (size_t i = 0; i < TO_STMT_FN(stmt).parameters.count; i++)
            {
                free(TO_STMT_FN(stmt).parameters.data[i]);
            }
            dynarray_free(&TO_STMT_FN(stmt).parameters);
            for (size_t i = 0; i < TO_STMT_BLOCK(TO_STMT_FN(stmt).body).stmts.count; i++)
            {
                free_stmt(TO_STMT_BLOCK(TO_STMT_FN(stmt).body).stmts.data[i]);
            }
            dynarray_free(&TO_STMT_BLOCK(TO_STMT_FN(stmt).body).stmts);
            free(TO_STMT_FN(stmt).body);
            break;
        }
        case STMT_DECO: {
            free(stmt.as.stmt_deco.name);
            free_stmt(*stmt.as.stmt_deco.fn);
            free(stmt.as.stmt_deco.fn);
            break;
        }
        case STMT_STRUCT: {
            free(TO_STMT_STRUCT(stmt).name);
            for (size_t i = 0; i < TO_STMT_STRUCT(stmt).properties.count; i++)
            {
                free(TO_STMT_STRUCT(stmt).properties.data[i]);
            }
            dynarray_free(&TO_STMT_STRUCT(stmt).properties);
            break;
        }
        case STMT_USE: {
            free(TO_STMT_USE(stmt).path);
            break;
        }
        case STMT_YIELD: {
            free_expression(TO_STMT_YIELD(stmt).exp);
            break;
        }
        default:
            break;
    }
}

DynArray_Stmt parse(Parser *parser)
{
    DynArray_Stmt stmts = {0};
    advance(parser);
    while (parser->current.type != TOKEN_EOF)
    {
        dynarray_insert(&stmts, statement(parser));
    }
    return stmts;
}
