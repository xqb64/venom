#ifndef venom_semantics_h
#define venom_semantics_h

#include <stdbool.h>

#include "ast.h"

typedef struct {
  union {
    DynArray_Stmt ast;
    Stmt stmt;
  } as;
  int errcode;
  bool is_ok;
  char *msg;
  Span span;
} LoopLabelResult;

typedef struct {
  int errcode;
  bool is_ok;
  char *msg;
  Span span;
} LabelCheckResult;

LoopLabelResult loop_label_program(DynArray_Stmt *ast, const char *current);
LabelCheckResult label_check_program(DynArray_Stmt *ast);

#endif
