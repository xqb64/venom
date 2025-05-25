#ifndef venom_err_h
#define venom_err_h

#include <stddef.h>

#include "tokenizer.h"

char *mkerrctx(const char *source, Span *span, size_t before, size_t after);

#endif
