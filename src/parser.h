#ifndef venom_parser_h
#define venom_parser_h

#include <stddef.h>

#include "ast.h"
#include "tokenizer.h"

typedef struct
{
    Token current;
    Token previous;
    size_t depth;
    DynArray_Token *tokens;
    size_t idx;
    bool error;
} Parser;

typedef struct
{
    DynArray_Stmt ast;
    bool is_ok;
    char *msg;
} ParseResult;

void init_parser(Parser *parser, DynArray_Token *tokens);
ParseResult parse(Parser *parser);

#endif
