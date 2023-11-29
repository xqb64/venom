#ifndef venom_tokenizer_h
#define venom_tokenizer_h

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
  TOKEN_QUOTE,
  TOKEN_STAR,
  TOKEN_SLASH,
  TOKEN_PLUS,
  TOKEN_MINUS,
  TOKEN_MOD,
  TOKEN_DOT,
  TOKEN_ARROW,
  TOKEN_COMMA,
  TOKEN_COLON,
  TOKEN_SEMICOLON,
  TOKEN_BANG,
  TOKEN_GREATER,
  TOKEN_LESS,
  TOKEN_GREATER_EQUAL,
  TOKEN_LESS_EQUAL,
  TOKEN_EQUAL,
  TOKEN_DOUBLE_EQUAL,
  TOKEN_BANG_EQUAL,
  TOKEN_AMPERSAND, /* reserved for bitwise AND */
  TOKEN_DOUBLE_AMPERSAND,
  TOKEN_PIPE, /* reserved for bitwise OR */
  TOKEN_DOUBLE_PIPE,
  TOKEN_PLUSPLUS,
  TOKEN_IF,
  TOKEN_ELSE,
  TOKEN_WHILE,
  TOKEN_BREAK,
  TOKEN_CONTINUE,
  TOKEN_FN,
  TOKEN_RETURN,
  TOKEN_TRUE,
  TOKEN_FALSE,
  TOKEN_NULL,
  TOKEN_EOF,
} TokenType;

typedef struct {
  char *start;
  TokenType type;
  int length;
} Token;

typedef struct {
  char *current;
  int line;
} Tokenizer;

void init_tokenizer(Tokenizer *tokenizer, char *source);
void print_token(Token *token);
Token get_token(Tokenizer *tokenizer);

#endif