#ifndef venom_object_h
#define venom_object_h

#include "dynarray.h"
#include "table.h"
#include <stdbool.h>
#include <stdint.h>

#define TABLE_MAX 1024

typedef enum {
  OBJ_NULL,
  OBJ_BOOLEAN,
  OBJ_NUMBER,
  OBJ_STRING,
  OBJ_STRUCT,
  OBJ_PTR,
} __attribute__((__packed__)) ObjectType;

typedef struct {
  int refcount;
  char *value;
} String;

typedef struct {
  int location;
  uint8_t *addr;
} BytecodePtr;

struct Struct;

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
    struct Struct *struct_;

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
  Table_Object properties;
  char *name;
} Struct;

#define DEALLOC_OBJ(object)                                                    \
  do {                                                                         \
    if (IS_STRUCT((object))) {                                                 \
      free_table_object(&(object).as.struct_->properties);                            \
      free((object).as.struct_);                                               \
    }                                                                          \
    if (IS_STRING((object))) {                                                 \
      free((object).as.str->value);                                            \
      free((object).as.str);                                                   \
    }                                                                          \
  } while (0)

#define OBJECT_INCREF(object)                                                  \
  do {                                                                         \
    if (IS_STRUCT((object)) || IS_STRING((object))) {                          \
      ++*(object).as.refcount;                                                 \
    }                                                                          \
  } while (0)

#define OBJECT_DECREF(object)                                                  \
  do {                                                                         \
    if (IS_STRUCT((object)) || IS_STRING((object))) {                          \
      if (--*(object).as.refcount == 0) {                                      \
        DEALLOC_OBJ((object));                                                 \
      }                                                                        \
    }                                                                          \
  } while (0)

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
#define TO_STRUCT(object) ((object).as.struct_)
#define TO_PTR(object) ((object).as.ptr)
#define TO_STR(object) ((object).as.str)

#define AS_DOUBLE(thing) ((Object){.type = OBJ_NUMBER, .as.dval = (thing)})
#define AS_BOOL(thing) ((Object){.type = OBJ_BOOLEAN, .as.bval = (thing)})
#define AS_PTR(thing) ((Object){.type = OBJ_PTR, .as.ptr = (thing)})
#define AS_STR(thing) ((Object){.type = OBJ_STRING, .as.str = (thing)})
#define AS_NULL() ((Object){.type = OBJ_NULL})
#define AS_STRUCT(thing) ((Object){.type = OBJ_STRUCT, .as.struct_ = (thing)})

void print_object(Object *obj);

#endif