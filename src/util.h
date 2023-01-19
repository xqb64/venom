#ifndef venom_util_h
#define venom_util_h

#include <string.h>
#include <stdlib.h>

#include "dynarray.h"

char *own_string(const char *string);
char *own_string_n(const char *string, int n);
char *strcat_dynarray(DynArray_char_ptr array);
char *read_file(const char *path);

#define ALLOC(obj) (memcpy(malloc(sizeof((obj))), &(obj), sizeof((obj))))

#endif