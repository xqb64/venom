#ifndef venom_util_h
#define venom_util_h

#include <stdbool.h>
#include <stddef.h>

typedef struct {
  char *payload;
  bool is_ok;
  int errcode;
  char *msg;
} ReadFileResult;

char *own_string(const char *string);
char *own_string_n(const char *string, int n);
ReadFileResult read_file(const char *path);
size_t numlen(size_t n);
size_t lblen(const char *lb, size_t n);
int alloc_err_str(char **dst, const char *fmt, ...);

/* The purpose of the ALLOC macro is to take an
 * existing object, put it on the heap and ret-
 * urn a pointer to the newly allocated object.
 * This is accomplished by (ab)using the return
 * value of memcpy which is the pointer to dest. */
#define ALLOC(obj) (memcpy(malloc(sizeof((obj))), &(obj), sizeof((obj))))

#endif
