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
    printf("%s", TO_STRUCT(*object)->name);
    printf(" { ");
    for (size_t i = 0; i < TO_STRUCT(*object)->propcount; i++) {
      print_object(&TO_STRUCT(*object)->properties[i]);
      if (i < TO_STRUCT(*object)->propcount - 1) {
        printf(", ");
      }
    }
    printf(" }");
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

inline void objincref(Object *obj) {
  if (IS_STRUCT(*obj) || IS_STRING(*obj)) {
    ++*obj->as.refcount;
  }
}

inline void objdecref(Object *obj) {
  if (IS_STRING(*obj)) {
    if (--*obj->as.refcount == 0) {
      dealloc(obj);
    }
  }
  if (IS_STRUCT(*obj)) {
    if (--*obj->as.refcount == 0) {
      for (size_t i = 0; i < obj->as.structobj->propcount; i++) {
        objdecref(&obj->as.structobj->properties[i]);
      }
      dealloc(obj);
    }
  }
}

inline void dealloc(Object *obj) {
  if (IS_STRUCT(*obj)) {
    free(obj->as.structobj);
  }
  if (IS_STRING(*obj)) {
    free(obj->as.str->value);
    free(obj->as.str);
  }
}