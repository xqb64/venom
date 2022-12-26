#include <stdio.h>
#include "object.h"

void print_object(Object *object) {
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
    } else if IS_STRUCT(object) {
        printf("%s", object->as.struct_.name);
        printf("{ ");
        for (size_t i = 0; i < 1024; i++) {
            print_object(object->as.struct_.properties->data[i]->obj);
        }
        printf(" }");
    }
}