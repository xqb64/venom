#include "object.h"
#include "table.h"

void print_object(Object *object) {
  if (IS_BOOL(*object)) {
    bool value = AS_BOOL(*object);
    printf("%s", value ? "true" : "false");
  }
  if (IS_NUM(*object)) {
    double number = AS_NUM(*object);
    printf("%.2f", number);
  }
  if (IS_NULL(*object)) {
    printf("null");
  }
  if (IS_OBJ(*object)) {
    switch (AS_OBJ(*object)->type) {
    case OBJ_STRING: {
      String *string = AS_STR(*object);
      printf("%s", string->value);
      break;
    }
    case OBJ_STRUCT: {
      Struct *structobj = AS_STRUCT(*object);
      printf("%s", structobj->name);
      printf(" { ");
      for (size_t i = 0; i < structobj->propcount; i++) {
        print_object(&structobj->properties[i]);
        if (i < structobj->propcount - 1) {
          printf(", ");
        }
      }
      printf(" }");
      break;
    }
    case OBJ_PTR: {
      Object *ptr = AS_PTR(*object);
      printf("PTR ('%p')", (void *)ptr);
      break;
    }
    default:
      break;
    }
  }
}

void free_table_object(const Table_Object *table) {
  for (size_t i = 0; i < TABLE_MAX; i++) {
    if (table->indexes[i] != NULL) {
      Bucket *bucket = table->indexes[i];
      Object obj = table->items[bucket->value];
      if (IS_STRUCT(obj) || IS_STRING(obj)) {
        objdecref(&obj);
      }
      list_free(bucket);
    }
  }
}

extern inline void dealloc(Object *obj);
extern inline void objdecref(Object *obj);
extern inline void objincref(Object *obj);
extern inline const char *get_object_type(Object *object);