#ifndef venom_internal_object_h
#define venom_internal_object_h

#include "dynarray.h"
#include <stddef.h>

#define TABLE_MAX 1024

typedef enum {
  OBJ_FUNCTION,
  OBJ_STRUCT_BLUEPRINT,
} __attribute__((__packed__)) InternalObjectType;

typedef struct {
  char *name;
  size_t location;
  size_t paramcount;
} Function;

typedef struct {
  char *name;
  DynArray_char_ptr properties;
} StructBlueprint;

typedef struct InternalObject {
  InternalObjectType type;
  union {
    Function *func;
    StructBlueprint *struct_blueprint;
  } as;
} InternalObject;

typedef struct InternalBucket {
  char *key;
  InternalObject obj;
  struct InternalBucket *next;
} InternalBucket;

typedef struct Internal {
  InternalBucket *data[TABLE_MAX];
} InternalTable;

void internal_table_free(const InternalTable *table);
void internal_table_insert(InternalTable *table, const char *key,
                           InternalObject obj);
InternalObject *internal_table_get(const InternalTable *table, const char *key);
void internal_table_print(const InternalTable *table);

#define IS_FUNC(object) ((object).type == OBJ_FUNCTION)
#define IS_STRUCT_BLUEPRINT(object) ((object).type == OBJ_STRUCT_BLUEPRINT)

#define AS_FUNC(thing)                                                         \
  ((InternalObject){.type = OBJ_FUNCTION, .as.func = (thing)})
#define AS_STRUCT_BLUEPRINT(thing)                                             \
  ((InternalObject){.type = OBJ_STRUCT_BLUEPRINT,                              \
                    .as.struct_blueprint = (thing)})

#define TO_FUNC(object) ((object).as.func)
#define TO_STRUCT_BLUEPRINT(object) ((object).as.struct_blueprint)

#define PRINT_INTERNAL_OBJECT(object)                                          \
  do {                                                                         \
    if (IS_STRUCT_BLUEPRINT(object)) {                                         \
      printf("{ ");                                                            \
      printf("%s", TO_STRUCT_BLUEPRINT(object)->name);                         \
      printf(" {");                                                            \
      for (size_t j = 0; j < TO_STRUCT_BLUEPRINT(object)->properties.count;    \
           j++) {                                                              \
        printf("%s, ", TO_STRUCT_BLUEPRINT(object)->properties.data[j]);       \
      }                                                                        \
      printf(" }");                                                            \
      printf(" }");                                                            \
    } else if (IS_FUNC(object)) {                                              \
      Function *f = TO_FUNC(object);                                           \
      printf("Function '%s' at location %ld with paramcount %ld", f->name,     \
             f->location, f->paramcount);                                      \
    }                                                                          \
  } while (0)

#endif