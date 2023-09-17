#ifndef venom_object_h
#define venom_object_h

#include <stdbool.h>
#include <stdint.h>
#include "dynarray.h"

typedef struct Table Table;

typedef enum {
    OBJ_NULL,
    OBJ_BOOLEAN,
    OBJ_NUMBER,
    OBJ_STRING,
    OBJ_STRUCT,
    OBJ_PTR,
    OBJ_BCPTR,
    OBJ_FUNCTION,
    OBJ_STRUCT_BLUEPRINT,
} __attribute__ ((__packed__)) ObjectType;

typedef struct {
    char *name;
    size_t location;
    size_t paramcount;
} Function;

typedef struct {
    int refcount;
    Table *properties;
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
        struct Object *ptr;
        Function *func;
        uint8_t *bcptr;

        /* Structs can get arbitrarily large, so we need
         * a pointer, at which point (no pun intended) it
         * grows into a memory management issue because the
         * venom users would need to worry about free()-ing
         * their instances manually.
         *
         * In short, we need refcounting or some other form
         * of garbage collection. We choose refcounting because
         * it's simple.
         *
         * It corresponds to having a variable on the stack.
         * e.g. if one of the instructions takes a variable
         * from somewhere and pushes it on the stack, now we
         * have it at two places, and we need to INCREF. */
        Struct *struct_;
        StructBlueprint *struct_blueprint;

        /* Since we will ultimately have two refcounted
         * objects when string concatenation gets impl-
         * emented (Struct and String), we are going to
         * need a handy way to access their refcounts.
         *
         * For example, when one of these two refcounted
         * objects (a Struct or a String) is at some address,
         * we will (ab)use the fact that the first member of
         * those (int refcount;) will also be lying at the
         * same address, and choose to interpret the object
         * at that address as an int pointer, effectively
         * accessing their refcounts. */
        int *refcount;
    } as;
} Object;

#define IS_BOOL(object) ((object).type == OBJ_BOOLEAN)
#define IS_NUM(object) ((object).type == OBJ_NUMBER)
#define IS_FUNC(object) ((object).type == OBJ_FUNCTION)
#define IS_PTR(object) ((object).type == OBJ_PTR)
#define IS_BCPTR(object) ((object).type == OBJ_BCPTR)
#define IS_NULL(object) ((object).type == OBJ_NULL)
#define IS_STRING(object) ((object).type == OBJ_STRING)
#define IS_STRUCT(object) ((object).type == OBJ_STRUCT)
#define IS_STRUCT_BLUEPRINT(object) ((object).type == OBJ_STRUCT_BLUEPRINT)

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
#define AS_FUNC(thing) ((Object){ .type = OBJ_FUNCTION, .as.func = (thing) })
#define AS_PTR(thing) ((Object){ .type = OBJ_PTR, .as.ptr = (thing) })
#define AS_BCPTR(thing) ((Object){ .type = OBJ_BCPTR, .as.bcptr = (thing) })
#define AS_STR(thing) ((Object){ .type = OBJ_STRING, .as.str = (thing) })
#define AS_NULL() ((Object){ .type = OBJ_NULL })
#define AS_STRUCT(thing) ((Object){ .type = OBJ_STRUCT, .as.struct_ = (thing) })
#define AS_STRUCT_BLUEPRINT(thing) ((Object){ .type = OBJ_STRUCT_BLUEPRINT, .as.struct_blueprint = (thing) })

#define TO_DOUBLE(object) ((object).as.dval)
#define TO_BOOL(object) ((object).as.bval)
#define TO_STRUCT(object) ((object).as.struct_)
#define TO_FUNC(object) ((object).as.func)
#define TO_STRUCT_BLUEPRINT(object) ((object).as.struct_blueprint)
#define TO_PTR(object) ((object).as.ptr)
#define TO_BCPTR(object) ((object).as.bcptr)
#define TO_STR(object) ((object).as.str)

#define PRINT_OBJECT(object) \
do { \
    if IS_BOOL(object) { \
        printf("%s", TO_BOOL(object) ? "true" : "false"); \
    } else if IS_NUM(object) { \
        printf("%.2f", TO_DOUBLE(object)); \
    } else if IS_PTR(object) { \
        printf("PTR ('%p')", TO_PTR(object)); \
    } else if IS_BCPTR(object) { \
        printf("BC ('%p')", TO_BCPTR(object)); \
    } else if IS_NULL(object) { \
        printf("null"); \
    } else if IS_STRING(object) { \
        printf("%s", TO_STR(object)); \
    } else if (IS_STRUCT(object)) { \
        printf("{ "); \
        printf("%s", TO_STRUCT(object)->name); \
        printf(" {"); \
        table_print(TO_STRUCT(object)->properties); \
        printf(" }"); \
        printf(" }"); \
    } \
} while (0)

#endif