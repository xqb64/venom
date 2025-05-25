#include "args.h"

#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int parse_measure_flag(const char *arg)
{
  if (strcmp(arg, "all") == 0) {
    return MEASURE_ALL;
  } else if (strcmp(arg, "lex") == 0) {
    return MEASURE_LEX;
  } else if (strcmp(arg, "parse") == 0) {
    return MEASURE_PARSE;
  } else if (strcmp(arg, "loop-label") == 0) {
    return MEASURE_LOOP_LABEL;
  } else if (strcmp(arg, "optimize") == 0) {
    return MEASURE_OPTIMIZE;
  } else if (strcmp(arg, "disassemble") == 0) {
    return MEASURE_DISASSEMBLE;
  } else if (strcmp(arg, "compile") == 0) {
    return MEASURE_COMPILE;
  } else if (strcmp(arg, "exec") == 0) {
    return MEASURE_EXEC;
  }
  return MEASURE_NONE;
}

ArgParseResult parse_args(int argc, char **argv)
{
  static const struct option long_opts[] = {
      {"lex", no_argument, 0, 'l'},
      {"parse", no_argument, 0, 'p'},
      {"ir", no_argument, 0, 'i'},
      {"optimize", no_argument, 0, 'o'},
      {"measure", required_argument, 0, 'm'},
      {0, 0, 0, 0},
  };

  int do_lex = 0;
  int do_parse = 0;
  int do_ir = 0;
  int do_optimize = 0;
  int measure_flags = 0;

  int opt, opt_idx = 0;
  while ((opt = getopt_long(argc, argv, "lpiom", long_opts, &opt_idx)) != -1) {
    switch (opt) {
      case 'l':
        do_lex = 1;
        break;
      case 'p':
        do_parse = 1;
        break;
      case 'i':
        do_ir = 1;
        break;
      case 'o':
        do_optimize = 1;
        break;
      case 'm':
        measure_flags |= parse_measure_flag(optarg);
        break;
      default:
        return (ArgParseResult) {
            .args = {0},
            .is_ok = false,
            .errcode = -1,
            .msg = strdup("usage: %s [--lex] [--parse] [--ir] [--optimize]")};
    }
  }

  if (do_lex + do_optimize > 1) {
    return (ArgParseResult) {
        .args = {0},
        .is_ok = false,
        .errcode = -1,
        .msg =
            strdup("--optimize available only from the parsing stage onwards")};
  }

  if (do_lex + do_parse + do_ir > 1) {
    return (ArgParseResult) {
        .args = {0},
        .is_ok = false,
        .errcode = -1,
        .msg = strdup("Please specify exactly one option.")};
  }

  Arguments args;

  args.lex = do_lex;
  args.parse = do_parse;
  args.ir = do_ir;
  args.optimize = do_optimize;
  args.measure_flags = measure_flags;
  args.file = argv[optind];

  return (ArgParseResult) {
      .args = args, .is_ok = true, .errcode = 0, .msg = NULL};
}
