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
        printf("%s", object->as.bval ? "true" : "false");
    } else if IS_NUM(object) {
        printf("%.2f", object->as.dval);
    } else if IS_FUNC(object) {
        printf("<fn %s", object->as.func.name);    
        printf(" @ %d>", object->as.func.location);   
    } else if IS_POINTER(object) {
        printf("PTR ('%d')", *object->as.ptr);
    } else if IS_NULL(object) {
        printf("null");
    } else if IS_STRING(object) {
        printf("%s", object->as.str);
    } else if IS_PROP(object) {
        printf("%s", object->as.prop);
    } else if IS_HEAP(object) {
        if (IS_STRUCT(object->as.heapobj->obj)) {
            printf("%s", object->as.heapobj->obj->as.struct_.name);
            printf(" {");
            print_table(object->as.heapobj->obj->as.struct_.properties);
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
