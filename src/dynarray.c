#include "dynarray.h"

#include <stdlib.h>
#include <string.h>

char *strcat_dynarray(DynArray_char_ptr array) {
  /* Compute the combined length of all strings
   * in the array, excluding the null terminat-
   * ors after each of the strings in the array. */
  int msg_len = 0;
  for (size_t i = 0; i < array.count; i++) {
    msg_len += strlen(array.data[i]);
  }

  /* Allocate sufficient space for the new str-
   * ing: msg_len + (array.count - 1) * 2 + 1,
   * where: (array.count - 1) * 2 is the length
   * of ", " pairs that come after each string,
   * except the last one (hence -1), and +1 in
   * the end is for the null terminator of the
   * resulting string. */
  char *msg = malloc(msg_len + (array.count - 1) * 2 + 1);

  /* Concatenate strings in the array into the
   * previously allocated string, while taking
   * care of not adding ", " after the last one. */
  for (size_t i = 0; i < array.count; i++) {
    strcat(msg, array.data[i]);
    if (i != array.count - 1) {
      strcat(msg, ", ");
    }
  }
  return msg;
}