#ifndef venom_tokenizer_h
#define venom_tokenizer_h

typedef enum
{
    TOKEN_PRINT,
    TOKEN_LET,
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_STRUCT,
    TOKEN_USE,
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
    TOKEN_PLUSPLUS,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_FOR,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
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

typedef struct
{
    char *start;
    TokenType type;
    int length;
} Token;

typedef struct
{
    char *current;
    int line;
} Tokenizer;

void init_tokenizer(Tokenizer *tokenizer, char *source);
void print_token(Token *token);
Token get_token(Tokenizer *tokenizer);

#endif