#ifndef venom_args_h
#define venom_args_h

#include <stdbool.h>

typedef struct {
  int lex;
  int parse;
  int ir;
  int optimize;
  int measure_flags;
  char *file;
} Arguments;

typedef struct {
  Arguments args;
  bool is_ok;
  int errcode;
  char *msg;
} ArgParseResult;

ArgParseResult parse_args(int argc, char **argv);

#define MEASURE_NONE 0
#define MEASURE_READ_FILE (1 << 0)
#define MEASURE_LEX (1 << 1)
#define MEASURE_PARSE (1 << 2)
#define MEASURE_LOOP_LABEL (1 << 3)
#define MEASURE_OPTIMIZE (1 << 4)
#define MEASURE_DISASSEMBLE (1 << 5)
#define MEASURE_COMPILE (1 << 6)
#define MEASURE_EXEC (1 << 7)
#define MEASURE_ALL                                                       \
  (MEASURE_READ_FILE | MEASURE_LEX | MEASURE_PARSE | MEASURE_LOOP_LABEL | \
   MEASURE_OPTIMIZE | MEASURE_COMPILE | MEASURE_DISASSEMBLE | MEASURE_EXEC)

#endif
