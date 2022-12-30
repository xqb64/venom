#include <stdio.h>
#include <stdlib.h>
#include "object.h"
#include "table.h"

inline Object *ALLOC(Object object) {
    Object *obj = malloc(sizeof(Object));
    *obj = object;
    return obj;
}

inline void DEALLOC(Object *object) {
    if (IS_STRUCT(object)) {
        table_free(object->as.struct_.properties);
        free(object->as.struct_.properties);
        free(object);
    }
}
