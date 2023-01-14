#ifndef venom_object_h
#define venom_object_h

#include <stdbool.h>
#include <stdint.h>
#include "dynarray.h"

typedef struct Table Table;
typedef struct HeapObject HeapObject;

void table_print(Table *table);

typedef DynArray(char *) DynArray_char_ptr;

typedef enum {
    OBJ_NUMBER,
    OBJ_BOOLEAN,
    OBJ_FUNCTION,
    OBJ_POINTER,
    OBJ_HEAP,
    OBJ_STRING,
    OBJ_STRUCT,
    OBJ_STRUCT_BLUEPRINT,
    OBJ_PROPERTY,
    OBJ_NULL,
} __attribute__ ((__packed__)) ObjectType;

typedef struct {
    char *name;
    size_t location;
    size_t paramcount;
} Function;

typedef struct {
    int refcount;
    Table *properties;
    int propertycount;
    char *name;
} Struct;

typedef struct {
    char *name;
    DynArray_char_ptr properties;
} StructBlueprint;

typedef struct Object {
    ObjectType type;
    union {
        char *str;
        double dval;
        bool bval;
        Function *func;
        uint8_t *ptr;
        Struct *struct_;
        StructBlueprint *struct_blueprint;
        int *refcount;
    } as;
} Object;

#define IS_BOOL(object) ((object).type == OBJ_BOOLEAN)
#define IS_NUM(object) ((object).type == OBJ_NUMBER)
#define IS_FUNC(object) ((object).type == OBJ_FUNCTION)
#define IS_POINTER(object) ((object).type == OBJ_POINTER)
#define IS_NULL(object) ((object).type == OBJ_NULL)
#define IS_STRING(object) ((object).type == OBJ_STRING)
#define IS_STRUCT(object) ((object).type == OBJ_STRUCT)
#define IS_STRUCT_BLUEPRINT(object) ((object).type == OBJ_STRUCT_BLUEPRINT)
#define IS_PROP(object) ((object).type == OBJ_PROPERTY)

#define DEALLOC_OBJ(object) \
do { \
    if (IS_STRUCT((object))) { \
        table_free((object).as.struct_->properties); \
        free((object).as.struct_->properties); \
        free((object).as.struct_); \
    } \
} while(0)

#define OBJECT_INCREF(object) \
do { \
    if (IS_STRUCT((object))) { \
        ++*(object).as.refcount; \
    } \
} while (0)

#define OBJECT_DECREF(object) \
do { \
    if (IS_STRUCT((object))) { \
        if (--*(object).as.refcount == 0) { \
            DEALLOC_OBJ((object)); \
        } \
    } \
} while(0)

#define AS_DOUBLE(thing) ((Object){ .type = OBJ_NUMBER, .as.dval = (thing) })
#define AS_BOOL(thing) ((Object){ .type = OBJ_BOOLEAN, .as.bval = (thing) })
#define AS_FUNC(thing) ((Object){ .type = OBJ_FUNCTION, .as.func = ALLOC(thing) })
#define AS_POINTER(thing) ((Object){ .type = OBJ_POINTER, .as.ptr = (thing) })
#define AS_STR(thing) ((Object){ .type = OBJ_STRING, .as.str = (thing) })
#define AS_NULL() ((Object){ .type = OBJ_NULL })
#define AS_PROP(thing) ((Object){ .type = OBJ_PROPERTY, .as.prop = (thing) })
#define AS_STRUCT(thing) ((Object){ .type = OBJ_STRUCT, .as.struct_ = (thing) })
#define AS_STRUCT_BLUEPRINT(thing) ((Object){ .type = OBJ_STRUCT_BLUEPRINT, .as.struct_blueprint = ALLOC(thing) })

#define TO_DOUBLE(object) ((object).as.dval)
#define TO_BOOL(object) ((object).as.bval)
#define TO_STRUCT(object) ((object).as.struct_)
#define TO_FUNC(object) ((object).as.func)
#define TO_PROP(object) ((object).as.prop)
#define TO_STRUCT_BLUEPRINT(object) ((object).as.struct_blueprint)
#define TO_PTR(object) ((object).as.ptr)
#define TO_STR(object) ((object).as.str)

#define PRINT_OBJECT(object) \
do { \
    printf("{ "); \
    if IS_BOOL(object) { \
        printf("%s", TO_BOOL(object) ? "true" : "false"); \
    } else if IS_NUM(object) { \
        printf("%.2f", TO_DOUBLE(object)); \
    } else if IS_POINTER(object) { \
        printf("PTR ('%p')", TO_PTR(object)); \
    } else if IS_NULL(object) { \
        printf("null"); \
    } else if IS_STRING(object) { \
        printf("%s", TO_STR(object)); \
    } else if (IS_STRUCT(object)) { \
        printf("%s", TO_STRUCT(object)->name); \
        printf(" {"); \
        table_print(TO_STRUCT(object)->properties); \
        printf(" }"); \
    } \
    printf(" }"); \
} while (0)

#endif