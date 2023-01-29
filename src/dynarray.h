#ifndef venom_dynarray_h
#define venom_dynarray_h

#include <assert.h>

#define DynArray(T) struct { \
    T *data; \
    size_t count; \
    size_t capacity; \
}

#define dynarray_insert(array, exp) \
do { \
    if ((array)->count >= (array)->capacity) { \
        (array)->capacity = (array)->capacity == 0 ? 2 : (array)->capacity * 2; \
        (array)->data = realloc((array)->data, sizeof((array)->data[0]) * (array)->capacity); \
    } \
    (array)->data[(array)->count++] = (exp); \
} while (0)

#define dynarray_free(array) \
do { \
    free((array)->data); \
} while (0)

#define dynarray_pop(array) \
    (assert((array)->count > 0), (array)->data[--(array)->count])

#define dynarray_peek(array) ((array)->data[(array)->count-1])

typedef DynArray(char *) DynArray_char_ptr;
typedef DynArray(int) DynArray_int;

char *strcat_dynarray(DynArray_char_ptr array);

#define COPY_DYNARRAY(dest, src) \
do { \
    for (size_t i = 0; i < (src)->count; i++) { \
        dynarray_insert((dest), (src)->data[i]); \
    } \
} while (0)

#endif