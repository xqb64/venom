#ifndef venom_table_h
#define venom_table_h

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define TABLE_MAX 1024

typedef struct Bucket {
  char *key;
  int value;
  struct Bucket *next;
} Bucket;

#define Table(T)                                                               \
  struct {                                                                     \
    Bucket *indexes[TABLE_MAX];                                                \
    T items[TABLE_MAX];                                                        \
    int count;                                                                 \
  }

uint32_t hash(const char *key, int length);
void list_free(Bucket *head);
int *list_find(Bucket *head, const char *item);
void list_insert(Bucket **head, char *key, int item);

/*
Given &array[0], sizeof(array[0]) and i, this function returns array[*i].
If i is NULL, then this returns NULL.

The advantage compared to using array[*i] in a macro is checking for NULL.
The disadvantage is that this always evaluates to a void*.
*/
void *access_if_idx_not_null(void *array, size_t itemsize, int *i);

// Never returns NULL. Assumes that the table has the given key.
#define table_get_unchecked(table, key)                                        \
  (&(table)->items[*list_find(                                                 \
      (table)->indexes[hash((key), strlen((key))) % TABLE_MAX], (key))])

/*
Returns NULL for not found.
Return type is void*, so make sure to use a pointer of the correct type.
*/
#define table_get(table, key)                                                  \
  access_if_idx_not_null(                                                      \
      &(table)->items[0], sizeof((table)->items[0]),                           \
      list_find((table)->indexes[hash((key), strlen((key))) % TABLE_MAX],      \
                (key)))

#define table_insert(table, key, item)                                         \
  do {                                                                         \
    char *k = own_string((key));                                               \
    int bucket_idx = hash(k, strlen(k)) % TABLE_MAX;                           \
    if (list_find((table)->indexes[bucket_idx], k) == NULL) {                  \
      list_insert(&(table)->indexes[bucket_idx], k, (table)->count);           \
      (table)->items[(table)->count++] = (item);                               \
    } else {                                                                   \
      *table_get_unchecked((table), (key)) = (item);                           \
      free(k);                                                                 \
    }                                                                          \
  } while (0)

#define table_print(table)                                                     \
  do {                                                                         \
    for (int i = 0; i < TABLE_MAX; i++) {                                      \
      if ((table)->indexes[i] != NULL) {                                       \
        Bucket *bucket = (table)->indexes[i];                                  \
        printf(" %s: ", bucket->key);                                          \
        print_object(&((table)->items[bucket->value]));                        \
        printf(", ");                                                          \
      }                                                                        \
    }                                                                          \
  } while (0)

#endif