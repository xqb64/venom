#ifndef venom_parser_h
#define venom_parser_h

#include <stdbool.h>
#include <stddef.h>

#include "ast.h"
#include "dynarray.h"
#include "tokenizer.h"

typedef struct
{
    Token current;
    Token previous;
    size_t depth;
    DynArray_Token *tokens;
    size_t idx;
} Parser;

DynArray_Stmt parse(Parser *parser);
void free_stmt(Stmt stmt);
void init_parser(Parser *parser, DynArray_Token *tokens);
void print_ast(DynArray_Stmt *stmts);
void free_parser(Parser *parser);

#endif