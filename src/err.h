#ifndef venom_err_h
#define venom_err_h

#include <stddef.h>

char *mkerrctx(const char *source, size_t line, size_t span_start,
               size_t span_end, size_t before, size_t after);

#endif
