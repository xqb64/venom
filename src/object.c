#include "object.h"

#include "table.h"

void print_object(Object *object)
{
    if (IS_BOOL(*object))
    {
        bool value = AS_BOOL(*object);
        printf("%s", value ? "true" : "false");
    }
    else if (IS_NUM(*object))
    {
        double number = AS_NUM(*object);
        printf("%.16g", number);
    }
    else if (IS_NULL(*object))
    {
        printf("null");
    }
    else if (IS_CLOSURE(*object))
    {
        printf("<fn %s, ref: %d>", AS_CLOSURE(*object)->func->name, AS_CLOSURE(*object)->refcount);
    }
    else if (IS_STRING(*object))
    {
        String *string = AS_STRING(*object);
        printf("%s", string->value);
    }
    else if (IS_STRUCT(*object))
    {
        Struct *structobj = AS_STRUCT(*object);
        printf("<%s", structobj->name);
        printf(" { ");
        for (size_t i = 0; i < structobj->properties->count; i++)
        {
            print_object(&structobj->properties->items[i]);
            if (i < structobj->properties->count - 1)
            {
                printf(", ");
            }
        }
        printf(" }, ref: %d>", AS_STRUCT(*object)->refcount);
    }
    else if (IS_PTR(*object))
    {
        printf("PTR ('%p')", (void *) AS_PTR(*object));
    }
    else if (IS_ARRAY(*object))
    {
        Array *array = AS_ARRAY(*object);
        printf("[");
        for (size_t i = 0; i < array->elements.count; i++)
        {
            print_object(&array->elements.data[i]);
            if (i < array->elements.count - 1)
            {
                printf(", ");
            }
        }
        printf("]");
    }
    else if (IS_GENERATOR(*object))
    {
        Generator *gen = AS_GENERATOR(*object);
        printf("<gen [%s] [ip: %p]>", gen->fn->func->name, gen->ip);
    }
}

void free_table_object(const Table_Object *table)
{
    for (size_t i = 0; i < table->count; i++)
    {
        Object obj = table->items[i];
        if (IS_CLOSURE(obj) || IS_STRUCT(obj) || IS_STRING(obj) || IS_ARRAY(obj))
        {
            objdecref(&obj);
        }
    }
    for (size_t i = 0; i < TABLE_MAX; i++)
    {
        if (table->indexes[i] != NULL)
        {
            Bucket *bucket = table->indexes[i];
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