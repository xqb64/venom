#ifndef venom_optimizer_h
#define venom_optimizer_h

#include <stdbool.h>

#include "ast.h"

typedef struct {
  DynArray_Stmt payload;
  bool is_ok;
  int errcode;
  char *msg;
  double time;
} OptimizeResult;

OptimizeResult optimize(const DynArray_Stmt *ast);

#endif
