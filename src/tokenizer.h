#ifndef venom_tokenizer_h
#define venom_tokenizer_h

#include <stdbool.h>

#include "dynarray.h"

typedef enum {
  TOKEN_PRINT,
  TOKEN_LET,
  TOKEN_IDENTIFIER,
  TOKEN_NUMBER,
  TOKEN_STRING,
  TOKEN_STRUCT,
  TOKEN_LEFT_PAREN,
  TOKEN_RIGHT_PAREN,
  TOKEN_LEFT_BRACE,
  TOKEN_RIGHT_BRACE,
  TOKEN_LEFT_BRACKET,
  TOKEN_RIGHT_BRACKET,
  TOKEN_QUOTE,
  TOKEN_STAR,
  TOKEN_STAR_EQUAL,
  TOKEN_SLASH,
  TOKEN_SLASH_EQUAL,
  TOKEN_PLUS,
  TOKEN_PLUS_EQUAL,
  TOKEN_MINUS,
  TOKEN_MINUS_EQUAL,
  TOKEN_MOD,
  TOKEN_MOD_EQUAL,
  TOKEN_DOT,
  TOKEN_AT,
  TOKEN_ARROW,
  TOKEN_COMMA,
  TOKEN_COLON,
  TOKEN_SEMICOLON,
  TOKEN_BANG,
  TOKEN_GREATER,
  TOKEN_GREATER_GREATER,
  TOKEN_GREATER_GREATER_EQUAL,
  TOKEN_LESS,
  TOKEN_LESS_LESS,
  TOKEN_LESS_LESS_EQUAL,
  TOKEN_GREATER_EQUAL,
  TOKEN_LESS_EQUAL,
  TOKEN_EQUAL,
  TOKEN_DOUBLE_EQUAL,
  TOKEN_BANG_EQUAL,
  TOKEN_AMPERSAND,
  TOKEN_AMPERSAND_EQUAL,
  TOKEN_DOUBLE_AMPERSAND,
  TOKEN_PIPE,
  TOKEN_PIPE_EQUAL,
  TOKEN_DOUBLE_PIPE,
  TOKEN_CARET,
  TOKEN_CARET_EQUAL,
  TOKEN_TILDE,
  TOKEN_QUESTION,
  TOKEN_PLUSPLUS,
  TOKEN_IF,
  TOKEN_ELSE,
  TOKEN_DO,
  TOKEN_WHILE,
  TOKEN_FOR,
  TOKEN_BREAK,
  TOKEN_CONTINUE,
  TOKEN_GOTO,
  TOKEN_IMPL,
  TOKEN_FN,
  TOKEN_RETURN,
  TOKEN_TRUE,
  TOKEN_FALSE,
  TOKEN_NULL,
  TOKEN_YIELD,
  TOKEN_ASSERT,
  TOKEN_ERROR,
  TOKEN_EOF,
} TokenType;

typedef struct {
  size_t start;
  size_t end;
  size_t line;
} Span;

typedef struct {
  const char *start;
  TokenType type;
  int length;
  Span span;
} Token;

typedef struct {
  char *src;
  char *current;
  size_t line;
} Tokenizer;

typedef DynArray(Token) DynArray_Token;

typedef struct {
  DynArray_Token tokens;
  int errcode;
  bool is_ok;
  char *msg;
  Span span;
  double time;
} TokenizeResult;

void init_tokenizer(Tokenizer *tokenizer, char *source);
TokenizeResult tokenize(Tokenizer *tokenizer);
void print_token(const Token *token);
void print_tokens(const DynArray_Token *tokens);

#endif
