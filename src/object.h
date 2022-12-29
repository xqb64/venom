#ifndef venom_object_h
#define venom_object_h

#include <stdbool.h>
#include <stdint.h>
#include "dynarray.h"

typedef struct Table Table;
typedef struct HeapObject HeapObject;

typedef DynArray(char *) String_DynArray;

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
    String_DynArray properties;
    int propertycount;
} StructBlueprint;

typedef struct Object {
    ObjectType type;
    union {
        char *prop;
        char *str;
        double dval;
        bool bval;
        Function func;
        uint8_t *ptr;
        Struct struct_;
        StructBlueprint struct_blueprint;
        HeapObject *heapobj;
    } as;
} Object;

typedef struct HeapObject {
    Object *obj;
    int refcount;
} HeapObject;

#define IS_BOOL(object) ((object)->type == OBJ_BOOLEAN)
#define IS_NUM(object) ((object)->type == OBJ_NUMBER)
#define IS_FUNC(object) ((object)->type == OBJ_FUNCTION)
#define IS_POINTER(object) ((object)->type == OBJ_POINTER)
#define IS_NULL(object) ((object)->type == OBJ_NULL)
#define IS_STRING(object) ((object)->type == OBJ_STRING)
#define IS_STRUCT(object) ((object)->type == OBJ_STRUCT)
#define IS_STRUCT_BLUEPRINT(object) ((object)->type == OBJ_STRUCT_BLUEPRINT)
#define IS_HEAP(object) ((object)->type == OBJ_HEAP)
#define IS_PROP(object) ((object)->type == OBJ_PROPERTY)

Object *ALLOC(Object object);
void DEALLOC(Object *object);

#define OBJECT_INCREF(object) \
do { \
    if ((object).type == OBJ_HEAP) { \
        (object).as.heapobj->refcount++; \
    } \
} while (0)

#define OBJECT_DECREF(object) \
do { \
    if ((object).type == OBJ_HEAP) { \
        if (--(object).as.heapobj->refcount == 0) { \
            DEALLOC((object).as.heapobj->obj); \
            free((object).as.heapobj); \
        } \
    } \
} while(0)

#define AS_DOUBLE(thing) ((Object){ .type = OBJ_NUMBER, .as.dval = (thing) })
#define AS_BOOL(thing) ((Object){ .type = OBJ_BOOLEAN, .as.bval = (thing) })
#define AS_FUNC(thing) ((Object){ .type = OBJ_FUNCTION, .as.func = (thing) })
#define AS_POINTER(thing) ((Object){ .type = OBJ_POINTER, .as.ptr = (thing) })
#define AS_STR(thing) ((Object){ .type = OBJ_STRING, .as.str = (thing) })
#define AS_NULL() ((Object){ .type = OBJ_NULL })
#define AS_HEAP(thing) ((Object){ .type = OBJ_HEAP, .as.heapobj = (thing) })
#define AS_PROP(thing) ((Object){ .type = OBJ_PROPERTY, .as.prop = (thing) })
#define AS_STRUCT(thing) ((Object){ .type = OBJ_STRUCT, .as.struct_ = (thing) })
#define AS_STRUCT_BLUEPRINT(thing) ((Object){ .type = OBJ_STRUCT_BLUEPRINT, .as.struct_blueprint = (thing) })

#define TO_DOUBLE(object) ((object).as.dval)
#define TO_BOOL(object) ((object).as.bval)
#define TO_STRUCT(object) ((object).as.heapobj->obj->as.struct_)
#define TO_FUNC(object) ((object).as.func)
#define TO_HEAP(object) ((object).as.heapobj)
#define TO_PROP(object) ((object).as.prop)
#define TO_STRUCT_BLUEPRINT(object) ((object).as.struct_blueprint)
#define TO_PTR(object) ((object).as.ptr)
#define TO_STR(object) ((object).as.str)

void print_object(Object *object);

#endif