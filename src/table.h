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

#define table_get(table, key)                                                  \
  ({                                                                           \
    int idx = hash((key), strlen((key))) % TABLE_MAX;                          \
    int *item_idx = list_find((table)->indexes[idx], (key));                   \
    item_idx != NULL ? &((table)->items[*item_idx]) : NULL;                    \
  })

#define table_insert(table, key, item)                                         \
  do {                                                                         \
    char *k = own_string((key));                                               \
    int bucket_idx = hash(k, strlen(k)) % TABLE_MAX;                           \
    if (list_find((table)->indexes[bucket_idx], k) == NULL) {                  \
      list_insert(&(table)->indexes[bucket_idx], k, (table)->count);           \
      (table)->items[(table)->count++] = (item);                               \
    } else {                                                                   \
      *table_get((table), (key)) = (item);                                     \
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