#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

char *own_string(const char *string) {
  char *s = malloc(strlen(string) + 1);
  snprintf(s, strlen(string) + 1, "%s", string);
  return s;
}

char *own_string_n(const char *string, int n) {
  char *s = malloc(strlen(string) + 1);
  snprintf(s, n + 1, "%s", string);
  return s;
}

char *read_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }

  fseek(file, 0L, SEEK_END);
  size_t size = ftell(file);
  rewind(file);

  char *buffer = malloc(size + 1);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    exit(74);
  }

  size_t bytes_read = fread(buffer, 1, size, file);
  if (bytes_read < size) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }

  buffer[bytes_read] = '\0';

  fclose(file);
  return buffer;
}
