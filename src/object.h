#ifndef venom_object_h
#define venom_object_h

#include <stdbool.h>
#include <stdint.h>
#include "dynarray.h"
#include "table.h"

typedef struct Table Table;

typedef enum {
    OBJ_NUMBER,
    OBJ_BOOLEAN,
    OBJ_FUNCTION,
    OBJ_STRUCT,
    OBJ_PROPERTY,
    OBJ_POINTER,
    OBJ_STRING,
    OBJ_NULL,
} ObjectType;

typedef struct {
    char *name;
    uint8_t location;
    size_t paramcount;
} Function;

typedef struct {
    char *name;
    Table *properties;
    int propertycount;
} Struct;

typedef struct Object {
    ObjectType type;
    union {
        char *str;
        double dval;
        bool bval;
        Function func;
        uint8_t *ptr;
        Struct struct_;
    } as;
    char *name;
} Object;

typedef DynArray(Object) Object_DynArray;
typedef DynArray(char *) String_DynArray;

#define IS_BOOL(object) ((object)->type == OBJ_BOOLEAN)
#define IS_NUM(object) ((object)->type == OBJ_NUMBER)
#define IS_FUNC(object) ((object)->type == OBJ_FUNCTION)
#define IS_POINTER(object) ((object)->type == OBJ_POINTER)
#define IS_NULL(object) ((object)->type == OBJ_NULL)
#define IS_STRING(object) ((object)->type == OBJ_STRING)
#define IS_STRUCT(object) ((object)->type == OBJ_STRUCT)

#define AS_NUM(thing) ((Object){ .type = OBJ_NUMBER, .as.dval = (thing) })
#define AS_BOOL(thing) ((Object){ .type = OBJ_BOOLEAN, .as.bval = (thing) })
#define AS_FUNC(thing) ((Object){ .type = OBJ_FUNCTION, .as.func = (thing) })
#define AS_POINTER(thing) ((Object){ .type = OBJ_POINTER, .as.ptr = (thing)} )
#define AS_STR(thing) ((Object){ .type = OBJ_STRING, .as.str = (thing)} )

#define NUM_VAL(object) ((object).as.dval)
#define BOOL_VAL(object) ((object).as.bval)

void print_object(Object *object);

#endif