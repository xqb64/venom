#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

#include "ast.h"
#include "compiler.h"
#include "disassembler.h"
#include "dynarray.h"
#include "optimizer.h"
#include "parser.h"
#include "semantics.h"
#include "tokenizer.h"
#include "util.h"
#include "vm.h"

typedef struct {
  int lex;
  int parse;
  int ir;
  int optimize;
  char *file;
} Arguments;

typedef struct {
  Arguments args;
  bool is_ok;
  int errcode;
  char *msg;
} ArgParseResult;

typedef struct {
  bool is_ok;
  int errcode;
  char *msg;
} RunResult;

static RunResult run(Arguments *args)
{
  RunResult result = {.is_ok = true, .errcode = 0, .msg = NULL};

  ReadFileResult read_file_result = read_file(args->file);
  if (!read_file_result.is_ok) {
    alloc_err_str(&result.msg, read_file_result.msg);
    result.is_ok = false;
    result.errcode = read_file_result.errcode;
    goto cleanup_after_read_file;
  }

  char *source = read_file_result.payload;

  Tokenizer tokenizer;
  init_tokenizer(&tokenizer, source);

  TokenizeResult tokenize_result = tokenize(&tokenizer);

  if (!tokenize_result.is_ok) {
    alloc_err_str(&result.msg, "tokenizer: %s\n", tokenize_result.msg);
    result.is_ok = false;
    result.errcode = -1;
    goto cleanup_after_lex;
  }

  DynArray_Token tokens = tokenize_result.tokens;

  if (args->lex) {
    print_tokens(&tokens);
    goto cleanup_after_lex;
  }

  Parser parser;
  init_parser(&parser, &tokens);

  ParseResult parse_result = parse(&parser);
  if (!parse_result.is_ok) {
    alloc_err_str(&result.msg, "parser: %s\n", parse_result.msg);
    result.is_ok = false;
    result.errcode = -1;
    goto cleanup_after_parse;
  }

  DynArray_Stmt raw_ast = parse_result.ast;

  LoopLabelResult loop_label_result = loop_label_program(&raw_ast, NULL);
  if (!loop_label_result.is_ok) {
    alloc_err_str(&result.msg, "loop_labeler: %s\n", loop_label_result.msg);
    result.is_ok = false;
    result.errcode = -1;
    goto cleanup_after_loop_label;
  }

  DynArray_Stmt labeled_ast = loop_label_result.as.ast;

  DynArray_Stmt optimized_ast = {0};

  if (args->optimize) {
    optimized_ast = optimize(&labeled_ast);
  }

  if (args->parse) {
    if (args->optimize) {
      print_ast(&optimized_ast);
    } else {
      print_ast(&labeled_ast);
    }
    goto cleanup_after_loop_label;
  }

  Compiler *compiler = current_compiler = new_compiler();

  CompileResult compile_result;
  if (args->optimize) {
    compile_result = compile(&optimized_ast);
  } else {
    compile_result = compile(&labeled_ast);
  }

  if (!compile_result.is_ok) {
    alloc_err_str(&result.msg, "compiler: %s\n", compile_result.msg);
    result.is_ok = false;
    result.errcode = -1;
    goto cleanup_after_compile;
  }

  Bytecode *chunk = compile_result.chunk;

  DisassembleResult disassemble_result = {0};
  if (args->ir) {
    disassemble_result = disassemble(chunk);
    if (!disassemble_result.is_ok) {
      alloc_err_str(&result.msg, "disassembler: %s\n", disassemble_result.msg);
      result.is_ok = false;
      result.errcode = -1;
    }
    goto cleanup_after_disassemble;
  }

  VM vm;
  init_vm(&vm);

  ExecResult exec_result = exec(&vm, chunk);
  if (!exec_result.is_ok) {
    alloc_err_str(&result.msg, "vm: %s\n", exec_result.msg);
    result.is_ok = false;
    result.errcode = -1;
    goto cleanup_after_exec; /* just for the symmetry */
  }

cleanup_after_exec:
  free_vm(&vm);
  if (!exec_result.is_ok) {
    free(exec_result.msg);
  }

cleanup_after_disassemble:
  if (!disassemble_result.is_ok) {
    free(disassemble_result.msg);
  }

cleanup_after_compile:
  if (compile_result.is_ok) {
    free_compiler(compiler);
    free(compiler);
    free_chunk(chunk);
    free(chunk);
  } else {
    free(compile_result.msg);
  }

cleanup_after_loop_label:
  if (loop_label_result.is_ok) {
    free_ast(&labeled_ast);
  } else {
    free(loop_label_result.msg);
  }

cleanup_after_parse:
  if (parse_result.is_ok) {
    free_ast(&raw_ast);
  }

  if (args->optimize) {
    free_ast(&optimized_ast);
  }

cleanup_after_lex:
  dynarray_free(&tokenize_result.tokens);
  free(source);

  if (!tokenize_result.is_ok) {
    free(tokenize_result.msg);
  }

cleanup_after_read_file:
  if (!read_file_result.is_ok) {
    free(read_file_result.msg);
  }

  return result;
}

static ArgParseResult parse_args(int argc, char *argv[])
{
  static const struct option long_opts[] = {
      {"lex", no_argument, 0, 'l'},
      {"parse", no_argument, 0, 'p'},
      {"ir", no_argument, 0, 'i'},
      {"optimize", no_argument, 0, 'o'},
      {0, 0, 0, 0},
  };

  int do_lex = 0;
  int do_parse = 0;
  int do_ir = 0;
  int do_optimize = 0;

  int opt, opt_idx = 0;
  while ((opt = getopt_long(argc, argv, "lpio", long_opts, &opt_idx)) != -1) {
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
      default:
        return (ArgParseResult) {
            .args = {0},
            .is_ok = false,
            .errcode = -1,
            .msg = ALLOC("usage: %s [--lex] [--parse] [--ir] [--optimize]")};
    }
  }

  if (do_lex + do_optimize > 1) {
    return (ArgParseResult) {
        .args = {0},
        .is_ok = false,
        .errcode = -1,
        .msg =
            ALLOC("--optimize available only from the parsing stage onwards")};
  }

  if (do_lex + do_parse + do_ir > 1) {
    return (ArgParseResult) {
        .args = {0},
        .is_ok = false,
        .errcode = -1,
        .msg = ALLOC("Please specify exactly one option.")};
  }

  Arguments args;

  args.lex = do_lex;
  args.parse = do_parse;
  args.ir = do_ir;
  args.optimize = do_optimize;
  args.file = argv[optind];

  return (ArgParseResult) {
      .args = args, .is_ok = true, .errcode = 0, .msg = NULL};
}

int main(int argc, char *argv[])
{
  Arguments args;
  ArgParseResult arg_parse_result;
  RunResult result;

  arg_parse_result = parse_args(argc, argv);
  if (!arg_parse_result.is_ok) {
    fprintf(stderr, "venom: %s\n", arg_parse_result.msg);
    free(arg_parse_result.msg);
    return arg_parse_result.errcode;
  }

  args = arg_parse_result.args;
  result = run(&args);
  if (!result.is_ok) {
    fprintf(stderr, "%s", result.msg);
    free(result.msg);
    return result.errcode;
  }

  return result.errcode;
}
