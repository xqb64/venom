#include "table.h"

void *access_if_idx_not_null(void *array, size_t itemsize, int *i)
{
  if (i == NULL) {
    return NULL;
  }

  // Casting to char*, so that we can give the offset as bytes.
  return (char *)array + (*i) * itemsize;
}

int *list_find(Bucket *head, const char *item)
{
  while (head != NULL) {
    if (strcmp(head->key, item) == 0) {
      return &head->value;
    }
    head = head->next;
  }
  return NULL;
}

void list_insert(Bucket **head, char *key, int item)
{
  /* Create a new node. */
  Bucket *new_node = malloc(sizeof(Bucket));

  new_node->key = key;
  new_node->value = item;
  new_node->next = NULL;

  /* Handle the edge case when the list is empty. */
  if (*head == NULL) {
    *head = new_node;
  } else {
    /* Go to the end of the list. */
    Bucket *current = *head;
    while (current->next != NULL) {
      current = current->next;
    }

    /* Store the new node at the end. */
    current->next = new_node;
  }
}

void list_free(Bucket *head)
{
  Bucket *tmp;
  while (head != NULL) {
    tmp = head;
    head = head->next;
    free(tmp->key);
    free(tmp);
  }
}

uint32_t hash(const char *key, int length)
{
  /* copy-paste from 'crafting interpreters' */
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

void table_remove_impl(Bucket **head, const char *key, void *items,
                       size_t itemsize)
{
  Bucket *prev = NULL;
  Bucket *curr = *head;

  while (curr != NULL) {
    if (strcmp(curr->key, key) == 0) {
      if (prev) {
        prev->next = curr->next;
      } else {
        *head = curr->next;
      }

      // Clear item from items array
      memset((char *)items + curr->value * itemsize, 0, itemsize);

      // Free the bucket node
      free(curr->key);
      free(curr);

      return;
    }
    prev = curr;
    curr = curr->next;
  }
}
