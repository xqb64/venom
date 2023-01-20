#ifndef venom_util_h
#define venom_util_h

#include <string.h>
#include <stdlib.h>

char *own_string(const char *string);
char *own_string_n(const char *string, int n);
char *read_file(const char *path);

/* The purpose of the ALLOC macro is to take an
 * existing object, put it on the heap and ret-
 * urn a pointer to the newly allocated object.
 * This is accomplished by (ab)using the return
 * value of memcpy which is the pointer to dest. */
#define ALLOC(obj) (memcpy(malloc(sizeof((obj))), &(obj), sizeof((obj))))

#endif