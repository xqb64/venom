#include "object.h"
#include "table.h"

void print_object(Object *object) {
  if (IS_BOOL(*object)) {
    bool value = AS_BOOL(*object);
    printf("%s", value ? "true" : "false");
  } else if (IS_NUM(*object)) {
    double number = AS_NUM(*object);
    printf("%.16g", number);
  } else if (IS_NULL(*object)) {
    printf("null");
  } else if (IS_STRING(*object)) {
    String *string = AS_STRING(*object);
    printf("%s", string->value);
  } else if (IS_STRUCT(*object)) {
    Struct *structobj = AS_STRUCT(*object);
    printf("%s", structobj->name);
    printf(" { ");
    for (size_t i = 0; i < structobj->propcount; i++) {
      print_object(&structobj->properties[i]);
      if (i < structobj->propcount - 1) {
        printf(", ");
      }
    }
  } else if (IS_PTR(*object)) {
    printf("PTR ('%p')", (void *)AS_PTR(*object));
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

#ifdef NAN_BOXING
extern inline double object2num(Object value);
extern inline Object num2object(double num);
#endif