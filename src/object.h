#ifndef venom_object_h
#define venom_object_h

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "table.h"

typedef enum {
  OBJ_STRUCT,
  OBJ_STRING,
  OBJ_NULL,
  OBJ_BOOLEAN,
  OBJ_NUMBER,
  OBJ_PTR,
} ObjectType;

typedef struct {
  int refcount;
  char *value;
} String;

typedef struct {
  uint8_t *addr;
  int location;
} BytecodePtr;

typedef struct Object {
  ObjectType type;
  union {
    String *str;
    double dval;
    bool bval;
    struct Object *ptr;

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
    struct Struct *structobj;

    /* Since we have two refcounted objects (Struct and String),
     * we need a handy way to access their refcounts.
     *
     * For example, when one of these two types of refcounted
     * objects is at some address, we will (ab)use the fact that
     * the first member of those (int refcount;) will also be
     * lying at the same address, and choose to interpret the
     * object at that address as an int pointer, effectively
     * accessing their refcounts. */
    int *refcount;
  } as;
} Object;

typedef Table(Object) Table_Object;
void free_table_object(const Table_Object *table);

typedef struct Struct {
  int refcount;
  char *name;
  size_t propcount;
  Object properties[];
} Struct;

#define GET_OBJTYPE(type)                                                      \
  ((type) == OBJ_BOOLEAN  ? "boolean"                                          \
   : (type) == OBJ_NUMBER ? "number"                                           \
   : (type) == OBJ_PTR    ? "pointer"                                          \
   : (type) == OBJ_NULL   ? "null"                                             \
   : (type) == OBJ_STRING ? "string"                                           \
   : (type) == OBJ_STRUCT ? "struct"                                           \
                          : "unknown")

#define IS_FUNC(object) ((object).type == OBJ_FUNCTION)
#define IS_STRUCT_BLUEPRINT(object) ((object).type == OBJ_STRUCT_BLUEPRINT)

#define TO_FUNC(object) ((object).as.func)
#define TO_STRUCT_BLUEPRINT(object) ((object).as.struct_blueprint)

#define IS_BOOL(object) ((object).type == OBJ_BOOLEAN)
#define IS_NUM(object) ((object).type == OBJ_NUMBER)
#define IS_PTR(object) ((object).type == OBJ_PTR)
#define IS_NULL(object) ((object).type == OBJ_NULL)
#define IS_STRING(object) ((object).type == OBJ_STRING)
#define IS_STRUCT(object) ((object).type == OBJ_STRUCT)

#define TO_DOUBLE(object) ((object).as.dval)
#define TO_BOOL(object) ((object).as.bval)
#define TO_STRUCT(object) ((object).as.structobj)
#define TO_PTR(object) ((object).as.ptr)
#define TO_STR(object) ((object).as.str)

#define AS_DOUBLE(thing) ((Object){.type = OBJ_NUMBER, .as.dval = (thing)})
#define AS_BOOL(thing) ((Object){.type = OBJ_BOOLEAN, .as.bval = (thing)})
#define AS_PTR(thing) ((Object){.type = OBJ_PTR, .as.ptr = (thing)})
#define AS_STR(thing) ((Object){.type = OBJ_STRING, .as.str = (thing)})
#define AS_NULL() ((Object){.type = OBJ_NULL})
#define AS_STRUCT(thing) ((Object){.type = OBJ_STRUCT, .as.structobj = (thing)})

void print_object(Object *obj);

inline void dealloc(Object *obj) {
  switch (obj->type) {
  case OBJ_STRUCT: {
    free(obj->as.structobj);
    break;
  }
  case OBJ_STRING: {
    free(obj->as.str->value);
    free(obj->as.str);
    break;
  }
  default:
    break;
  }
}

inline void objincref(Object *obj) {
  switch (obj->type) {
  case OBJ_STRING:
  case OBJ_STRUCT: {
    ++*obj->as.refcount;
    break;
  }
  default:
    break;
  }
}

inline void objdecref(Object *obj) {
  switch (obj->type) {
  case OBJ_STRING: {
    if (--*obj->as.refcount == 0) {
      dealloc(obj);
    }
    break;
  }
  case OBJ_STRUCT: {
    if (--*obj->as.refcount == 0) {
      for (size_t i = 0; i < obj->as.structobj->propcount; i++) {
        objdecref(&obj->as.structobj->properties[i]);
      }
      dealloc(obj);
    }
    break;
  }
  default:
    break;
  }
}

#endif