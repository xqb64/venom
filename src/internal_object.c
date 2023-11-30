#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal_object.h"
#include "util.h"

static void list_insert(InternalBucket **head, char *key, InternalObject item) {
  /* Create a new node. */
  InternalBucket *new_node = malloc(sizeof(InternalBucket));

  new_node->key = key;
  new_node->obj = item;
  new_node->next = NULL;

  /* Handle the edge case when the list is empty. */
  if (*head == NULL) {
    *head = new_node;
  } else {
    /* Go to the end of the list. */
    InternalBucket *current = *head;
    while (current->next != NULL) {
      current = current->next;
    }

    /* Store the new node at the end. */
    current->next = new_node;
  }
}

static void list_free(InternalBucket *head) {
  InternalBucket *tmp;
  while (head != NULL) {
    tmp = head;
    head = head->next;

    if (IS_STRUCT_BLUEPRINT(tmp->obj)) {
      StructBlueprint *blueprint = TO_STRUCT_BLUEPRINT(tmp->obj);
      dynarray_free(&blueprint->properties);
      free(blueprint);
    } else if (IS_FUNC(tmp->obj)) {
      Function *funcobj = TO_FUNC(tmp->obj);
      free(funcobj);
    }

    free(tmp->key);
    free(tmp);
  }
}

static InternalObject *list_find(InternalBucket *head, const char *item) {
  while (head != NULL) {
    if (strcmp(head->key, item) == 0)
      return &head->obj;
    head = head->next;
  }
  return NULL;
}

static uint32_t hash(const char *key, int length) {
  /* copy-paste from 'crafting interpreters' */
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

void internal_table_insert(InternalTable *table, const char *key,
                           InternalObject obj) {
  char *k = own_string(key);
  int idx = hash(k, strlen(k)) % TABLE_MAX;
  if (list_find(table->data[idx], k) == NULL) {
    list_insert(&table->data[idx], k, obj);
  } else {
    /* If the key is already in the list, change its value. */
    InternalBucket *head = table->data[idx];
    while (head != NULL) {
      if (strcmp(head->key, k) == 0) {
        table->data[idx]->obj = obj;
      }
      head = head->next;
    }
    free(k);
  }
}

InternalObject *internal_table_get(const InternalTable *table,
                                   const char *key) {
  int idx = hash(key, strlen(key)) % TABLE_MAX;
  return list_find(table->data[idx], key);
}

void internal_table_free(const InternalTable *table) {
  for (size_t i = 0; i < TABLE_MAX; i++) {
    if (table->data[i] != NULL) {
      list_free(table->data[i]);
    }
  }
}

void internal_table_print(const InternalTable *table) {
  for (size_t i = 0; i < TABLE_MAX; i++) {
    if (table->data[i] != NULL) {
      printf(" %s: ", table->data[i]->key);
      PRINT_INTERNAL_OBJECT(table->data[i]->obj);
      printf(", ");
    }
  }
}