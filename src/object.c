#include "object.h"
#include "table.h"

void print_object(Object *object) {
  switch (object->type) {
  case OBJ_BOOLEAN: {
    bool value = TO_BOOL(*object);
    printf("%s", value ? "true" : "false");
    break;
  }
  case OBJ_NUMBER: {
    double number = TO_DOUBLE(*object);
    printf("%.2f", number);
    break;
  }
  case OBJ_STRING: {
    String *string = TO_STR(*object);
    printf("%s", string->value);
    break;
  }
  case OBJ_STRUCT: {
    Struct *structobj = TO_STRUCT(*object);
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
    Object *ptr = TO_PTR(*object);
    printf("PTR ('%p')", (void *)ptr);
    break;
  }
  case OBJ_NULL: {
    printf("null");
    break;
  }
  default:
    break;
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
