#include <string.h>
#include <stdlib.h>
#include "dynarray.h"

char *strcat_dynarray(DynArray_char_ptr array) {
    /* Determine the combined length of all strings
     * within the array, excluding the null terminators. */
    int msg_len = 0;
    for (size_t i = 0; i < array.count; i++) {
        msg_len += strlen(array.data[i]);
    }

    /* We need to allocate sufficient space for the resulting string.
     * That would be: msg_len + (array.count - 1) * 2 + 1, where:
     * - (array.count - 1) * 2 is the length of (comma, space)
     * pairs (", ") that come after each string except the last
     * one (hence -1),
     * - +1 in the end is for the null terminator of the result. */
    char *msg = malloc(msg_len + (array.count - 1) * 2 + 1);
    for (size_t i = 0; i < array.count; i++) {
        strcat(msg, array.data[i]);
        if (i != array.count - 1) {
            strcat(msg, ", ");
        }
    }
    return msg;
}