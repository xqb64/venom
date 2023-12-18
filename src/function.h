#ifndef VENOM_FUNCTION_H
#define VENOM_FUNCTION_H

#include "table.h"
#include <stddef.h>

typedef struct {
  char *name;
  size_t location;
  size_t paramcount;
} Function;

typedef Table(Function) Table_Function;

#endif