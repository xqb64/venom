#include "parser.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "dynarray.h"
#include "tokenizer.h"
#include "util.h"

#define CONSUME(parser, token_type, err)                       \
  ({                                                           \
    TokenResult r = consume((parser), (token_type));           \
    if (!r.is_ok)                                              \
      return (ParseFnResult) {                                 \
          .is_ok = false, .msg = strdup(err), .as.stmt = {0}}; \
    r.token;                                                   \
  })

#define HANDLE_EXPR(kind, ...)           \
  ({                                     \
    ParseFnResult r = kind(__VA_ARGS__); \
    if (!r.is_ok) {                      \
      return r;                          \
    }                                    \
    r.as.expr;                           \
  })

#define HANDLE_STMT(kind, ...)        \
  ({                                  \
    ParseFnResult r = kind((parser)); \
    if (!r.is_ok)                     \
      return r;                       \
    r.as.stmt;                        \
  })

void init_parser(Parser *parser, const DynArray_Token *tokens)
{
  memset(parser, 0, sizeof(Parser));
  parser->tokens = tokens;
}

void free_parse_result(const ParseResult *result)
{
  if (!result->is_ok) {
    free(result->msg);
  }
}

static Token pop_front(Parser *parser)
{
  if (parser->idx < parser->tokens->count) {
    return parser->tokens->data[parser->idx++];
  }
  return (Token) {.type = TOKEN_EOF, .length = 0, .start = NULL};
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
  for (int i = 0; i < size; i++) {
    TokenType type = va_arg(ap, TokenType);
    if (check(parser, type)) {
      advance(parser);
      va_end(ap);
      return true;
    }
  }
  va_end(ap);
  return false;
}

static TokenResult consume(Parser *parser, TokenType type)
{
  if (check(parser, type)) {
    return (TokenResult) {.token = advance(parser), .is_ok = true};
  }
  return (TokenResult) {.is_ok = false, .token = {0}};
}

static ParseFnResult boolean(Parser *parser)
{
  bool b;

  switch (parser->previous.type) {
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
  ExprLiteral e = {
      .kind = LIT_BOOLEAN,
      .as._bool = b,
  };

  return (ParseFnResult) {
      .as.expr = AS_EXPR_LITERAL(e), .is_ok = true, .msg = NULL};
}

static ParseFnResult null(Parser *parser)
{
  return (ParseFnResult) {
      .as.expr = AS_EXPR_LITERAL((ExprLiteral) {.kind = LIT_NULL}),
      .is_ok = true,
      .msg = NULL};
}

static ParseFnResult number(Parser *parser)
{
  ExprLiteral e = {
      .kind = LIT_NUMBER,
      .as._double = strtod(parser->previous.start, NULL),
      .span = (Span) {.start = parser->previous.span.start,
                      .end = parser->previous.span.end,
                      .line = parser->previous.span.line},
  };

  return (ParseFnResult) {
      .as.expr = AS_EXPR_LITERAL(e), .is_ok = true, .msg = NULL};
}

static ParseFnResult string(Parser *parser)
{
  ExprLiteral e = {
      .kind = LIT_STRING,
      .as.str =
          own_string_n(parser->previous.start, parser->previous.length - 1),
  };

  return (ParseFnResult) {
      .as.expr = AS_EXPR_LITERAL(e), .is_ok = true, .msg = NULL};
}

static ParseFnResult variable(Parser *parser)
{
  ExprVariable e = {
      .name = own_string_n(parser->previous.start, parser->previous.length),
      .span = (Span) {.start = parser->previous.span.start,
                      .end = parser->previous.span.end,
                      .line = parser->previous.span.line},
  };

  return (ParseFnResult) {.as.expr = AS_EXPR_VARIABLE(e),
                          .is_ok = true,
                          .msg = NULL,
                          .span = e.span};
}

static ParseFnResult literal(Parser *parser)
{
  switch (parser->previous.type) {
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

static ParseFnResult primary(Parser *parser);
static ParseFnResult expression(Parser *parser);
static ParseFnResult statement(Parser *parser);

static ParseFnResult finish_call(Parser *parser, Expr callee)
{
  DynArray_Expr arguments = {0};
  if (!check(parser, TOKEN_RIGHT_PAREN)) {
    do {
      ParseFnResult r = expression(parser);
      if (!r.is_ok) {
        for (size_t i = 0; i < arguments.count; i++) {
          free_expr(&arguments.data[i]);
        }
        dynarray_free(&arguments);
        return r;
      }
      dynarray_insert(&arguments, r.as.expr);
    } while (match(parser, 1, TOKEN_COMMA));
  }

  TokenResult rparen_result = consume(parser, TOKEN_RIGHT_PAREN);
  if (!rparen_result.is_ok) {
    for (size_t i = 0; i < arguments.count; i++) {
      free_expr(&arguments.data[i]);
    }
    dynarray_free(&arguments);
    return (ParseFnResult) {.is_ok = false,
                            .as.expr = {0},
                            .msg = strdup("Expected ')' after expression.")};
  }

  ExprCall e = {
      .callee = ALLOC(callee),
      .arguments = arguments,
  };
  return (ParseFnResult) {
      .as.expr = AS_EXPR_CALL(e), .is_ok = true, .msg = NULL};
}

static ParseFnResult call(Parser *parser)
{
  ParseFnResult expr_result = primary(parser);

  if (!expr_result.is_ok) {
    return expr_result;
  }

  Expr expr = expr_result.as.expr;

  for (;;) {
    if (match(parser, 1, TOKEN_LEFT_PAREN)) {
      expr = HANDLE_EXPR(finish_call, parser, expr);
    } else if (match(parser, 2, TOKEN_DOT, TOKEN_ARROW)) {
      char *op = own_string_n(parser->previous.start, parser->previous.length);

      TokenResult identifier_result = consume(parser, TOKEN_IDENTIFIER);
      if (!identifier_result.is_ok) {
        free(op);
        free_expr(&expr);
        return (ParseFnResult) {
            .is_ok = false,
            .as.expr = {0},
            .msg = strdup("Expected property name after '..")};
      }

      Token property_name = identifier_result.token;

      ExprGet get_expr = {
          .expr = ALLOC(expr),
          .property_name =
              own_string_n(property_name.start, property_name.length),
          .op = op,
          .span = (Span) {.start = expr.span.start,
                          .end = property_name.span.end,
                          .line = expr.span.line},
      };
      expr = AS_EXPR_GET(get_expr);
    } else if (match(parser, 1, TOKEN_LEFT_BRACKET)) {
      ParseFnResult index_result = expression(parser);
      if (!index_result.is_ok) {
        free_expr(&expr);
        return index_result;
      }
      Expr index = index_result.as.expr;

      TokenResult rbracket_result = consume(parser, TOKEN_RIGHT_BRACKET);
      if (!rbracket_result.is_ok) {
        free_expr(&expr);
        free_expr(&index);
        return (ParseFnResult) {
            .is_ok = false,
            .as.expr = {0},
            .msg = strdup("Expected ']' after index."),
            .span = (Span) {.start = expr.span.start,
                            .end = rbracket_result.token.span.end,
                            .line = expr.span.line}};
      }

      ExprSubscript subscript_expr = {
          .expr = ALLOC(expr),
          .index = ALLOC(index),
      };
      expr = AS_EXPR_SUBSCRIPT(subscript_expr);
    } else {
      break;
    }
  }

  return (ParseFnResult) {.as.expr = expr, .is_ok = true, .msg = NULL};
}

static ParseFnResult unary(Parser *parser)
{
  if (match(parser, 5, TOKEN_MINUS, TOKEN_AMPERSAND, TOKEN_STAR, TOKEN_BANG,
            TOKEN_TILDE)) {
    char *op = own_string_n(parser->previous.start, parser->previous.length);

    ParseFnResult right_result = unary(parser);
    if (!right_result.is_ok) {
      free(op);
      return right_result;
    }
    Expr right = right_result.as.expr;

    ExprUnary e = {.expr = ALLOC(right), .op = op};
    return (ParseFnResult) {
        .as.expr = AS_EXPR_UNARY(e), .is_ok = true, .msg = NULL};
  }

  return call(parser);
}

static ParseFnResult factor(Parser *parser)
{
  ParseFnResult expr_result = unary(parser);
  if (!expr_result.is_ok) {
    return expr_result;
  }

  Expr expr = expr_result.as.expr;

  while (match(parser, 3, TOKEN_STAR, TOKEN_SLASH, TOKEN_MOD)) {
    char *op = own_string_n(parser->previous.start, parser->previous.length);

    ParseFnResult right_result = unary(parser);
    if (!right_result.is_ok) {
      free(op);
      free_expr(&expr);
      return right_result;
    }

    Expr right = right_result.as.expr;

    ExprBinary binexp = {.lhs = ALLOC(expr),
                         .rhs = ALLOC(right),
                         .op = op,
                         .span = (Span) {.start = expr.span.start,
                                         .end = right.span.end,
                                         .line = expr.span.line}};

    expr = AS_EXPR_BINARY(binexp);
  }

  return (ParseFnResult) {
      .as.expr = expr, .is_ok = true, .msg = NULL, .span = expr.span};
}

static ParseFnResult term(Parser *parser)
{
  ParseFnResult expr_result = factor(parser);
  if (!expr_result.is_ok) {
    return expr_result;
  }

  Expr expr = expr_result.as.expr;

  while (match(parser, 3, TOKEN_PLUS, TOKEN_MINUS, TOKEN_PLUSPLUS)) {
    char *op = own_string_n(parser->previous.start, parser->previous.length);

    ParseFnResult right_result = factor(parser);
    if (!right_result.is_ok) {
      free(op);
      free_expr(&expr);
      return right_result;
    }

    Expr right = right_result.as.expr;

    ExprBinary binexp = {.lhs = ALLOC(expr),
                         .rhs = ALLOC(right),
                         .op = op,
                         .span = (Span) {.start = expr.span.start,
                                         .end = right.span.end,
                                         .line = expr.span.line}};

    expr = AS_EXPR_BINARY(binexp);
  }

  return (ParseFnResult) {
      .as.expr = expr, .is_ok = true, .msg = NULL, .span = expr.span};
}

static ParseFnResult bitwise_shift(Parser *parser)
{
  ParseFnResult expr_result = term(parser);
  if (!expr_result.is_ok) {
    return expr_result;
  }

  Expr expr = expr_result.as.expr;

  while (match(parser, 2, TOKEN_GREATER_GREATER, TOKEN_LESS_LESS)) {
    char *op = own_string_n(parser->previous.start, parser->previous.length);

    ParseFnResult right_result = term(parser);
    if (!right_result.is_ok) {
      free(op);
      free_expr(&expr);
      return right_result;
    }

    Expr right = right_result.as.expr;

    ExprBinary binexp = {.lhs = ALLOC(expr),
                         .rhs = ALLOC(right),
                         .op = op,
                         .span = (Span) {.start = expr.span.start,
                                         .end = right.span.end,
                                         .line = expr.span.line}};
    expr = AS_EXPR_BINARY(binexp);
  }

  return (ParseFnResult) {.as.expr = expr, .is_ok = true, .msg = NULL};
}

static ParseFnResult comparison(Parser *parser)
{
  ParseFnResult expr_result = bitwise_shift(parser);
  if (!expr_result.is_ok) {
    return expr_result;
  }

  Expr expr = expr_result.as.expr;

  while (match(parser, 4, TOKEN_GREATER, TOKEN_LESS, TOKEN_GREATER_EQUAL,
               TOKEN_LESS_EQUAL)) {
    char *op = own_string_n(parser->previous.start, parser->previous.length);

    ParseFnResult right_result = bitwise_shift(parser);
    if (!right_result.is_ok) {
      free(op);
      free_expr(&expr);
      return right_result;
    }

    Expr right = right_result.as.expr;

    ExprBinary binexp = {.lhs = ALLOC(expr),
                         .rhs = ALLOC(right),
                         .op = op,
                         .span = (Span) {.start = expr.span.start,
                                         .end = right.span.end,
                                         .line = expr.span.line}};
    expr = AS_EXPR_BINARY(binexp);
  }

  return (ParseFnResult) {.as.expr = expr, .is_ok = true, .msg = NULL};
}

static ParseFnResult equality(Parser *parser)
{
  ParseFnResult expr_result = comparison(parser);
  if (!expr_result.is_ok) {
    return expr_result;
  }

  Expr expr = expr_result.as.expr;

  while (match(parser, 2, TOKEN_DOUBLE_EQUAL, TOKEN_BANG_EQUAL)) {
    char *op = own_string_n(parser->previous.start, parser->previous.length);

    ParseFnResult right_result = comparison(parser);
    if (!right_result.is_ok) {
      free(op);
      free_expr(&expr);
      return right_result;
    }

    Expr right = right_result.as.expr;

    ExprBinary binexp = {.lhs = ALLOC(expr),
                         .rhs = ALLOC(right),
                         .op = op,
                         .span = (Span) {.start = expr.span.start,
                                         .end = right.span.end,
                                         .line = expr.span.line}};
    expr = AS_EXPR_BINARY(binexp);
  }

  return (ParseFnResult) {.as.expr = expr, .is_ok = true, .msg = NULL};
}

static ParseFnResult bitwise_and(Parser *parser)
{
  ParseFnResult expr_result = equality(parser);
  if (!expr_result.is_ok) {
    return expr_result;
  }

  Expr expr = expr_result.as.expr;
  while (match(parser, 1, TOKEN_AMPERSAND)) {
    char *op = own_string_n(parser->previous.start, parser->previous.length);

    ParseFnResult right_result = equality(parser);
    if (!right_result.is_ok) {
      free(op);
      free_expr(&expr);
      return right_result;
    }

    Expr right = right_result.as.expr;

    ExprBinary binexp = {.lhs = ALLOC(expr),
                         .rhs = ALLOC(right),
                         .op = op,
                         .span = (Span) {.start = expr.span.start,
                                         .end = right.span.end,
                                         .line = expr.span.line}};
    expr = AS_EXPR_BINARY(binexp);
  }

  return (ParseFnResult) {.as.expr = expr, .is_ok = true, .msg = NULL};
}

static ParseFnResult bitwise_xor(Parser *parser)
{
  ParseFnResult expr_result = bitwise_and(parser);
  if (!expr_result.is_ok) {
    return expr_result;
  }

  Expr expr = expr_result.as.expr;

  while (match(parser, 1, TOKEN_CARET)) {
    char *op = own_string_n(parser->previous.start, parser->previous.length);

    ParseFnResult right_result = bitwise_and(parser);
    if (!right_result.is_ok) {
      free(op);
      free_expr(&expr);
      return right_result;
    }

    Expr right = right_result.as.expr;

    ExprBinary binexp = {.lhs = ALLOC(expr),
                         .rhs = ALLOC(right),
                         .op = op,
                         .span = (Span) {.start = expr.span.start,
                                         .end = right.span.end,
                                         .line = expr.span.line}};
    expr = AS_EXPR_BINARY(binexp);
  }

  return (ParseFnResult) {.as.expr = expr, .is_ok = true, .msg = NULL};
}

static ParseFnResult bitwise_or(Parser *parser)
{
  ParseFnResult expr_result = bitwise_xor(parser);
  if (!expr_result.is_ok) {
    return expr_result;
  }

  Expr expr = expr_result.as.expr;

  while (match(parser, 1, TOKEN_PIPE)) {
    char *op = own_string_n(parser->previous.start, parser->previous.length);

    ParseFnResult right_result = bitwise_xor(parser);
    if (!right_result.is_ok) {
      free(op);
      free_expr(&expr);
      return right_result;
    }

    Expr right = right_result.as.expr;

    ExprBinary binexp = {.lhs = ALLOC(expr),
                         .rhs = ALLOC(right),
                         .op = op,
                         .span = (Span) {.start = expr.span.start,
                                         .end = right.span.end,
                                         .line = expr.span.line}};
    expr = AS_EXPR_BINARY(binexp);
  }

  return (ParseFnResult) {.as.expr = expr, .is_ok = true, .msg = NULL};
}

static ParseFnResult and_(Parser *parser)
{
  ParseFnResult expr_result = bitwise_or(parser);
  if (!expr_result.is_ok) {
    return expr_result;
  }

  Expr expr = expr_result.as.expr;

  while (match(parser, 1, TOKEN_DOUBLE_AMPERSAND)) {
    char *op = own_string_n(parser->previous.start, parser->previous.length);

    ParseFnResult right_result = bitwise_or(parser);
    if (!right_result.is_ok) {
      free(op);
      free_expr(&expr);
      return right_result;
    }

    Expr right = right_result.as.expr;

    ExprBinary binexp = {.lhs = ALLOC(expr),
                         .rhs = ALLOC(right),
                         .op = op,
                         .span = (Span) {.start = expr.span.start,
                                         .end = right.span.end,
                                         .line = expr.span.line}};
    expr = AS_EXPR_BINARY(binexp);
  }

  return (ParseFnResult) {.as.expr = expr, .is_ok = true, .msg = NULL};
}

static ParseFnResult or_(Parser *parser)
{
  ParseFnResult expr_result = and_(parser);
  if (!expr_result.is_ok) {
    return expr_result;
  }

  Expr expr = expr_result.as.expr;

  while (match(parser, 1, TOKEN_DOUBLE_PIPE)) {
    char *op = own_string_n(parser->previous.start, parser->previous.length);

    ParseFnResult right_result = and_(parser);
    if (!right_result.is_ok) {
      free(op);
      free_expr(&expr);
      return right_result;
    }

    Expr right = right_result.as.expr;

    ExprBinary binexp = {.lhs = ALLOC(expr),
                         .rhs = ALLOC(right),
                         .op = op,
                         .span = (Span) {.start = expr.span.start,
                                         .end = right.span.end,
                                         .line = expr.span.line}};
    expr = AS_EXPR_BINARY(binexp);
  }

  return (ParseFnResult) {.as.expr = expr, .is_ok = true, .msg = NULL};
}

static ParseFnResult assignment(Parser *parser)
{
  ParseFnResult expr_result = or_(parser);
  if (!expr_result.is_ok) {
    return expr_result;
  }

  Expr expr = expr_result.as.expr;

  if (match(parser, 11, TOKEN_EQUAL, TOKEN_PLUS_EQUAL, TOKEN_MINUS_EQUAL,
            TOKEN_STAR_EQUAL, TOKEN_SLASH_EQUAL, TOKEN_MOD_EQUAL,
            TOKEN_AMPERSAND_EQUAL, TOKEN_PIPE_EQUAL, TOKEN_CARET_EQUAL,
            TOKEN_GREATER_GREATER_EQUAL, TOKEN_LESS_LESS_EQUAL)) {
    char *op = own_string_n(parser->previous.start, parser->previous.length);

    ParseFnResult right_result = or_(parser);
    if (!right_result.is_ok) {
      free(op);
      free_expr(&expr);
      return right_result;
    }

    Expr right = right_result.as.expr;

    ExprAssign assignexp = {.lhs = ALLOC(expr),
                            .rhs = ALLOC(right),
                            .op = op,
                            .span = (Span) {.start = expr.span.start,
                                            .end = right.span.end,
                                            .line = expr.span.line}};
    expr = AS_EXPR_ASSIGN(assignexp);
  }

  return (ParseFnResult) {
      .as.expr = expr, .is_ok = true, .msg = NULL, .span = expr.span};
}

static ParseFnResult expression(Parser *parser)
{
  ParseFnResult r = assignment(parser);
  if (!r.is_ok) {
    free_expr(&r.as.expr);
  }
  return r;
}

static ParseFnResult grouping(Parser *parser)
{
  TokenResult lparen_result = consume(parser, TOKEN_LEFT_PAREN);
  if (!lparen_result.is_ok) {
    return (ParseFnResult) {.is_ok = false,
                            .as.stmt = {0},
                            .msg = strdup("Expected '('."),
                            .span = (Span) {.start = parser->current.span.start,
                                            .end = parser->current.span.end,
                                            .line = parser->current.span.line}};
  }

  Expr expr = HANDLE_EXPR(expression, parser);

  TokenResult rparen_result = consume(parser, TOKEN_RIGHT_PAREN);
  if (!rparen_result.is_ok) {
    free_expr(&expr);
    return (ParseFnResult) {
        .is_ok = false,
        .as.expr = {0},
        .msg = strdup("Unmatched closing parentheses."),
        .span = (Span) {.start = lparen_result.token.span.start,
                        .end = expr.span.end,
                        .line = expr.span.line}};
  }

  return (ParseFnResult) {.as.expr = expr, .is_ok = true, .msg = NULL};
}

static ParseFnResult block(Parser *parser)
{
  parser->depth++;

  TokenResult lbrace_result = consume(parser, TOKEN_LEFT_BRACE);
  if (!lbrace_result.is_ok) {
    return (ParseFnResult) {.is_ok = false,
                            .as.stmt = {0},
                            .msg = strdup("Expected '{' token."),
                            .span = (Span) {.line = parser->previous.span.line,
                                            .start = parser->previous.span.end,
                                            .end = parser->previous.span.end}};
  }

  DynArray_Stmt stmts = {0};
  while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
    ParseFnResult r = statement(parser);
    if (!r.is_ok) {
      free_ast(&stmts);
      return r;
    }
    dynarray_insert(&stmts, r.as.stmt);
  }

  TokenResult rbrace_result = consume(parser, TOKEN_RIGHT_BRACE);
  if (!rbrace_result.is_ok) {
    free_ast(&stmts);
    return (ParseFnResult) {
        .is_ok = false,
        .as.stmt = {0},
        .msg = strdup("Expected '}' at the end of the block."),
        .span = (Span) {.line = parser->previous.span.line,
                        .start = parser->previous.span.end,
                        .end = parser->previous.span.end}};
  }

  StmtBlock body = {.depth = parser->depth--,
                    .stmts = stmts,
                    .span = (Span) {.line = lbrace_result.token.span.line,
                                    .start = lbrace_result.token.span.start,
                                    .end = rbrace_result.token.span.end}};

  return (ParseFnResult) {.as.stmt = AS_STMT_BLOCK(body),
                          .is_ok = true,
                          .msg = NULL,
                          .span = body.span};
}

static ParseFnResult struct_initializer(Parser *parser)
{
  char *name = own_string_n(parser->previous.start, parser->previous.length);

  TokenResult lbrace_result = consume(parser, TOKEN_LEFT_BRACE);
  if (!lbrace_result.is_ok) {
    free(name);
    return (ParseFnResult) {.is_ok = false,
                            .as.expr = {0},
                            .msg = strdup("Expected '{' after struct name.")};
  }

  size_t start_of_last_offending_expr = 0;
  DynArray_Expr initializers = {0};
  do {
    ParseFnResult property_result = expression(parser);
    if (!property_result.is_ok) {
      for (size_t i = 0; i < initializers.count; i++) {
        free_expr(&initializers.data[i]);
      }
      dynarray_free(&initializers);
      return property_result;
    }

    Expr property = property_result.as.expr;

    start_of_last_offending_expr = property.span.start;

    TokenResult colon_result = consume(parser, TOKEN_COLON);
    if (!colon_result.is_ok) {
      free(name);
      for (size_t i = 0; i < initializers.count; i++) {
        free_expr(&initializers.data[i]);
      }
      dynarray_free(&initializers);
      free_expr(&property);
      return (ParseFnResult) {
          .is_ok = false,
          .as.expr = {0},
          .msg = strdup("Expected ':' after property name.")};
    }

    ParseFnResult value_result = expression(parser);
    if (!value_result.is_ok) {
      for (size_t i = 0; i < initializers.count; i++) {
        free_expr(&initializers.data[i]);
      }
      dynarray_free(&initializers);
      free_expr(&property);
      return value_result;
    }

    Expr value = value_result.as.expr;

    ExprStructInitializer structinitexp = {
        .property = ALLOC(property),
        .value = ALLOC(value),
    };
    dynarray_insert(&initializers, AS_EXPR_STRUCT_INITIALIZER(structinitexp));
  } while (match(parser, 1, TOKEN_COMMA));

  if (parser->current.type != TOKEN_COMMA) {
    free(name);

    for (size_t i = 0; i < initializers.count; i++) {
      free_expr(&initializers.data[i]);
    }
    dynarray_free(&initializers);

    return (ParseFnResult) {
        .is_ok = false,
        .as.expr = {0},
        .msg = strdup("Expected comma after `key: value` pair"),
        .span = (Span) {.start = start_of_last_offending_expr,
                        .end = parser->current.span.end,
                        .line = parser->current.span.line}};
  }

  TokenResult rbrace_result = consume(parser, TOKEN_RIGHT_BRACE);
  if (!rbrace_result.is_ok) {
    free(name);

    for (size_t i = 0; i < initializers.count; i++) {
      free_expr(&initializers.data[i]);
    }
    dynarray_free(&initializers);

    return (ParseFnResult) {
        .is_ok = false,
        .as.expr = {0},
        .msg = strdup("Expected '}' after struct initialization."),
        .span = (Span) {.start = lbrace_result.token.span.start,
                        .end = parser->current.span.end,
                        .line = lbrace_result.token.span.line}};
  }

  ExprStruct structexp = {
      .initializers = initializers,
      .name = name,
  };
  return (ParseFnResult) {
      .as.expr = AS_EXPR_STRUCT(structexp), .is_ok = true, .msg = NULL};
}

static ParseFnResult array_initializer(Parser *parser)
{
  DynArray_Expr initializers = {0};
  do {
    ParseFnResult expr_result = expression(parser);
    if (!expr_result.is_ok) {
      for (size_t i = 0; i < initializers.count; i++) {
        free_expr(&initializers.data[i]);
      }
      dynarray_free(&initializers);
      return expr_result;
    }
    dynarray_insert(&initializers, expr_result.as.expr);
  } while (match(parser, 1, TOKEN_COMMA));

  TokenResult rbracket_result = consume(parser, TOKEN_RIGHT_BRACKET);
  if (!rbracket_result.is_ok) {
    for (size_t i = 0; i < initializers.count; i++) {
      free_expr(&initializers.data[i]);
    }
    dynarray_free(&initializers);

    return (ParseFnResult) {.is_ok = false,
                            .as.expr = {0},
                            .msg = strdup("Expected ']' after array members.")};
  }

  ExprArray arrayexp = {
      .elements = initializers,
  };
  return (ParseFnResult) {
      .as.expr = AS_EXPR_ARRAY(arrayexp), .is_ok = true, .msg = NULL};
}

static ParseFnResult primary(Parser *parser)
{
  if (match(parser, 1, TOKEN_IDENTIFIER)) {
    if (check(parser, TOKEN_LEFT_BRACE)) {
      return struct_initializer(parser);
    } else {
      return variable(parser);
    }
  } else if (check(parser, TOKEN_LEFT_PAREN)) {
    return grouping(parser);
  } else if (match(parser, 5, TOKEN_TRUE, TOKEN_FALSE, TOKEN_NULL, TOKEN_NUMBER,
                   TOKEN_STRING)) {
    return literal(parser);
  } else if (match(parser, 1, TOKEN_LEFT_BRACKET)) {
    return array_initializer(parser);
  } else {
    assert(0);
  }
}

static ParseFnResult print_statement(Parser *parser)
{
  TokenResult print_result = consume(parser, TOKEN_PRINT);
  if (!print_result.is_ok) {
    return (ParseFnResult) {.is_ok = false,
                            .as.stmt = {0},
                            .msg = strdup("Expected 'print' token."),
                            .span = (Span) {.start = parser->current.span.start,
                                            .end = parser->current.span.end,
                                            .line = parser->current.span.line}};
  }

  ParseFnResult expr_result = expression(parser);
  if (!expr_result.is_ok) {
    return expr_result;
  }

  Expr expr = expr_result.as.expr;

  TokenResult semicolon_result = consume(parser, TOKEN_SEMICOLON);
  if (!semicolon_result.is_ok) {
    ParseFnResult r = {.is_ok = false,
                       .as.stmt = {0},
                       .msg = strdup("Expected ';' after 'print' statement."),
                       .span = (Span) {.start = expr.span.end + 1,
                                       .end = expr.span.end + 1,
                                       .line = expr.span.line}};
    free_expr(&expr);
    return r;
  }

  StmtPrint stmt = {.expr = expr,
                    .span = (Span) {.start = print_result.token.span.start,
                                    .end = semicolon_result.token.span.end,
                                    .line = print_result.token.span.line}};

  return (ParseFnResult) {.as.stmt = AS_STMT_PRINT(stmt),
                          .is_ok = true,
                          .msg = NULL,
                          .span = stmt.span};
}

static ParseFnResult let_statement(Parser *parser)
{
  TokenResult let_result = consume(parser, TOKEN_LET);
  if (!let_result.is_ok) {
    return (ParseFnResult) {.is_ok = false,
                            .as.stmt = {0},
                            .msg = strdup("Expected 'let' token."),
                            .span = parser->current.span};
  }

  TokenResult identifier_result = consume(parser, TOKEN_IDENTIFIER);
  if (!identifier_result.is_ok) {
    return (ParseFnResult) {.is_ok = false,
                            .as.stmt = {0},
                            .msg = strdup("Expected identifier after 'let'."),
                            .span = parser->current.span};
  }

  Token identifier = identifier_result.token;

  char *name = own_string_n(identifier.start, identifier.length);

  TokenResult equal_result = consume(parser, TOKEN_EQUAL);
  if (!equal_result.is_ok) {
    free(name);
    return (ParseFnResult) {
        .is_ok = false,
        .as.stmt = {0},
        .msg = strdup("Expected '=' after variable name in 'let' statement."),
        .span = parser->current.span};
  }

  ParseFnResult initializer_result = expression(parser);
  if (!initializer_result.is_ok) {
    free(name);
    return initializer_result;
  }

  Expr initializer = initializer_result.as.expr;

  TokenResult semicolon_result = consume(parser, TOKEN_SEMICOLON);
  if (!semicolon_result.is_ok) {
    free(name);
    free_expr(&initializer);
    return (ParseFnResult) {
        .is_ok = false,
        .as.stmt = {0},
        .msg = strdup("Expected ';' after 'let' statement."),
        .span = (Span) {.line = parser->previous.span.line,
                        .start = parser->previous.span.end,
                        .end = parser->previous.span.end}};
  }

  StmtLet stmt = {.name = name,
                  .initializer = initializer,
                  .span = (Span) {.start = let_result.token.span.start,
                                  .end = semicolon_result.token.span.end,
                                  .line = let_result.token.span.line}};
  return (ParseFnResult) {.as.stmt = AS_STMT_LET(stmt),
                          .is_ok = true,
                          .msg = NULL,
                          .span = stmt.span};
}

static ParseFnResult expression_statement(Parser *parser)
{
  Expr expr = HANDLE_EXPR(expression, parser);

  TokenResult semicolon_result = consume(parser, TOKEN_SEMICOLON);
  if (!semicolon_result.is_ok) {
    free_expr(&expr);
    return (ParseFnResult) {
        .is_ok = false,
        .as.stmt = {0},
        .msg = strdup("Expected ';' after expression statement.")};
  }

  StmtExpr stmt = {.expr = expr};
  return (ParseFnResult) {
      .as.stmt = AS_STMT_EXPR(stmt), .is_ok = true, .msg = NULL};
}

static ParseFnResult if_statement(Parser *parser)
{
  TokenResult if_result = consume(parser, TOKEN_IF);
  if (!if_result.is_ok) {
    return (ParseFnResult) {.is_ok = false,
                            .as.stmt = {0},
                            .msg = strdup("Expected 'if' token."),
                            .span = parser->current.span};
  }

  TokenResult lparen_result = consume(parser, TOKEN_LEFT_PAREN);
  if (!lparen_result.is_ok) {
    return (ParseFnResult) {.is_ok = false,
                            .as.stmt = {0},
                            .msg = strdup("Expected '(' after 'if'."),
                            .span = if_result.token.span};
  }

  ParseFnResult condition_result = expression(parser);
  if (!condition_result.is_ok) {
    return condition_result;
  }

  Expr condition = condition_result.as.expr;

  TokenResult rparen_result = consume(parser, TOKEN_RIGHT_PAREN);
  if (!rparen_result.is_ok) {
    free_expr(&condition);
    return (ParseFnResult) {
        .is_ok = false,
        .as.stmt = {0},
        .msg = strdup("Expected ')' after 'if' condition."),
        .span = (Span) {.line = lparen_result.token.span.line,
                        .start = lparen_result.token.span.start,
                        .end = parser->previous.span.end}};
  }

  Stmt *then_branch = malloc(sizeof(Stmt));
  Stmt *else_branch = NULL;

  ParseFnResult then_result = statement(parser);
  if (!then_result.is_ok) {
    free_expr(&condition);
    free(then_branch);
    return then_result;
  }

  *then_branch = then_result.as.stmt;

  if (match(parser, 1, TOKEN_ELSE)) {
    ParseFnResult else_result = statement(parser);
    if (!else_result.is_ok) {
      free_expr(&condition);
      free_stmt(then_branch);
      free(then_branch);
      return else_result;
    }
    else_branch = malloc(sizeof(Stmt));
    *else_branch = else_result.as.stmt;
  }

  StmtIf stmt = {
      .then_branch = then_branch,
      .else_branch = else_branch,
      .condition = condition,
      .span = (Span) {.line = if_result.token.span.line,
                      .start = if_result.token.span.start,
                      .end = else_branch ? else_branch->span.end
                                         : then_branch->span.end},
  };
  return (ParseFnResult) {.as.stmt = AS_STMT_IF(stmt),
                          .is_ok = true,
                          .msg = NULL,
                          .span = stmt.span};
}

static ParseFnResult while_statement(Parser *parser)
{
  TokenResult while_result = consume(parser, TOKEN_WHILE);
  if (!while_result.is_ok) {
    return (ParseFnResult) {.is_ok = false,
                            .as.stmt = {0},
                            .msg = strdup("Expected 'while' token."),
                            .span = parser->current.span};
  }

  TokenResult lparen_result = consume(parser, TOKEN_LEFT_PAREN);
  if (!lparen_result.is_ok) {
    return (ParseFnResult) {.is_ok = false,
                            .as.stmt = {0},
                            .msg = strdup("Expected '(' after 'while'."),
                            .span = parser->previous.span};
  }

  ParseFnResult condition_result = expression(parser);
  if (!condition_result.is_ok) {
    return condition_result;
  }

  Expr condition = condition_result.as.expr;

  TokenResult rparen_result = consume(parser, TOKEN_RIGHT_PAREN);
  if (!rparen_result.is_ok) {
    free_expr(&condition);
    return (ParseFnResult) {
        .is_ok = false,
        .as.stmt = {0},
        .msg = strdup("Expected ')' after 'while' condition."),
        .span = (Span) {.line = lparen_result.token.span.line,
                        .start = lparen_result.token.span.start,
                        .end = condition.span.end}};
  }

  ParseFnResult body_result = block(parser);
  if (!body_result.is_ok) {
    free_expr(&condition);
    return body_result;
  }

  Stmt body = body_result.as.stmt;

  StmtWhile stmt = {
      .condition = condition,
      .body = ALLOC(body),
      .label = NULL,
      .span = (Span) {.line = while_result.token.span.line,
                      .start = while_result.token.span.start,
                      .end = body.span.end},
  };
  return (ParseFnResult) {.as.stmt = AS_STMT_WHILE(stmt),
                          .is_ok = true,
                          .msg = NULL,
                          .span = stmt.span};
}

static ParseFnResult for_statement(Parser *parser)
{
  TokenResult for_result = consume(parser, TOKEN_FOR);
  if (!for_result.is_ok) {
    return (ParseFnResult) {.is_ok = false,
                            .as.stmt = {0},
                            .msg = strdup("Expected 'for' token."),
                            .span = parser->current.span};
  }

  TokenResult lparen_result = consume(parser, TOKEN_LEFT_PAREN);
  if (!lparen_result.is_ok) {
    return (ParseFnResult) {.is_ok = false,
                            .as.stmt = {0},
                            .msg = strdup("Expected '(' after 'for'."),
                            .span = parser->previous.span};
  }

  TokenResult let_result = consume(parser, TOKEN_LET);
  if (!let_result.is_ok) {
    return (ParseFnResult) {
        .is_ok = false,
        .as.stmt = {0},
        .msg = strdup("Expected 'let' after '(' in 'for' initializer."),
        .span = parser->previous.span};
  }

  ParseFnResult initializer_result = expression(parser);
  if (!initializer_result.is_ok) {
    return initializer_result;
  }

  Expr initializer = initializer_result.as.expr;

  TokenResult semicolon_result1 = consume(parser, TOKEN_SEMICOLON);
  if (!semicolon_result1.is_ok) {
    free_expr(&initializer);
    return (ParseFnResult) {
        .is_ok = false,
        .as.stmt = {0},
        .msg = strdup("Expected ';' after 'for' initializer."),
        .span = (Span) {.line = initializer.span.line,
                        .start = initializer.span.start,
                        .end = initializer.span.end + 1}};
  }

  ParseFnResult condition_result = expression(parser);
  if (!condition_result.is_ok) {
    free_expr(&initializer);
    return condition_result;
  }

  Expr condition = condition_result.as.expr;

  TokenResult semicolon_result2 = consume(parser, TOKEN_SEMICOLON);
  if (!semicolon_result2.is_ok) {
    free_expr(&initializer);
    free_expr(&condition);
    return (ParseFnResult) {
        .is_ok = false,
        .as.stmt = {0},
        .msg = strdup("Expected ';' after 'for' condition."),
        .span = (Span) {.line = condition.span.line,
                        .start = condition.span.start,
                        .end = condition.span.end + 1}};
  }

  ParseFnResult advancement_result = expression(parser);
  if (!advancement_result.is_ok) {
    free_expr(&initializer);
    free_expr(&condition);
    return advancement_result;
  }

  Expr advancement = advancement_result.as.expr;
  TokenResult rparen_result = consume(parser, TOKEN_RIGHT_PAREN);
  if (!rparen_result.is_ok) {
    free_expr(&initializer);
    free_expr(&condition);
    free_expr(&advancement);
    return (ParseFnResult) {
        .is_ok = false,
        .as.stmt = {0},
        .msg = strdup("Expected ')' after 'for' advancement."),
        .span = (Span) {.line = advancement.span.line,
                        .start = advancement.span.start,
                        .end = advancement.span.end + 1}};
  }

  ParseFnResult body_result = block(parser);
  if (!body_result.is_ok) {
    free_expr(&initializer);
    free_expr(&condition);
    free_expr(&advancement);
    return body_result;
  }

  Stmt body = body_result.as.stmt;

  StmtFor stmt = {.initializer = initializer,
                  .condition = condition,
                  .advancement = advancement,
                  .body = ALLOC(body),
                  .label = NULL,
                  .span = (Span) {.line = for_result.token.span.line,
                                  .start = for_result.token.span.start,
                                  .end = body.span.end}};

  return (ParseFnResult) {.as.stmt = AS_STMT_FOR(stmt),
                          .is_ok = true,
                          .msg = NULL,
                          .span = stmt.span};
}

static ParseFnResult function_statement(Parser *parser)
{
  TokenResult fn_result = consume(parser, TOKEN_FN);
  if (!fn_result.is_ok) {
    return (ParseFnResult) {.is_ok = false,
                            .as.stmt = {0},
                            .msg = strdup("Expected 'fn' token."),
                            .span = parser->current.span};
  }

  TokenResult fn_identifier_result = consume(parser, TOKEN_IDENTIFIER);
  if (!fn_identifier_result.is_ok) {
    return (ParseFnResult) {.is_ok = false,
                            .as.stmt = {0},
                            .msg = strdup("Expected identifier after 'fn'."),
                            .span = parser->current.span};
  }

  Token name = fn_identifier_result.token;

  TokenResult lparen_result = consume(parser, TOKEN_LEFT_PAREN);
  if (!lparen_result.is_ok) {
    return (ParseFnResult) {.is_ok = false,
                            .as.stmt = {0},
                            .msg = strdup("Expected '(' after identifier in 'fn' statement."),
                            .span = (Span) {.line = parser->previous.span.line,
                                            .start = parser->previous.span.end,
                                            .end = parser->previous.span.end}};
  }

  DynArray_char_ptr parameters = {0};
  if (!check(parser, TOKEN_RIGHT_PAREN)) {
    do {
      TokenResult identifier_result = consume(parser, TOKEN_IDENTIFIER);
      if (!identifier_result.is_ok) {
        for (size_t i = 0; i < parameters.count; i++) {
          free(parameters.data[i]);
        }
        dynarray_free(&parameters);
        return (ParseFnResult) {
            .is_ok = false,
            .as.stmt = {0},
            .msg =
                strdup("Expected parameter name after '(' in 'fn' statement."),
            .span = lparen_result.token.span};
      }

      Token parameter = identifier_result.token;

      dynarray_insert(&parameters,
                      own_string_n(parameter.start, parameter.length));
    } while (match(parser, 1, TOKEN_COMMA));
  }

  TokenResult rparen_result = consume(parser, TOKEN_RIGHT_PAREN);
  if (!rparen_result.is_ok) {
    for (size_t i = 0; i < parameters.count; i++) {
      free(parameters.data[i]);
    }
    dynarray_free(&parameters);

    return (ParseFnResult) {
        .is_ok = false,
        .as.stmt = {0},
        .msg =
            strdup("Expected ')' after the parameter list in 'fn' statement."),
        .span = (Span) {.line = parser->previous.span.line, .start = parser->previous.span.end, .end = parser->previous.span.end}};
  }

  ParseFnResult body_result = block(parser);
  if (!body_result.is_ok) {
    for (size_t i = 0; i < parameters.count; i++) {
      free(parameters.data[i]);
    }
    dynarray_free(&parameters);
    return body_result;
  }

  Stmt body = body_result.as.stmt;

  StmtFn stmt = {
      .name = own_string_n(name.start, name.length),
      .body = ALLOC(body),
      .parameters = parameters,
      .span = (Span) {.line = fn_result.token.span.line,
                      .start = fn_result.token.span.start,
                      .end = body.span.end},
  };
  return (ParseFnResult) {
      .as.stmt = AS_STMT_FN(stmt), .is_ok = true, .msg = NULL};
}

static ParseFnResult decorator_statement(Parser *parser)
{
  Token decorator =
      CONSUME(parser, TOKEN_IDENTIFIER, "Expected identifier after '@'.");

  CONSUME(parser, TOKEN_FN, "Expected 'fn' after '@deco'.");

  Stmt fn = HANDLE_STMT(function_statement, parser);

  StmtDecorator stmt = {
      .name = own_string_n(decorator.start, decorator.length),
      .fn = ALLOC(fn),
  };
  return (ParseFnResult) {
      .as.stmt = AS_STMT_DECORATOR(stmt), .is_ok = true, .msg = NULL};
}

static ParseFnResult return_statement(Parser *parser)
{
  TokenResult return_result = consume(parser, TOKEN_RETURN);
  if (!return_result.is_ok) {
    return (ParseFnResult) {.is_ok = false,
                            .as.stmt = {0},
                            .msg = strdup("Expected 'return' token."),
                            .span = parser->previous.span};
  }

  ParseFnResult expr_result = expression(parser);
  if (!expr_result.is_ok) {
    return expr_result;
  }

  Expr expr = expr_result.as.expr;

  TokenResult semicolon_result = consume(parser, TOKEN_SEMICOLON);
  if (!semicolon_result.is_ok) {
    free_expr(&expr);
    return (ParseFnResult) {
        .is_ok = false,
        .as.stmt = {0},
        .msg = strdup("Expected ';' after 'return' statement."),
        .span = (Span) {.line = parser->previous.span.line,
                        .start = parser->previous.span.end,
                        .end = parser->previous.span.end}};
  }

  StmtReturn stmt = {.expr = expr,
                     .span = (Span) {.line = return_result.token.span.line,
                                     .start = return_result.token.span.start,
                                     .end = semicolon_result.token.span.end}};

  return (ParseFnResult) {.as.stmt = AS_STMT_RETURN(stmt),
                          .is_ok = true,
                          .msg = NULL,
                          .span = stmt.span};
}

static ParseFnResult break_statement(Parser *parser)
{
  TokenResult break_result = consume(parser, TOKEN_BREAK);
  if (!break_result.is_ok) {
    return (ParseFnResult) {.is_ok = false,
                            .as.stmt = {0},
                            .msg = strdup("Expected 'break' token."),
                            .span = parser->current.span};
  }

  TokenResult semicolon_result = consume(parser, TOKEN_SEMICOLON);
  if (!semicolon_result.is_ok) {
    return (ParseFnResult) {
        .is_ok = false,
        .as.stmt = {0},
        .msg = strdup("Expected ';' after 'break' statement."),
        .span = (Span) {.line = parser->previous.span.line,
                        .start = parser->previous.span.end,
                        .end = parser->previous.span.end}};
  }

  StmtBreak stmt = {.label = NULL,
                    .span = (Span) {.line = break_result.token.span.line,
                                    .start = break_result.token.span.start,
                                    .end = semicolon_result.token.span.end}};

  return (ParseFnResult) {.as.stmt = AS_STMT_BREAK(stmt),
                          .is_ok = true,
                          .msg = NULL,
                          .span = stmt.span};
}

static ParseFnResult continue_statement(Parser *parser)
{
  TokenResult continue_result = consume(parser, TOKEN_CONTINUE);
  if (!continue_result.is_ok) {
    return (ParseFnResult) {.is_ok = false,
                            .as.stmt = {0},
                            .msg = strdup("Expected 'continue' token."),
                            .span = parser->current.span};
  }

  TokenResult semicolon_result = consume(parser, TOKEN_SEMICOLON);
  if (!semicolon_result.is_ok) {
    return (ParseFnResult) {
        .is_ok = false,
        .as.stmt = {0},
        .msg = strdup("Expected ';' after 'continue' statement."),
        .span = (Span) {.line = parser->previous.span.line,
                        .start = parser->previous.span.end,
                        .end = parser->previous.span.end}};
  }

  StmtContinue stmt = {
      .label = NULL,
      .span = (Span) {.line = continue_result.token.span.line,
                      .start = continue_result.token.span.start,
                      .end = semicolon_result.token.span.end}};

  return (ParseFnResult) {.as.stmt = AS_STMT_CONTINUE(stmt),
                          .is_ok = true,
                          .msg = NULL,
                          .span = stmt.span};
}

static ParseFnResult struct_statement(Parser *parser)
{
  Token name =
      CONSUME(parser, TOKEN_IDENTIFIER, "Expected identifier after 'struct'.");
  CONSUME(parser, TOKEN_LEFT_BRACE, "Expected '{' after 'struct'.");

  DynArray_char_ptr properties = {0};
  do {
    TokenResult identifier_result = consume(parser, TOKEN_IDENTIFIER);
    if (!identifier_result.is_ok) {
      for (size_t i = 0; i < properties.count; i++) {
        free(properties.data[i]);
      }
      dynarray_free(&properties);

      return (ParseFnResult) {.is_ok = false,
                              .as.stmt = {0},
                              .msg = strdup("Expected property name.")};
    }

    Token property = identifier_result.token;

    TokenResult semicolon_result = consume(parser, TOKEN_SEMICOLON);
    if (!semicolon_result.is_ok) {
      for (size_t i = 0; i < properties.count; i++) {
        free(properties.data[i]);
      }
      dynarray_free(&properties);

      return (ParseFnResult) {
          .is_ok = false,
          .as.stmt = {0},
          .msg = strdup("Expected semicolon after property name.")};
    }

    dynarray_insert(&properties, own_string_n(property.start, property.length));
  } while (!match(parser, 1, TOKEN_RIGHT_BRACE));

  StmtStruct stmt = {
      .name = own_string_n(name.start, name.length),
      .properties = properties,
  };
  return (ParseFnResult) {
      .as.stmt = AS_STMT_STRUCT(stmt), .is_ok = true, .msg = NULL};
}

static ParseFnResult impl_statement(Parser *parser)
{
  Token name =
      CONSUME(parser, TOKEN_IDENTIFIER, "Expected identifier after 'impl'.");
  CONSUME(parser, TOKEN_LEFT_BRACE,
          "Expected '{' after identifier in 'impl' statement.");

  DynArray_Stmt methods = {0};
  while (!match(parser, 1, TOKEN_RIGHT_BRACE)) {
    ParseFnResult stmt_result = statement(parser);
    if (!stmt_result.is_ok) {
      free_ast(&methods);
      return stmt_result;
    }
    dynarray_insert(&methods, stmt_result.as.stmt);
  }

  StmtImpl stmt = {
      .name = own_string_n(name.start, name.length),
      .methods = methods,
  };
  return (ParseFnResult) {
      .as.stmt = AS_STMT_IMPL(stmt), .is_ok = true, .msg = NULL};
}

static ParseFnResult yield_statement(Parser *parser)
{
  TokenResult yield_result = consume(parser, TOKEN_YIELD);
  if (!yield_result.is_ok) {
    return (ParseFnResult) {.is_ok = false,
                            .as.stmt = {0},
                            .msg = strdup("Expected 'yield' token."),
                            .span = parser->current.span};
  }

  ParseFnResult expr_result = expression(parser);
  if (!expr_result.is_ok) {
    return expr_result;
  }

  Expr expr = expr_result.as.expr;

  TokenResult semicolon_result = consume(parser, TOKEN_SEMICOLON);
  if (!semicolon_result.is_ok) {
    free_expr(&expr);
    return (ParseFnResult) {
        .is_ok = false,
        .as.stmt = {0},
        .msg = strdup("Expected ';' after 'yield' statement."),
        .span = (Span) {.line = yield_result.token.span.line,
                        .start = yield_result.token.span.start,
                        .end = parser->previous.span.end}};
  }

  StmtYield stmt = {.expr = expr};
  return (ParseFnResult) {
      .as.stmt = AS_STMT_YIELD(stmt), .is_ok = true, .msg = NULL};
}

static ParseFnResult assert_statement(Parser *parser)
{
  TokenResult assert_result = consume(parser, TOKEN_ASSERT);
  if (!assert_result.is_ok) {
    return (ParseFnResult) {.is_ok = false,
                            .as.stmt = {0},
                            .msg = strdup("Expected 'assert' token."),
                            .span = parser->current.span};
  }

  ParseFnResult expr_result = expression(parser);
  if (!expr_result.is_ok) {
    return expr_result;
  }

  Expr expr = expr_result.as.expr;

  TokenResult semicolon_result = consume(parser, TOKEN_SEMICOLON);
  if (!semicolon_result.is_ok) {
    free_expr(&expr);
    return (ParseFnResult) {
        .is_ok = false,
        .as.stmt = {0},
        .msg = strdup("Expected ';' after 'assert' statement."),
        .span = (Span) {.line = assert_result.token.span.line,
                        .start = assert_result.token.span.start,
                        .end = parser->previous.span.end}};
  }

  StmtAssert stmt = {.expr = expr,
                     .span = (Span) {.line = assert_result.token.span.line,
                                     .start = assert_result.token.span.start,
                                     .end = semicolon_result.token.span.end}};
  return (ParseFnResult) {.as.stmt = AS_STMT_ASSERT(stmt),
                          .is_ok = true,
                          .msg = NULL,
                          .span = stmt.span};
}

static ParseFnResult statement(Parser *parser)
{
  if (check(parser, TOKEN_PRINT)) {
    return print_statement(parser);
  } else if (check(parser, TOKEN_LET)) {
    return let_statement(parser);
  } else if (check(parser, TOKEN_LEFT_BRACE)) {
    return block(parser);
  } else if (check(parser, TOKEN_IF)) {
    return if_statement(parser);
  } else if (check(parser, TOKEN_WHILE)) {
    return while_statement(parser);
  } else if (check(parser, TOKEN_FOR)) {
    return for_statement(parser);
  } else if (check(parser, TOKEN_BREAK)) {
    return break_statement(parser);
  } else if (check(parser, TOKEN_CONTINUE)) {
    return continue_statement(parser);
  } else if (check(parser, TOKEN_FN)) {
    return function_statement(parser);
  } else if (check(parser, TOKEN_RETURN)) {
    return return_statement(parser);
  } else if (match(parser, 1, TOKEN_STRUCT)) {
    return struct_statement(parser);
  } else if (match(parser, 1, TOKEN_AT)) {
    return decorator_statement(parser);
  } else if (match(parser, 1, TOKEN_IMPL)) {
    return impl_statement(parser);
  } else if (check(parser, TOKEN_YIELD)) {
    return yield_statement(parser);
  } else if (check(parser, TOKEN_ASSERT)) {
    return assert_statement(parser);
  } else {
    return expression_statement(parser);
  }
}

ParseResult parse(Parser *parser)
{
  ParseResult result = {
      .is_ok = true, .errcode = 0, .msg = NULL, .ast = {0}, .span = {0}};
  advance(parser);
  while (parser->current.type != TOKEN_EOF) {
    ParseFnResult r = statement(parser);
    if (!r.is_ok) {
      free_ast(&result.ast);
      result.msg = r.msg;
      result.is_ok = false;
      result.errcode = -1;
      result.span = r.span;
      return result;
    }
    dynarray_insert(&result.ast, r.as.stmt);
  }
  result.is_ok = true;
  return result;
}
