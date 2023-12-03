#include "object.h"
#include "table.h"

void print_object(Object *object) {
  if (IS_BOOL(*object)) {
    printf("%s", TO_BOOL(*object) ? "true" : "false");
  } else if (IS_NUM(*object)) {
    printf("%.2f", TO_DOUBLE(*object));
  } else if (IS_PTR(*object)) {
    printf("PTR ('%p')", (void *)TO_PTR(*object));
  } else if (IS_NULL(*object)) {
    printf("null");
  } else if (IS_STRING(*object)) {
    printf("%s", TO_STR(*object)->value);
  } else if (IS_STRUCT(*object)) {
    printf("{ ");
    printf("%s", TO_STRUCT(*object)->name);
    printf(" {");
    for (size_t i = 0; i < TO_STRUCT(*object)->propcount; i++) {
      print_object(&TO_STRUCT(*object)->properties[i]);
    }
    printf(" }");
    printf(" }");
  }
}

void free_table_object(const Table_Object *table) {
  for (size_t i = 0; i < TABLE_MAX; i++) {
    if (table->indexes[i] != NULL) {
      Bucket *bucket = table->indexes[i];
      Object obj = table->items[bucket->value];
      if (IS_STRUCT(obj) || IS_STRING(obj)) {
        OBJECT_DECREF(obj);
      }
      list_free(bucket);
    }
  }
}