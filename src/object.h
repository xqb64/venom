#ifndef venom_object_h
#define venom_object_h

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "table.h"

#ifdef NAN_BOXING

#define SIGN_BIT ((uint64_t)0x8000000000000000)
#define QNAN ((uint64_t)0x7ffc000000000000)

#define TAG_NULL 1  // 01.
#define TAG_FALSE 2 // 10.
#define TAG_TRUE 3  // 11.

typedef uint64_t Object;

#define IS_BOOL(value) (((value) | 1) == TRUE_VAL)
#define IS_NULL(value) ((value) == NULL_VAL)
#define IS_NUM(value) (((value)&QNAN) != QNAN)
#define IS_OBJ(value) (((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#define AS_BOOL(value) ((value) == TRUE_VAL)
#define AS_NUM(value) object2num(value)
#define AS_OBJ(value) ((Obj *)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))
#define AS_STRUCT(object) (AS_OBJ((object))->as.structobj)
#define AS_STR(object) (AS_OBJ((object))->as.str)
#define AS_PTR(object) ((AS_OBJ((object))->as.ptr))

#define BOOL_VAL(b) ((b) ? TRUE_VAL : FALSE_VAL)
#define FALSE_VAL ((Object)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL ((Object)(uint64_t)(QNAN | TAG_TRUE))
#define NULL_VAL ((Object)(uint64_t)(QNAN | TAG_NULL))
#define NUM_VAL(num) num2object(num)
#define OBJ_VAL(obj) (Object)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj))

#define IS_STRING(object)                                                      \
  (IS_OBJ((object)) && AS_OBJ((object))->type == OBJ_STRING)
#define IS_STRUCT(object)                                                      \
  (IS_OBJ((object)) && AS_OBJ((object))->type == OBJ_STRUCT)
#define IS_PTR(object) (IS_OBJ((object)) && AS_OBJ((object))->type == OBJ_PTR)

static inline double object2num(Object value) {
  double num;
  memcpy(&num, &value, sizeof(Object));
  return num;
}

static inline Object num2object(double num) {
  Object value;
  memcpy(&value, &num, sizeof(double));
  return value;
}

#else

typedef struct Object {
  ObjectType type;
  union {
    double dval;
    bool bval;
    Obj *obj;
  } as;
} Object;

#define IS_BOOL(object) ((object).type == OBJ_BOOLEAN)
#define IS_NUM(object) ((object).type == OBJ_NUMBER)
#define IS_PTR(object)                                                         \
  ((object).type == OBJ_OBJ && AS_OBJ((object))->type == OBJ_PTR)
#define IS_NULL(object) ((object).type == OBJ_NULL)
#define IS_STRING(object)                                                      \
  ((object).type == OBJ_OBJ && AS_OBJ((object))->type == OBJ_STRING)
#define IS_STRUCT(object)                                                      \
  ((object).type == OBJ_OBJ && AS_OBJ((object))->type == OBJ_STRUCT)

#define IS_FUNC(object) ((object).type == OBJ_FUNCTION)
#define IS_STRUCT_BLUEPRINT(object) ((object).type == OBJ_STRUCT_BLUEPRINT)

#define AS_OBJ(object) ((object).as.obj)
#define AS_NUM(object) ((object).as.dval)
#define AS_BOOL(object) ((object).as.bval)
#define AS_STRUCT(object) ((object).as.obj->as.structobj)
#define AS_PTR(object) (AS_OBJ((object))->as.ptr)
#define AS_STR(object) ((AS_OBJ((object))->as.str)

#define AS_FUNC(object) ((object).as.func)
#define AS_STRUCT_BLUEPRINT(object) ((object).as.struct_blueprint)

#define NUM_VAL(thing) ((Object){.type = OBJ_NUMBER, .as.dval = (thing)})
#define BOOL_VAL(thing) ((Object){.type = OBJ_BOOLEAN, .as.bval = (thing)})
#define NULL_VAL ((Object){.type = OBJ_NULL})

#endif

void print_object(Object *obj);

typedef struct Pointer Pointer;

typedef enum {
  OBJ_OBJ,
  OBJ_NULL,
  OBJ_BOOLEAN,
  OBJ_NUMBER,
} ObjectType;

typedef enum {
  OBJ_STRUCT,
  OBJ_STRING,
  OBJ_PTR,
} ObjType;

typedef struct {
  int refcount;
  char *value;
} String;

typedef struct Struct {
  int refcount;
  char *name;
  size_t propcount;
  Object *properties;
} Struct;

typedef struct Obj {
  ObjType type;
  union {
    String *str;
    Pointer *ptr;

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
} Obj;

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

inline const char *get_object_type(Object *object) {
  if (IS_OBJ(*object)) {
    switch (OBJ_TYPE(*object)) {
    case OBJ_STRING:
      return "string";
    case OBJ_STRUCT:
      return "struct";
    case OBJ_PTR:
      return "pointer";
    default:
      break;
    }
  }
  if (IS_BOOL(*object)) {
    return "boolean";
  }
  if (IS_NUM(*object)) {
    return "number";
  }
  if (IS_NULL(*object)) {
    return "null";
  }
  return "unknown";
}

typedef Table(Object) Table_Object;
void free_table_object(const Table_Object *table);

typedef struct {
  uint8_t *addr;
  int location;
} BytecodePtr;

inline void dealloc(Object *obj) {
  if (IS_STRUCT(*obj)) {
    free(AS_OBJ(*obj)->as.structobj->properties);
    free(AS_OBJ(*obj)->as.structobj);
    free(AS_OBJ(*obj));
  } else if (IS_STRING(*obj)) {
    free(AS_OBJ(*obj)->as.str->value);
    free(AS_OBJ(*obj)->as.str);
    free(AS_OBJ(*obj));
  } else if (IS_PTR(*obj)) {
    free(AS_OBJ(*obj)->as.ptr);
    free(AS_OBJ(*obj));
  }
}

inline void objincref(Object *obj) {
  if (IS_STRING(*obj) || IS_STRUCT(*obj) || IS_PTR(*obj)) {
    ++*AS_OBJ(*obj)->as.refcount;
  }
}

inline void objdecref(Object *obj) {
  if (IS_STRING(*obj) || IS_PTR(*obj)) {
    if (--*AS_OBJ(*obj)->as.refcount == 0) {
      dealloc(obj);
    }
  } else if (IS_STRUCT(*obj)) {
    if (--*AS_OBJ(*obj)->as.refcount == 0) {
      for (size_t i = 0; i < AS_OBJ(*obj)->as.structobj->propcount; i++) {
        objdecref(&AS_OBJ(*obj)->as.structobj->properties[i]);
      }
      dealloc(obj);
    }
  }
}

typedef struct Pointer {
  int refcount;
  Object *ptr;
} Pointer;

#endif