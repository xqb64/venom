#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *own_string(const char *string)
{
    char *s = malloc(strlen(string) + 1);
    snprintf(s, strlen(string) + 1, "%s", string);
    return s;
}

char *own_string_n(const char *string, int n)
{
    char *s = malloc(strlen(string) + 1);
    snprintf(s, n + 1, "%s", string);
    return s;
}

char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
    {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    char *buffer = malloc(size + 1);
    if (buffer == NULL)
    {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytes_read = fread(buffer, 1, size, file);
    if (bytes_read < size)
    {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }

    buffer[bytes_read] = '\0';

    fclose(file);
    return buffer;
}

size_t numlen(size_t n)
{
    size_t i;

    i = 0;

    do {
        n /= 10;
        i++;
    } while (n != 0);

    return i;
}

size_t lblen(const char *label, size_t n)
{
    return strlen(label) + numlen(n) + 1;
}

int alloc_err_str(char **dst, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    int len = vsnprintf(NULL, 0, fmt, args);
    if (len < 0)
        return -1;

    *dst = malloc(len + 1);

    va_start(args, fmt);
    vsnprintf(*dst, len + 1, fmt, args);
    va_end(args);

    return 0;
}


