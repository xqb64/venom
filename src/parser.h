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
} Parser;

void init_parser(Parser *parser, DynArray_Token *tokens);

DynArray_Stmt parse(Parser *parser);

#endif
