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
    OBJ_STRUCT_BLUEPRINT,
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

typedef struct {
    char *name;
    char *properties[256];
    int propertycount;
} StructBlueprint;

typedef struct Object {
    ObjectType type;
    union {
        char *str;
        double dval;
        bool bval;
        Function func;
        uint8_t *ptr;
        Struct struct_;
        StructBlueprint struct_blueprint;
    } as;
    char *name;
    int refcount;
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

Object *ALLOC(Object object);
void DEALLOC(Object *object);

#define OBJECT_INCREF(object) (object)->refcount++
#define OBJECT_DECREF(object) do{  \
    if (--(object)->refcount == 0) { \
        DEALLOC((object)); \
    } \
} while(0)

#define AS_NUM(thing) ((Object){ .type = OBJ_NUMBER, .as.dval = (thing), .refcount = 0 })
#define AS_BOOL(thing) ((Object){ .type = OBJ_BOOLEAN, .as.bval = (thing), .refcount = 0 })
#define AS_FUNC(thing) ((Object){ .type = OBJ_FUNCTION, .as.func = (thing), .refcount = 0 })
#define AS_POINTER(thing) ((Object){ .type = OBJ_POINTER, .as.ptr = (thing), .refcount = 0 })
#define AS_STR(thing) ((Object){ .type = OBJ_STRING, .as.str = (thing), .refcount = 0 })

#define NUM_VAL(object) ((object)->as.dval)
#define BOOL_VAL(object) ((object)->as.bval)

void print_object(Object *object);

#endif