#ifndef venom_semantics_h
#define venom_semantics_h

#include "ast.h"

typedef struct {
  DynArray_Stmt ast;
  bool is_ok;
  char *msg;
} LoopLabelResult;

LoopLabelResult loop_label_program(DynArray_Stmt *ast, const char *current);
LoopLabelResult loop_label_stmt(Stmt *stmt, const char *current);

#endif
