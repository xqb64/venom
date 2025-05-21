#include "err.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INIT_CAP 256

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
} StringBuilder;

void sb_init(StringBuilder *sb)
{
  sb->cap = INIT_CAP;
  sb->len = 0;
  sb->buf = malloc(sb->cap);
  sb->buf[0] = '\0';
}

void sb_free(StringBuilder *sb)
{
  free(sb->buf);
}

void sb_append(StringBuilder *sb, const char *s)
{
  size_t slen = strlen(s);
  if (sb->len + slen + 1 > sb->cap) {
    sb->cap = (sb->len + slen + 1) * 2;
    sb->buf = realloc(sb->buf, sb->cap);
  }
  memcpy(sb->buf + sb->len, s, slen + 1);
  sb->len += slen;
}

void sb_append_char(StringBuilder *sb, char c)
{
  if (sb->len + 2 > sb->cap) {
    sb->cap *= 2;
    sb->buf = realloc(sb->buf, sb->cap);
  }
  sb->buf[sb->len++] = c;
  sb->buf[sb->len] = '\0';
}

#include "util.h"

void print_line_buf(StringBuilder *sb, const char *source, size_t line)
{
  const char *c = source;
  char *lineno;
  size_t curr = 1;
  size_t len;

  len = numlen(line);
  lineno = malloc(len + 1);

  snprintf(lineno, len+1, "%ld", line);

  sb_append(sb, lineno);
  sb_append_char(sb, ' ');

  free(lineno);

  while (curr < line && *c) {
    if (*c == '\n') {
      curr++;
    }
    c++;
  }

  while (*c && *c != '\n') {
    sb_append_char(sb, *c++);
  }
  sb_append_char(sb, '\n');
}

void print_offending_line_buf(StringBuilder *sb, const char *source,
                              size_t line, size_t span_start, size_t span_end)
{
  const char *c = source, *target_start = source;
  size_t current_line = 1;

  while (*c) {
    if (*c == '\n') {
      current_line++;
    }
    if (current_line == line) {
      target_start = ++c;
      break;
    }
    c++;
  }

  size_t len = numlen(line);
  char *lineno = malloc(line + 1);
  snprintf(lineno, len+1, "%ld", line);
  sb_append(sb, lineno);
  sb_append_char(sb, ' ');

  while (*c && *c != '\n') {
    sb_append_char(sb, *c++);
  }
  sb_append_char(sb, '\n');

  size_t prefix_len = (source + span_start) - target_start + len + 1;
  for (size_t i = 0; i < prefix_len; i++) {
    sb_append_char(sb, ' ');
  }

  size_t caret_len = (span_end - span_start == 0) ? 1 : (span_end - span_start);
  for (size_t i = 0; i < caret_len; i++) {
    sb_append_char(sb, '^');
  }
}

char *mkerrctx(const char *source, size_t line, size_t span_start,
               size_t span_end, size_t before, size_t after)
{
  StringBuilder sb;
  sb_init(&sb);

  for (size_t i = line - before; i < line; i++) {
    print_line_buf(&sb, source, i);
  }

  print_offending_line_buf(&sb, source, line, span_start, span_end);
  sb_append_char(&sb, '\n');

  for (size_t i = line + 1; i < line + after + 1; i++) {
    print_line_buf(&sb, source, i);
  }

  return sb.buf;
}
