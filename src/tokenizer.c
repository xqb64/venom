#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "tokenizer.h"

void init_tokenizer(Tokenizer *tokenizer, char *source) {
    tokenizer->current = source;
    tokenizer->line = 1;
}

static char peek(Tokenizer *tokenizer, int distance) {
    return tokenizer->current[distance];
}

static char advance(Tokenizer *tokenizer) {
    return *tokenizer->current++;
}

static void tokenizing_error(int line) {
    fprintf(stderr, "tokenizing error at line %d\n", line);
    exit(1);
}

static void skip_whitespace(Tokenizer *tokenizer) {
    for (;;) {
        switch (peek(tokenizer, 0)) {
            case ' ':
            case '\r':
            case '\t':
                advance(tokenizer);
                break;
            case '\n':
                tokenizer->line++;
                advance(tokenizer);
                break;
            default: return;
        }
    }
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_at_end(Tokenizer *tokenizer) {
    return peek(tokenizer, 0) == '\0';
}

static bool lookahead(Tokenizer *tokenizer, int length, char *rest) {
    if (strncmp(tokenizer->current, rest, length) == 0) {
        for (int i = 0; i < length; i++) {
            advance(tokenizer);
        }
        return true;
    }
    return false;
}

static Token make_token(Tokenizer *tokenizer, TokenType type, int length) {
    return (Token){
        .type = type,
        .start = tokenizer->current - length,
        .length = length,
    };
}

static Token number(Tokenizer *tokenizer) {
    int length = 0;
    while (is_digit(peek(tokenizer, 0))) {
        advance(tokenizer);
        ++length;
    }
    if (peek(tokenizer, 0) == '.' && is_digit(peek(tokenizer, 1))) {
        advance(tokenizer);
        ++length;

        while (is_digit(peek(tokenizer, 0))) {
            advance(tokenizer);
            ++length;
        }
    }
    return make_token(tokenizer, TOKEN_NUMBER, length + 1);
}

static Token string(Tokenizer *tokenizer) {
    int length = 0;
    while (peek(tokenizer, 0) != '"' && !is_at_end(tokenizer)) {
        advance(tokenizer);
        ++length;
    }
    if (is_at_end(tokenizer)) {
        tokenizing_error(tokenizer->line);
    }
    advance(tokenizer);
    ++length;
    return make_token(tokenizer, TOKEN_STRING, length);
}

static Token identifier(Tokenizer *tokenizer) {
    int length = 0;
    while (is_digit(peek(tokenizer, 0)) || is_alpha(peek(tokenizer, 0))) {
        advance(tokenizer);
        ++length;
    }
    return make_token(tokenizer, TOKEN_IDENTIFIER, length + 1);
}

void print_token(Token token) {
    printf("current token: Token { .type: ");

    switch (token.type) {
        case TOKEN_PRINT: printf("TOKEN_PRINT"); break;
        case TOKEN_LET: printf("TOKEN_LET"); break;
        case TOKEN_IDENTIFIER: printf("TOKEN_IDENTIFIER"); break;
        case TOKEN_NUMBER: printf("TOKEN_NUMBER"); break;
        case TOKEN_LEFT_PAREN: printf("TOKEN_LEFT_PAREN"); break;
        case TOKEN_RIGHT_PAREN: printf("TOKEN_RIGHT_PAREN"); break;
        case TOKEN_LEFT_BRACE: printf("TOKEN_LEFT_BRACE"); break;
        case TOKEN_RIGHT_BRACE: printf("TOKEN_RIGHT_BRACE"); break;
        case TOKEN_STAR: printf("TOKEN_STAR"); break;
        case TOKEN_SLASH: printf("TOKEN_SLASH"); break;
        case TOKEN_PLUS: printf("TOKEN_PLUS"); break;
        case TOKEN_MINUS: printf("TOKEN_MINUS"); break;
        case TOKEN_DOT: printf("TOKEN_DOT"); break;
        case TOKEN_SEMICOLON: printf("TOKEN_SEMICOLON"); break;
        case TOKEN_EQUAL: printf("TOKEN_EQUAL"); break;
        case TOKEN_DOUBLE_EQUAL: printf("TOKEN_DOUBLE_EQUAL"); break;
        case TOKEN_IF: printf("TOKEN_IF"); break;
        case TOKEN_ELSE: printf("TOKEN_ELSE"); break;
        case TOKEN_WHILE: printf("TOKEN_WHILE"); break;
        case TOKEN_FN: printf("TOKEN_FN"); break;
        case TOKEN_STRUCT: printf("TOKEN_STRUCT"); break;
        case TOKEN_RETURN: printf("TOKEN_RETURN"); break;
        case TOKEN_EOF: printf("TOKEN_EOF"); break;
        default: break;
    }

    printf(", value: '%.*s' }\n", token.length, token.start);
}

Token get_token(Tokenizer *tokenizer) {
    skip_whitespace(tokenizer);

    if (is_at_end(tokenizer)) return make_token(tokenizer, TOKEN_EOF, 0);

    char c = advance(tokenizer);

    if (is_digit(c)) return number(tokenizer);
    switch (c) {
        case '+': return make_token(tokenizer, TOKEN_PLUS, 1);
        case '-': {
            if (lookahead(tokenizer, 1, ">")) {
                return make_token(tokenizer, TOKEN_ARROW, 2);
            }
            return make_token(tokenizer, TOKEN_MINUS, 1);
        }
        case '*': return make_token(tokenizer, TOKEN_STAR, 1);
        case '/': return make_token(tokenizer, TOKEN_SLASH, 1);
        case '(': return make_token(tokenizer, TOKEN_LEFT_PAREN, 1);
        case ')': return make_token(tokenizer, TOKEN_RIGHT_PAREN, 1);
        case '{': return make_token(tokenizer, TOKEN_LEFT_BRACE, 1);
        case '}': return make_token(tokenizer, TOKEN_RIGHT_BRACE, 1);
        case ':': return make_token(tokenizer, TOKEN_COLON, 1);
        case ';': return make_token(tokenizer, TOKEN_SEMICOLON, 1);
        case ',': return make_token(tokenizer, TOKEN_COMMA, 1);
        case '%': return make_token(tokenizer, TOKEN_MOD, 1);
        case '.': return make_token(tokenizer, TOKEN_DOT, 1);
        case '"': return string(tokenizer);
        case '>': {
            if (lookahead(tokenizer, 1, "=")) {
                return make_token(tokenizer, TOKEN_GREATER_EQUAL, 2);
            }
            return make_token(tokenizer, TOKEN_GREATER, 1);
        }
        case '<': {
            if (lookahead(tokenizer, 1, "=")) {
                return make_token(tokenizer, TOKEN_LESS_EQUAL, 2);
            }
            return make_token(tokenizer, TOKEN_LESS, 1);
        }
        case '!': {
            if (lookahead(tokenizer, 1, "=")) {
                return make_token(tokenizer, TOKEN_BANG_EQUAL, 2);
            }
            return make_token(tokenizer, TOKEN_BANG, 1);
        }
        case '=': {
            if (lookahead(tokenizer, 1, "=")) {
                return make_token(tokenizer, TOKEN_DOUBLE_EQUAL, 2);
            }
            return make_token(tokenizer, TOKEN_EQUAL, 1);
        }
        case '&': {
            if (lookahead(tokenizer, 1, "&")) {
                return make_token(tokenizer, TOKEN_DOUBLE_AMPERSAND, 2);
            }
            return make_token(tokenizer, TOKEN_AMPERSAND, 1);
        }
        case '|': {
            if (lookahead(tokenizer, 1, "|")) {
                return make_token(tokenizer, TOKEN_DOUBLE_PIPE, 2);
            }
            return make_token(tokenizer, TOKEN_PIPE, 1);
        }
        case 'b': {
            if (lookahead(tokenizer, 4, "reak")) {
                return make_token(tokenizer, TOKEN_BREAK, 5);
            }
            return identifier(tokenizer);
        }
        case 'c': {
            if (lookahead(tokenizer, 7, "ontinue")) {
                return make_token(tokenizer, TOKEN_CONTINUE, 8);
            }
            return identifier(tokenizer);
        }
        case 'e': {
            if (lookahead(tokenizer, 3, "lse")) {
                return make_token(tokenizer, TOKEN_ELSE, 4);
            }
            return identifier(tokenizer);
        }
        case 'f': {
            if (lookahead(tokenizer, 1, "n")) {
                return make_token(tokenizer, TOKEN_FN, 2);
            }
            if (lookahead(tokenizer, 4, "alse")) {
                return make_token(tokenizer, TOKEN_FALSE, 5);
            }
            return identifier(tokenizer);
        }
        case 'i': {
            if (lookahead(tokenizer, 1, "f")) {
                return make_token(tokenizer, TOKEN_IF, 2);
            }
            return identifier(tokenizer);
        }
        case 'l': {
            if (lookahead(tokenizer, 2, "et")) {
                return make_token(tokenizer, TOKEN_LET, 3);
            }
            return identifier(tokenizer);
        }
        case 'n': {
            if (lookahead(tokenizer, 3, "ull")) {
                return make_token(tokenizer, TOKEN_NULL, 4);
            }
            return identifier(tokenizer);
        }
        case 'p': {
            if (lookahead(tokenizer, 4, "rint")) {
                return make_token(tokenizer, TOKEN_PRINT, 5);
            }
            return identifier(tokenizer);
        }
        case 'r': {
            if (lookahead(tokenizer, 5, "eturn")) {
                return make_token(tokenizer, TOKEN_RETURN, 6);
            }
            return identifier(tokenizer);
        }
        case 's': {
            if (lookahead(tokenizer, 5, "truct")) {
                return make_token(tokenizer, TOKEN_STRUCT, 6);
            }
            return identifier(tokenizer);
        }
        case 't': {
            if (lookahead(tokenizer, 3, "rue")) {
                return make_token(tokenizer, TOKEN_TRUE, 4);
            }
            return identifier(tokenizer);
        }
        case 'w': {
            if (lookahead(tokenizer, 4, "hile")) {
                return make_token(tokenizer, TOKEN_WHILE, 5);
            }
            return identifier(tokenizer);
        }
        default: return identifier(tokenizer);
    }
}