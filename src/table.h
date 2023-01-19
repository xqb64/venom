#ifndef venom_table_h
#define venom_table_h

#define TABLE_MAX 1024

#include "object.h"

typedef struct Bucket {
    char *key;
    Object obj;
    struct Bucket *next;
} Bucket;

typedef struct Table {
    Bucket *data[TABLE_MAX];
} Table;

void table_free(const Table *table);
void table_insert(Table *table, const char *key, Object obj);
Object *table_get(const Table *table, const char *key);
void table_print(Table *table);

#endif