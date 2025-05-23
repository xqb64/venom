#ifndef venom_object_h
#define venom_object_h

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "dynarray.h"
#include "table.h"

typedef enum {
  OBJ_STRUCT,
  OBJ_STRING,
  OBJ_ARRAY,
  OBJ_PTR,
  OBJ_NUMBER,
  OBJ_BOOLEAN,
  OBJ_NULL,
  OBJ_CLOSURE,
  OBJ_GENERATOR,
} ObjectType;

#ifdef NAN_BOXING

/* IEEE 754 double-precision floating-point numbers are used for encoding
 * all Venom objects which currently include numbers (doubles), booleans,
 * strings, structs, pointers, closures, arrays, and nulls.
 *
 * A brief reminder for my future self of how NaN boxing works follows.
 *
 * A double is composed of:
 *  - a sign bit (MSB),
 *  - 11-bit exponent, and
 *  - 52-bit mantissa
 *
 * What's interesting about IEEE 754 double-precision floating-point num-
 * bers is that when all exponent bits are set, i.e., are ones, that dou-
 * ble represents a special value called NaN (Not a Number).
 *
 * There are two types of NaNs:
 *  - signalling NaNs (the MSB of the mantissa is clear (0))
 *  - quiet NaNs (the MSB of the mantissa is set (1))
 *
 * As the name says, signalling NaNs are meant to convey that an erroneo-
 * us computation, like division by zero or similar, occurred. Quiet NaNs
 * don't represent any useful value, so they're supposed to be safer, and
 * these are what we will use for this scheme.
 *
 * This means that any double where all the exponent bits plus the highe-
 * st mantissa bit are set is a quiet NaN. The 12 bits (including 11 exp-
 * onent bits + the MSB of mantissa) used for indicating a quiet NaN lea-
 * ves 64 - 12 = 52 bits free to use for whatever we want. However, since
 * there exists a bit (at index 50, zero-based, called "QNaN Floating-Po-
 * int Indefinite") that Intel processors don't like you to step on, care
 * needs to be taken to not do so, leaving us with 51 bits to spare (0-49
 * (inclusive)) + sign bit.
 *
 * Is this the best we can do?
 *
 * Well, on most machines, a pointer is usually just 48-bits and the rest
 * of the bits are either unspecified or zeroed out. We will (ab)use this
 * fact, along with the Object pointers are aligned to an 8-byte boundary
 * since Object contains a union whose largest member is 64 bits, effect-
 * ively making the size of the whole union 64 bits. This, in turn, means
 * that the three least significant bits of the pointer will always be 0.
 * Since the pointer occupies 48 bits, and we have space until bit 50, we
 * can shift the pointer two places to the left, such that it starts from
 * 2 (instead of 0) and end at 49 (instead of 47). This frees up 2 slots,
 * and combined with the last 3 bits of the pointer, this is 5 bits total
 * consecutively to pack in the type of the Object. However, the tag bits
 * need not be consecutive. If we wanted them consecutively, there'd need
 * to be some bit shifting involved in very common operations and this is
 * slightly detrimental to the performance (well, it's rational to expect
 * that it is supposed to be, unless you have a Ryzen 3 3200g).
 *
 * Essentially, 64 bits is enough to hold all possible numeric fp values,
 * a pointer, and 32 different tags.
 *
 * So, we'll use the sign bit, the QNAN pattern, and tags to denote diff-
 * erent objects.
 *
 * Encoding a number is straightforward. It just takes a little type pun-
 * ning to convince the compiler to interpret our doubles as uint64_t.
 *
 * Encoding null, false, and true is done by setting QNAN and the approp-
 * riate tag.
 *
 * As for the other objects, we'll set the SIGN_BIT, QNAN, tag them acco-
 * rdingly, and use a pointer to the object. */

#define SIGN_BIT ((uint64_t) 0x8000000000000000)
#define QNAN ((uint64_t) 0x7ffc000000000000)

#define TAG_NULL 1
#define TAG_FALSE 2
#define TAG_TRUE 3
#define TAG_STRUCT 4
#define TAG_STRING 5
#define TAG_PTR 6
#define TAG_ARRAY 7
#define TAG_CLOSURE 0x2000000000000
#define TAG_GENERATOR 0x2000000000001

typedef uint64_t Object;
typedef DynArray(Object) DynArray_Object;

#define IS_NULL(value) ((value) == NULL_VAL)

/* To check whether a value is a boolean, we'll set the LSB to 1.
 *
 * If it was 'false', its two least significant bits were '10' and now
 * they are '11', which is the bit pattern for 'true' (TAG_TRUE).
 *
 * If it was 'true', its two least significant bits were '11', and now
 * they remained unchanged.
 *
 * Either way, it ends up 'true', so we just check if the value is eq-
 * ual to the TAG_TRUE bit pattern after setting the LSB. */
#define IS_BOOL(value) (((value) | 1) == TRUE_VAL)

/* To check whether a value is a number, we just bitwise-and it with QNAN.
 * If the result is not QNAN, it means it's a number. */
#define IS_NUM(value) (((value) & (QNAN)) != QNAN)

/* To check whether a value is some other object, we bitwise-and it with
 * (SIGN_BIT | QNAN). If the result is (SIGN_BIT | QNAN), it means those
 * bits were set, and remember, the patterns with those bits set mean we
 * have some object other than numbers, booleans, and nulls. */
#define IS_OBJ(value) (((value) & (SIGN_BIT | QNAN)) == (SIGN_BIT | QNAN))

#define STRUCT_PATTERN (SIGN_BIT | QNAN | TAG_STRUCT)
#define STRING_PATTERN (SIGN_BIT | QNAN | TAG_STRING)
#define PTR_PATTERN (SIGN_BIT | QNAN | TAG_PTR)
#define ARRAY_PATTERN (SIGN_BIT | QNAN | TAG_ARRAY)
#define CLOSURE_PATTERN (SIGN_BIT | QNAN | TAG_CLOSURE)
#define GENERATOR_PATTERN (SIGN_BIT | QNAN | TAG_GENERATOR)

/* To check whether a value is a struct, we check if it's an object and
 * whether it is tagged as a Struct. */
#define IS_STRUCT(value) (((value) & (SIGN_BIT | QNAN | 0x7)) == STRUCT_PATTERN)

/* To check whether a value is a struct, we check if it's an object and
 * whether it is tagged as a String. */
#define IS_STRING(value) (((value) & (SIGN_BIT | QNAN | 0x7)) == STRING_PATTERN)

/* To check whether a value is a struct, we check if it's an object and
 * whether it is tagged as a pointer. */
#define IS_PTR(value) (((value) & (SIGN_BIT | QNAN | 0x7)) == PTR_PATTERN)

/* To check whether a value is an array, we check if it's an object and
 * whether it is tagged as an array. */
#define IS_ARRAY(value) (((value) & (SIGN_BIT | QNAN | 0x7)) == ARRAY_PATTERN)

/* To check whether a value is a closure, we check if it's an object and
 * whether it is tagged as a closure. */
#define IS_CLOSURE(value) \
  (((value) & (SIGN_BIT | QNAN | 0x2000000000007)) == CLOSURE_PATTERN)

/* To check whether a value is a generator, we check if it's an object and
 * whether it is tagged as a generator. */
#define IS_GENERATOR(value) \
  (((value) & (SIGN_BIT | QNAN | 0x2000000000007)) == GENERATOR_PATTERN)

/* To convert a value to a boolean, we compare it to TRUE_VAL because
 * if we had a 'false', (false == true) will be false, and we got our
 * value. However, if we had a true, (true == true) will be true, and
 * we again have our value. */
#define AS_BOOL(value) ((value) == TRUE_VAL)

#define AS_NUM(value) object2num(value)

/* To convert a value to an Object pointer, we need to clear the SIGN_BIT,
 * QNAN, and the tag. Finally, we cast the result to Object pointer. */
#define AS_OBJ(value) \
  ((Object *) (uintptr_t) (((value) & ~(SIGN_BIT | QNAN | 0x2000000000007))))

/* To convert a value to a Struct pointer, we need to clear the SIGN_BIT,
 * QNAN, and the tag. Finaly, we cast the result to Struct pointer. */
#define AS_STRUCT(object)                                                \
  ((IS_STRUCT(object))                                                   \
       ? (Struct *) ((uintptr_t) ((object) &                             \
                                  ~(SIGN_BIT | QNAN | 0x2000000000007))) \
       : NULL)

/* To convert a value to a String pointer, we need to clear the SIGN_BIT,
 * QNAN, and the tag. Finally, we cast the result to String pointer. */
#define AS_STRING(object)                                                \
  ((IS_STRING(object))                                                   \
       ? (String *) ((uintptr_t) ((object) &                             \
                                  ~(SIGN_BIT | QNAN | 0x2000000000007))) \
       : NULL)

/* To convert a value to a pointer Object, we need to clear the SIGN_BIT,
 * QNAN, and the tag. Finally, we cast the result to Object pointer. */
#define AS_PTR(object)                                                         \
  ((IS_PTR(object)) ? (Object *) ((uintptr_t) ((object) & ~(SIGN_BIT | QNAN |  \
                                                            0x2000000000007))) \
                    : NULL)

/* To convert a value to an array Object, we need to clear the SIGN_BIT,
 * QNAN, and the tag. Finally, we cast the result to Array pointer. */
#define AS_ARRAY(object)                                                \
  ((IS_ARRAY(object))                                                   \
       ? (Array *) ((uintptr_t) ((object) &                             \
                                 ~(SIGN_BIT | QNAN | 0x2000000000007))) \
       : NULL)

/* To convert a value to a closure Object, we need to clear the SIGN_BIT,
 * QNAN, and the tag. Finally, we cast the result to Closure pointer. */
#define AS_CLOSURE(object)                                                \
  ((IS_CLOSURE(object))                                                   \
       ? (Closure *) ((uintptr_t) ((object) &                             \
                                   ~(SIGN_BIT | QNAN | 0x2000000000007))) \
       : NULL)

/* To convert a value to a generator Object, we need to clear the SIGN_BIT,
 * QNAN, and the tag. Finally, we cast the result to Generator pointer. */
#define AS_GENERATOR(object)                                                \
  ((IS_GENERATOR(object))                                                   \
       ? (Generator *) ((uintptr_t) ((object) &                             \
                                     ~(SIGN_BIT | QNAN | 0x2000000000007))) \
       : NULL)

#define BOOL_VAL(b) ((b) ? TRUE_VAL : FALSE_VAL)

/* To construct a boolean Object with value 'false', we set QNAN and tag
 * it, then sprinkle a little type punning on top of it.
 *
 * Likewise for 'true' and 'null'. */
#define FALSE_VAL ((Object) (uint64_t) (QNAN | TAG_FALSE))
#define TRUE_VAL ((Object) (uint64_t) (QNAN | TAG_TRUE))
#define NULL_VAL ((Object) (uint64_t) (QNAN | TAG_NULL))
#define NUM_VAL(num) num2object(num)

/* To construct a Struct object, we set the SIGN_BIT, QNAN, and tag it as
 * struct.
 *
 * Likewise for String, Array, Closure and Object pointer. */
#define STRUCT_VAL(obj) \
  (Object)(SIGN_BIT | QNAN | ((uint64_t) (uintptr_t) (obj)) | TAG_STRUCT)

#define STRING_VAL(obj) \
  (Object)(SIGN_BIT | QNAN | ((uint64_t) (uintptr_t) (obj)) | TAG_STRING)

#define PTR_VAL(obj) \
  (Object)(SIGN_BIT | QNAN | ((uint64_t) (uintptr_t) (obj)) | TAG_PTR)

#define ARRAY_VAL(obj) \
  (Object)(SIGN_BIT | QNAN | ((uint64_t) (uintptr_t) (obj)) | TAG_ARRAY)

#define CLOSURE_VAL(obj) \
  (Object)(SIGN_BIT | QNAN | ((uint64_t) (uintptr_t) (obj)) | TAG_CLOSURE)

#define GENERATOR_VAL(obj) \
  (Object)(SIGN_BIT | QNAN | ((uint64_t) (uintptr_t) (obj)) | TAG_GENERATOR)

inline double object2num(Object value)
{
  union {
    double num;
    uint64_t bits;
  } data;
  data.bits = value;
  return data.num;
}

inline Object num2object(double num)
{
  union {
    double num;
    uint64_t bits;
  } data;
  data.num = num;
  return data.bits;
}

#else

typedef struct String String;
typedef struct Struct Struct;
typedef struct Array Array;
typedef struct Function Function;
typedef struct Closure Closure;
typedef struct Upvalue Upvalue;
typedef struct Generator Generator;

typedef DynArray(struct Object) DynArray_Object;

typedef struct Object {
  ObjectType type;
  union {
    double dval;
    bool bval;
    String *str;
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
    Struct *structobj;

    Array *array;

    Closure *closure;

    Generator *generator;
    /* Since we have five refcounted objects (Struct, String,
     * Array, Closure, Generator), we need a handy way to access
     * their refcounts.
     *
     * For example, when one of these five types of refcounted
     * objects is at some address, we will (ab)use the fact that
     * the first member of those (int refcount;) will also be
     * lying at the same address, and choose to interpret the
     * object at that address as an int pointer, effectively
     * accessing their refcounts. */
    int *refcount;
  } as;
} Object;

#define IS_BOOL(object) ((object).type == OBJ_BOOLEAN)
#define IS_NUM(object) ((object).type == OBJ_NUMBER)
#define IS_PTR(object) ((object).type == OBJ_PTR)
#define IS_NULL(object) ((object).type == OBJ_NULL)
#define IS_STRING(object) ((object).type == OBJ_STRING)
#define IS_STRUCT(object) ((object).type == OBJ_STRUCT)
#define IS_CLOSURE(object) ((object).type == OBJ_CLOSURE)
#define IS_UPVALUE(object) ((object).type == OBJ_UPVALUE)
#define IS_STRUCT_BLUEPRINT(object) ((object).type == OBJ_STRUCT_BLUEPRINT)
#define IS_ARRAY(object) ((object).type == OBJ_ARRAY)
#define IS_GENERATOR(object) ((object).type == OBJ_GENERATOR)

#define AS_NUM(object) ((object).as.dval)
#define AS_BOOL(object) ((object).as.bval)
#define AS_STRUCT(object) ((object).as.structobj)
#define AS_PTR(object) ((object).as.ptr)
#define AS_STRING(object) ((object).as.str)
#define AS_ARRAY(object) ((object).as.array)
#define AS_FUNC(object) ((object).as.func)
#define AS_CLOSURE(object) ((object).as.closure)
#define AS_UPVALUE(object) ((object).as.upvalue)
#define AS_STRUCT_BLUEPRINT(object) ((object).as.struct_blueprint)
#define AS_GENERATOR(object) ((object).as.generator)

#define NUM_VAL(thing) ((Object) {.type = OBJ_NUMBER, .as.dval = (thing)})
#define BOOL_VAL(thing) ((Object) {.type = OBJ_BOOLEAN, .as.bval = (thing)})
#define STRING_VAL(thing) ((Object) {.type = OBJ_STRING, .as.str = (thing)})
#define STRUCT_VAL(thing) \
  ((Object) {.type = OBJ_STRUCT, .as.structobj = (thing)})
#define PTR_VAL(thing) ((Object) {.type = OBJ_PTR, .as.ptr = (thing)})
#define ARRAY_VAL(thing) ((Object) {.type = OBJ_ARRAY, .as.array = (thing)})
#define FUNC_VAL(thing) ((Object) {.type = OBJ_FUNC, .as.func = (thing)})
#define CLOSURE_VAL(thing) \
  ((Object) {.type = OBJ_CLOSURE, .as.closure = (thing)})
#define UPVALUE_VAL(thing) \
  ((Object) {.type = OBJ_UPVALUE, .as.upvalue = (thing)})
#define GENERATOR_VAL(thing) \
  ((Object) {.type = OBJ_GENERATOR, .as.generator = (thing)})
#define NULL_VAL ((Object) {.type = OBJ_NULL})

#endif

void print_object(const Object *obj);

typedef struct String {
  int refcount;
  char *value;
} String;

typedef struct Array {
  int refcount;
  DynArray_Object elements;
} Array;

typedef struct Function {
  char *name;
  size_t location;
  size_t paramcount;
  int upvalue_count;
  bool is_gen;
} Function;

typedef struct Upvalue {
  Object *location;
  Object closed;
  struct Upvalue *next;
} Upvalue;

typedef struct Closure {
  int refcount;
  Function *func;
  Upvalue **upvalues;
  int upvalue_count;
} Closure;

typedef Table(Function) Table_Function;

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

inline const char *get_object_type(const Object *object)
{
  if (IS_STRING(*object)) {
    return "string";
  } else if (IS_STRUCT(*object)) {
    return "struct";
  } else if (IS_ARRAY(*object)) {
    return "array";
  } else if (IS_PTR(*object)) {
    return "pointer";
  } else if (IS_BOOL(*object)) {
    return "boolean";
  } else if (IS_NUM(*object)) {
    return "number";
  } else if (IS_CLOSURE(*object)) {
    return "closure";
  } else if (IS_NULL(*object)) {
    return "null";
  } else if (IS_GENERATOR(*object)) {
    return "generator";
  }
  assert(0);
}

typedef Table(Object) Table_Object;
void free_table_object(const Table_Object *table);

typedef struct Struct {
  int refcount;
  char *name;
  size_t propcount;
  Table_Object *properties;
} Struct;

typedef struct {
  uint8_t *addr;
  int location;
  Closure *fn;
} BytecodePtr;

typedef enum {
  STATE_NEW,
  STATE_SUSPENDED,
  STATE_ACTIVE,
  STATE_DONE,
} GeneratorState;

typedef struct Generator {
  int refcount;
  uint8_t *ip;
  Object stack[1024];
  size_t tos;
  BytecodePtr fp_stack[1024];
  size_t fp_count;
  Closure *fn;
  GeneratorState state;
} Generator;

inline void objincref(Object *obj)
{
#ifdef NAN_BOXING
  if (IS_STRING(*obj)) {
    ++AS_STRING(*obj)->refcount;
  } else if (IS_STRUCT(*obj)) {
    ++AS_STRUCT(*obj)->refcount;
  } else if (IS_ARRAY(*obj)) {
    ++AS_ARRAY(*obj)->refcount;
  } else if (IS_CLOSURE(*obj)) {
    ++AS_CLOSURE(*obj)->refcount;
  } else if (IS_GENERATOR(*obj)) {
    ++AS_GENERATOR(*obj)->refcount;
  }
#else
  switch (obj->type) {
    case OBJ_STRING:
    case OBJ_ARRAY:
    case OBJ_CLOSURE:
    case OBJ_STRUCT:
    case OBJ_GENERATOR: {
      ++*(obj)->as.refcount;
      break;
    }

    default:
      break;
  }
#endif
}

inline void dealloc(Object *obj);

inline void objdecref(Object *obj)
{
#ifdef NAN_BOXING
  if (IS_STRING(*obj)) {
    if (--AS_STRING(*obj)->refcount == 0) {
      dealloc(obj);
    }
  } else if (IS_STRUCT(*obj)) {
    if (--AS_STRUCT(*obj)->refcount == 0) {
      for (size_t i = 0; i < AS_STRUCT(*obj)->properties->count; i++) {
        objdecref(&AS_STRUCT(*obj)->properties->items[i]);
      }
      dealloc(obj);
    }
  } else if (IS_ARRAY(*obj)) {
    if (--AS_ARRAY(*obj)->refcount == 0) {
      for (size_t i = 0; i < AS_ARRAY(*obj)->elements.count; i++) {
        objdecref(&AS_ARRAY(*obj)->elements.data[i]);
      }
      dealloc(obj);
    }
  } else if (IS_CLOSURE(*obj)) {
    if (--AS_CLOSURE(*obj)->refcount == 0) {
      for (int i = 0; i < AS_CLOSURE(*obj)->upvalue_count; i++) {
        objdecref(AS_CLOSURE(*obj)->upvalues[i]->location);
      }
      dealloc(obj);
    }
  } else if (IS_GENERATOR(*obj)) {
    if (--AS_GENERATOR(*obj)->refcount == 0) {
      for (size_t i = 0; i < AS_GENERATOR(*obj)->tos; i++) {
        objdecref(&AS_GENERATOR(*obj)->stack[i]);
      }
      dealloc(obj);
    }
  }

#else

  switch (obj->type) {
    case OBJ_STRING: {
      if (--*(obj)->as.refcount == 0) {
        dealloc(obj);
      }
      break;
    }
    case OBJ_STRUCT: {
      if (--*(obj)->as.refcount == 0) {
        for (size_t i = 0; i < AS_STRUCT(*obj)->properties->count; i++) {
          objdecref(&AS_STRUCT(*obj)->properties->items[i]);
        }
        dealloc(obj);
      }
      break;
    }
    case OBJ_ARRAY: {
      if (--*(obj)->as.refcount == 0) {
        for (size_t i = 0; i < AS_ARRAY(*obj)->elements.count; i++) {
          objdecref(&AS_ARRAY(*obj)->elements.data[i]);
        }
        dealloc(obj);
      }
      break;
    }
    case OBJ_CLOSURE: {
      if (--*(obj)->as.refcount == 0) {
        for (int i = 0; i < AS_CLOSURE(*obj)->upvalue_count; i++) {
          objdecref(AS_CLOSURE(*obj)->upvalues[i]->location);
        }
        dealloc(obj);
      }
      break;
    }
    case OBJ_GENERATOR: {
      if (--*(obj)->as.refcount == 0) {
        for (size_t i = 0; i < AS_GENERATOR(*obj)->tos; i++) {
          objdecref(&AS_GENERATOR(*obj)->stack[i]);
        }
        dealloc(obj);
      }
      break;
    }
    default:
      break;
  }
#endif
}

inline void dealloc(Object *obj)
{
#ifdef NAN_BOXING
  if (IS_STRUCT(*obj)) {
    for (size_t i = 0; i < TABLE_MAX; i++) {
      if (AS_STRUCT(*obj)->properties->indexes[i] != NULL) {
        Bucket *bucket = AS_STRUCT(*obj)->properties->indexes[i];
        list_free(bucket);
      }
    }

    free(AS_STRUCT(*obj)->properties);
    free(AS_STRUCT(*obj));
  } else if (IS_STRING(*obj)) {
    free(AS_STRING(*obj)->value);
    free(AS_STRING(*obj));
  } else if (IS_ARRAY(*obj)) {
    dynarray_free(&AS_ARRAY(*obj)->elements);
    free(AS_ARRAY(*obj));
  } else if (IS_CLOSURE(*obj)) {
    for (int i = 0; i < AS_CLOSURE(*obj)->upvalue_count; i++) {
      free(AS_CLOSURE(*obj)->upvalues[i]);
    }
    free(AS_CLOSURE(*obj)->upvalues);
    free(AS_CLOSURE(*obj)->func);
    free(AS_CLOSURE(*obj));
  } else if (IS_GENERATOR(*obj)) {
    free(AS_GENERATOR(*obj));
  }
#else
  switch (obj->type) {
    case OBJ_STRUCT: {
      for (size_t i = 0; i < TABLE_MAX; i++) {
        if (AS_STRUCT(*obj)->properties->indexes[i] != NULL) {
          Bucket *bucket = AS_STRUCT(*obj)->properties->indexes[i];
          list_free(bucket);
        }
      }

      free(AS_STRUCT(*obj)->properties);
      free(AS_STRUCT(*obj));
      break;
    }
    case OBJ_STRING: {
      free(AS_STRING(*obj)->value);
      free(AS_STRING(*obj));
      break;
    }
    case OBJ_ARRAY: {
      dynarray_free(&AS_ARRAY(*obj)->elements);
      free(AS_ARRAY(*obj));
      break;
    }
    case OBJ_CLOSURE: {
      for (int i = 0; i < AS_CLOSURE(*obj)->upvalue_count; i++) {
        free(AS_CLOSURE(*obj)->upvalues[i]);
      }
      free(AS_CLOSURE(*obj)->upvalues);
      free(AS_CLOSURE(*obj)->func);
      free(AS_CLOSURE(*obj));
      break;
    }
    case OBJ_GENERATOR: {
      free(AS_GENERATOR(*obj));
      break;
    }
    default:
      break;
  }
#endif
}

#ifdef NAN_BOXING
inline ObjectType type(const Object *obj)
{
  if (IS_ARRAY(*obj)) {
    return OBJ_ARRAY;
  }
  if (IS_BOOL(*obj)) {
    return OBJ_BOOLEAN;
  }
  if (IS_CLOSURE(*obj)) {
    return OBJ_CLOSURE;
  }
  if (IS_GENERATOR(*obj)) {
    return OBJ_GENERATOR;
  }
  if (IS_NULL(*obj)) {
    return OBJ_NULL;
  }
  if (IS_NUM(*obj)) {
    return OBJ_NUMBER;
  }
  if (IS_PTR(*obj)) {
    return OBJ_PTR;
  }
  if (IS_STRING(*obj)) {
    return OBJ_STRING;
  }
  if (IS_STRUCT(*obj)) {
    return OBJ_STRUCT;
  }
  assert(0);
}
#else
inline ObjectType type(const Object *obj)
{
  return obj->type;
}
#endif

#endif
