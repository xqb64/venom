#include <stdio.h>
#include <stdlib.h>
#include "object.h"
#include "table.h"

void print_table(Table *table) {
    for (size_t i = 0; i < sizeof(table->data) / sizeof(table->data[0]); i++) {
        if (table->data[i] != NULL) {
            printf(" %s: ", table->data[i]->key);
            print_object(&table->data[i]->obj);
            printf(", ");
        }
    }
}

void print_object(Object *object) {
    printf("{ ");
    if IS_BOOL(object) {
        printf("%s", TO_BOOL(*object) ? "true" : "false");
    } else if IS_NUM(object) {
        printf("%.2f", TO_DOUBLE(*object));
    } else if IS_FUNC(object) {
        printf("<fn %s", TO_FUNC(*object).name);
        printf(" @ %d>", TO_FUNC(*object).location);
    } else if IS_POINTER(object) {
        printf("PTR ('%p')", TO_PTR(*object));
    } else if IS_NULL(object) {
        printf("null");
    } else if IS_STRING(object) {
        printf("%s", TO_STR(*object));
    } else if IS_PROP(object) {
        printf("prop: %s", TO_PROP(*object));
    } else if IS_HEAP(object) {
        if (IS_STRUCT(TO_HEAP(*object)->obj)) {
            printf("%s", TO_STRUCT(*object).name);
            printf(" {");
            print_table(TO_STRUCT(*object).properties);
            printf(" }");
        }
    }
    printf(" }");
}

inline Object *ALLOC(Object object) {
    Object *obj = malloc(sizeof(Object));
    *obj = object;
    return obj;
}

inline void DEALLOC(Object *object) {
    if (IS_STRUCT(object)) {
        table_free(object->as.struct_.properties);
        free(object->as.struct_.properties);
    }
    free(object);
}
