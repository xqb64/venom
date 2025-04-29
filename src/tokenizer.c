#include "tokenizer.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void init_tokenizer(Tokenizer *tokenizer, char *source)
{
    tokenizer->current = source;
    tokenizer->line = 1;
}

static char peek(Tokenizer *tokenizer, int distance)
{
    return tokenizer->current[distance];
}

static bool match(Tokenizer *tokenizer, char target)
{
    if (peek(tokenizer, 0) == target)
    {
        tokenizer->current++;
        return true;
    }
    return false;
}

static char advance(Tokenizer *tokenizer)
{
    return *tokenizer->current++;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_alnum(char c)
{
    return is_alpha(c) || is_digit(c);
}

static bool is_at_end(Tokenizer *tokenizer)
{
    return peek(tokenizer, 0) == '\0';
}

static Token make_token(Tokenizer *tokenizer, TokenType type, int length)
{
    return (Token) {
        .type = type,
        .start = tokenizer->current - length,
        .length = length,
    };
}

static void print_token(Token *token)
{
    printf("%.*s", token->length, token->start);
}

static TokenType check_keyword(Tokenizer *tokenizer, int start_pos, int length)
{
    char *lexeme = tokenizer->current - length;

    if (strncmp(lexeme, "fn", 2) == 0 && length == 2)
        return TOKEN_FN;
    if (strncmp(lexeme, "let", 3) == 0 && length == 3)
        return TOKEN_LET;
    if (strncmp(lexeme, "if", 2) == 0 && length == 2)
        return TOKEN_IF;
    if (strncmp(lexeme, "else", 4) == 0 && length == 4)
        return TOKEN_ELSE;
    if (strncmp(lexeme, "for", 3) == 0 && length == 3)
        return TOKEN_FOR;
    if (strncmp(lexeme, "while", 5) == 0 && length == 5)
        return TOKEN_WHILE;
    if (strncmp(lexeme, "return", 6) == 0 && length == 6)
        return TOKEN_RETURN;
    if (strncmp(lexeme, "print", 5) == 0 && length == 5)
        return TOKEN_PRINT;
    if (strncmp(lexeme, "yield", 5) == 0 && length == 5)
        return TOKEN_YIELD;
    if (strncmp(lexeme, "break", 5) == 0 && length == 5)
        return TOKEN_BREAK;
    if (strncmp(lexeme, "continue", 8) == 0 && length == 8)
        return TOKEN_CONTINUE;
    if (strncmp(lexeme, "struct", 6) == 0 && length == 6)
        return TOKEN_STRUCT;
    if (strncmp(lexeme, "use", 3) == 0 && length == 3)
        return TOKEN_USE;
    if (strncmp(lexeme, "impl", 4) == 0 && length == 4)
        return TOKEN_IMPL;
    if (strncmp(lexeme, "true", 4) == 0 && length == 4)
        return TOKEN_TRUE;
    if (strncmp(lexeme, "false", 5) == 0 && length == 5)
        return TOKEN_FALSE;
    if (strncmp(lexeme, "assert", 6) == 0 && length == 6)
        return TOKEN_ASSERT;
    if (strncmp(lexeme, "null", 4) == 0 && length == 4)
        return TOKEN_NULL;

    return TOKEN_IDENTIFIER;
}

typedef enum
{
    STATE_START,
    STATE_NUMBER,
    STATE_STRING,
    STATE_IDENTIFIER,
    STATE_DONE,
} TokenizerState;

static Token get_token(Tokenizer *tokenizer)
{
    TokenizerState state = STATE_START;
    int length = 0;

    while (state != STATE_DONE)
    {
        switch (state)
        {
            case STATE_START: {
                length = 0;

                if (is_at_end(tokenizer))
                {
                    state = STATE_DONE;
                    return make_token(tokenizer, TOKEN_EOF, 0);
                }

                char c = advance(tokenizer);
                length++;

                if (c == ' ' || c == '\t' || c == '\r')
                {
                    length = 0;
                    state = STATE_START;
                    break;
                }

                if (c == '\n')
                {
                    tokenizer->line++;
                    length = 0;
                    state = STATE_START;
                    break;
                }

                if (is_alpha(c) || c == '_')
                {
                    state = STATE_IDENTIFIER;
                    break;
                }

                if (is_digit(c))
                {
                    state = STATE_NUMBER;
                    break;
                }

                if (c == '"')
                {
                    length = 0;
                    state = STATE_STRING;
                    break;
                }

                switch (c)
                {
                    case '(':
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_LEFT_PAREN, 1);
                    case ')':
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_RIGHT_PAREN, 1);
                    case '{':
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_LEFT_BRACE, 1);
                    case '}':
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_RIGHT_BRACE, 1);
                    case '[':
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_LEFT_BRACKET, 1);
                    case ']':
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_RIGHT_BRACKET, 1);
                    case ';':
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_SEMICOLON, 1);
                    case ',':
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_COMMA, 1);
                    case '.':
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_DOT, 1);
                    case ':':
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_COLON, 1);
                    case '@':
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_AT, 1);
                    case '~':
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_TILDE, 1);

                    case '=':
                        if (match(tokenizer, '='))
                        {
                            state = STATE_DONE;
                            return make_token(tokenizer, TOKEN_DOUBLE_EQUAL, 2);
                        }
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_EQUAL, 1);

                    case '<':
                        if (match(tokenizer, '='))
                        {
                            state = STATE_DONE;
                            return make_token(tokenizer, TOKEN_LESS_EQUAL, 2);
                        }
                        else if (match(tokenizer, '<'))
                        {
                            if (match(tokenizer, '='))
                            {
                                state = STATE_DONE;
                                return make_token(tokenizer, TOKEN_LESS_LESS_EQUAL, 3);
                            }
                            state = STATE_DONE;
                            return make_token(tokenizer, TOKEN_LESS_LESS, 2);
                        }
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_LESS, 1);

                    case '>':
                        if (match(tokenizer, '='))
                        {
                            state = STATE_DONE;
                            return make_token(tokenizer, TOKEN_GREATER_EQUAL, 2);
                        }
                        else if (match(tokenizer, '>'))
                        {
                            if (match(tokenizer, '='))
                            {
                                state = STATE_DONE;
                                return make_token(tokenizer, TOKEN_GREATER_GREATER_EQUAL, 3);
                            }
                            state = STATE_DONE;
                            return make_token(tokenizer, TOKEN_GREATER_GREATER, 2);
                        }
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_GREATER, 1);

                    case '!':
                        if (match(tokenizer, '='))
                        {
                            state = STATE_DONE;
                            return make_token(tokenizer, TOKEN_BANG_EQUAL, 2);
                        }
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_BANG, 1);

                    case '+':
                        if (match(tokenizer, '+'))
                        {
                            state = STATE_DONE;
                            return make_token(tokenizer, TOKEN_PLUSPLUS, 2);
                        }
                        else if (match(tokenizer, '='))
                        {
                            state = STATE_DONE;
                            return make_token(tokenizer, TOKEN_PLUS_EQUAL, 2);
                        }
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_PLUS, 1);

                    case '-': {
                        if (match(tokenizer, '='))
                        {
                            state = STATE_DONE;
                            return make_token(tokenizer, TOKEN_MINUS_EQUAL, 2);
                        }
                        else if (match(tokenizer, '>'))
                        {
                            state = STATE_DONE;
                            return make_token(tokenizer, TOKEN_ARROW, 2);
                        }
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_MINUS, 1);
                    }
                    case '*':
                        if (match(tokenizer, '='))
                        {
                            state = STATE_DONE;
                            return make_token(tokenizer, TOKEN_STAR_EQUAL, 2);
                        }
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_STAR, 1);

                    case '/':
                        if (match(tokenizer, '='))
                        {
                            state = STATE_DONE;
                            return make_token(tokenizer, TOKEN_SLASH_EQUAL, 2);
                        }
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_SLASH, 1);

                    case '%':
                        if (match(tokenizer, '='))
                        {
                            state = STATE_DONE;
                            return make_token(tokenizer, TOKEN_MOD_EQUAL, 2);
                        }
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_MOD, 1);

                    case '&':
                        if (match(tokenizer, '&'))
                        {
                            state = STATE_DONE;
                            return make_token(tokenizer, TOKEN_DOUBLE_AMPERSAND, 2);
                        }
                        else if (match(tokenizer, '='))
                        {
                            state = STATE_DONE;
                            return make_token(tokenizer, TOKEN_AMPERSAND_EQUAL, 2);
                        }
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_AMPERSAND, 1);

                    case '|':
                        if (match(tokenizer, '|'))
                        {
                            state = STATE_DONE;
                            return make_token(tokenizer, TOKEN_DOUBLE_PIPE, 2);
                        }
                        else if (match(tokenizer, '='))
                        {
                            state = STATE_DONE;
                            return make_token(tokenizer, TOKEN_PIPE_EQUAL, 2);
                        }
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_PIPE, 1);

                    case '^':
                        if (match(tokenizer, '='))
                        {
                            state = STATE_DONE;
                            return make_token(tokenizer, TOKEN_CARET_EQUAL, 2);
                        }
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_CARET, 1);

                    default:
                        state = STATE_DONE;
                        return make_token(tokenizer, TOKEN_ERROR, 0);
                }

                break;
            }

            case STATE_IDENTIFIER:
                if (is_alnum(peek(tokenizer, 0)) || peek(tokenizer, 0) == '_')
                {
                    advance(tokenizer);
                    length++;
                }
                else
                {
                    state = STATE_DONE;
                    TokenType type = check_keyword(tokenizer, 0, length);
                    return make_token(tokenizer, type, length);
                }
                break;

            case STATE_NUMBER:
                if (is_digit(peek(tokenizer, 0)))
                {
                    advance(tokenizer);
                    length++;
                }
                else if (peek(tokenizer, 0) == '.' && is_digit(peek(tokenizer, 1)))
                {
                    advance(tokenizer);
                    length++;
                    while (is_digit(peek(tokenizer, 0)))
                    {
                        advance(tokenizer);
                        length++;
                    }
                    state = STATE_DONE;
                    return make_token(tokenizer, TOKEN_NUMBER, length);
                }
                else
                {
                    state = STATE_DONE;
                    return make_token(tokenizer, TOKEN_NUMBER, length);
                }
                break;

            case STATE_STRING:
                if (peek(tokenizer, 0) == '"')
                {
                    advance(tokenizer);
                    state = STATE_DONE;
                    return make_token(tokenizer, TOKEN_STRING, length + 1);
                }
                else
                {
                    advance(tokenizer);
                    length++;
                }
                break;

            case STATE_DONE:
                break;

            default:
                break;
        }
    }
    return make_token(tokenizer, TOKEN_ERROR, 0);
}

void print_tokens(DynArray_Token *tokens)
{
    printf("[");
    for (size_t i = 0; i < tokens->count; i++)
    {
        print_token(&tokens->data[i]);
        if (i < tokens->count - 1)
            printf(", ");
    }
    printf("]\n");
}

TokenizeResult tokenize(Tokenizer *tokenizer)
{
    TokenizeResult result = {0};

    Token t;
    while ((t = get_token(tokenizer)).type != TOKEN_EOF)
    {
        if (t.type == TOKEN_ERROR)
        {
            result.msg = "Error at line X";
            result.is_ok = false;
            return result;
        }
        dynarray_insert(&result.tokens, t);
    }

    result.is_ok = true;

    return result;
}
