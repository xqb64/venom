#ifndef venom_parser_h
#define venom_parser_h

#include <stddef.h>

#include "ast.h"
#include "tokenizer.h"

typedef void *Resource;
typedef DynArray(Resource) DynArray_Resource;

typedef struct {
  Token current;
  Token previous;
  size_t depth;
  const DynArray_Token *tokens;
  size_t idx;
} Parser;

typedef struct {
  Token token;
  bool is_ok;
} TokenResult;

typedef struct {
  union {
    Expr expr;
    Stmt stmt;
  } as;
  bool is_ok;
  char *msg;
} ParseFnResult;

typedef struct {
  DynArray_Stmt ast;
  bool is_ok;
  char *msg;
} ParseResult;

void init_parser(Parser *parser, const DynArray_Token *tokens);
ParseResult parse(Parser *parser);
void free_parse_result(const ParseResult *result);

#endif
