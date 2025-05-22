#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>  // IWYU pragma: keep
#include <time.h>

#include "args.h"
#include "ast.h"
#include "compiler.h"
#include "disassembler.h"
#include "dynarray.h"
#include "err.h"
#include "optimizer.h"
#include "parser.h"
#include "semantics.h"
#include "tokenizer.h"
#include "util.h"
#include "vm.h"

typedef struct {
  bool is_ok;
  int errcode;
  char *msg;
} RunResult;

static RunResult run(Arguments *args)
{
  RunResult result = {.is_ok = true, .errcode = 0, .msg = NULL};

  struct timespec read_file_start, read_file_end, tokenize_start, tokenize_end,
      parse_start, parse_end, loop_label_start, loop_label_end, optimize_start,
      optimize_end, compile_start, compile_end, disassemble_start,
      disassemble_end, exec_start, exec_end;

  double read_file_elapsed = 0.0, tokenize_elapsed = 0.0, parse_elapsed = 0.0,
         loop_label_elapsed = 0.0, optimize_elapsed = 0.0,
         compile_elapsed = 0.0, disassemble_elapsed = 0.0, exec_elapsed = 0.0;

  clock_gettime(CLOCK_MONOTONIC, &read_file_start);

  ReadFileResult read_file_result = read_file(args->file);
  if (!read_file_result.is_ok) {
    alloc_err_str(&result.msg, read_file_result.msg);
    result.is_ok = false;
    result.errcode = read_file_result.errcode;
    goto cleanup_after_read_file;
  }

  clock_gettime(CLOCK_MONOTONIC, &read_file_end);

  read_file_elapsed = (read_file_end.tv_sec - read_file_start.tv_sec) +
                      (read_file_end.tv_nsec - read_file_start.tv_nsec) / 1e9;

  char *source = read_file_result.payload;

  clock_gettime(CLOCK_MONOTONIC, &tokenize_start);

  Tokenizer tokenizer;
  init_tokenizer(&tokenizer, source);

  TokenizeResult tokenize_result = tokenize(&tokenizer);
  if (!tokenize_result.is_ok) {
    char *errctx =
        mkerrctx(source, tokenize_result.span.line, tokenize_result.span.start,
                 tokenize_result.span.end, 3, 3);
    alloc_err_str(&result.msg, "tokenizer: %s\n%s\n", tokenize_result.msg,
                  errctx);
    free(errctx);
    result.is_ok = false;
    result.errcode = tokenize_result.errcode;
    goto cleanup_after_lex;
  }

  clock_gettime(CLOCK_MONOTONIC, &tokenize_end);

  tokenize_elapsed = (tokenize_end.tv_sec - tokenize_start.tv_sec) +
                     (tokenize_end.tv_nsec - tokenize_start.tv_nsec) / 1e9;

  DynArray_Token tokens = tokenize_result.tokens;

  if (args->lex) {
    print_tokens(&tokens);
    goto cleanup_after_lex;
  }

  clock_gettime(CLOCK_MONOTONIC, &parse_start);

  Parser parser;
  init_parser(&parser, &tokens);

  ParseResult parse_result = parse(&parser);
  if (!parse_result.is_ok) {
    char *errctx =
        mkerrctx(source, parse_result.span.line, parse_result.span.start,
                 parse_result.span.end, 3, 3);
    alloc_err_str(&result.msg, "parser: %s\n%s\n", parse_result.msg, errctx);
    free(errctx);
    result.is_ok = false;
    result.errcode = parse_result.errcode;
    goto cleanup_after_parse;
  }

  clock_gettime(CLOCK_MONOTONIC, &parse_end);

  parse_elapsed = (parse_end.tv_sec - parse_start.tv_sec) +
                  (parse_end.tv_nsec - parse_start.tv_nsec) / 1e9;

  DynArray_Stmt raw_ast = parse_result.ast;

  clock_gettime(CLOCK_MONOTONIC, &loop_label_start);

  LoopLabelResult loop_label_result = loop_label_program(&raw_ast, NULL);
  if (!loop_label_result.is_ok) {
    char *errctx = mkerrctx(source, loop_label_result.span.line,
                            loop_label_result.span.start,
                            loop_label_result.span.end, 3, 3);
    alloc_err_str(&result.msg, "loop_labeler: %s\n%s\n", loop_label_result.msg,
                  errctx);
    result.is_ok = false;
    result.errcode = loop_label_result.errcode;
    goto cleanup_after_loop_label;
  }

  clock_gettime(CLOCK_MONOTONIC, &loop_label_end);

  loop_label_elapsed =
      (loop_label_end.tv_sec - loop_label_start.tv_sec) +
      (loop_label_end.tv_nsec - loop_label_start.tv_nsec) / 1e9;

  DynArray_Stmt labeled_ast = loop_label_result.as.ast;

  DynArray_Stmt optimized_ast = {0};

  if (args->optimize) {
    clock_gettime(CLOCK_MONOTONIC, &optimize_start);
    optimized_ast = optimize(&labeled_ast);
    clock_gettime(CLOCK_MONOTONIC, &optimize_end);
    optimize_elapsed = (optimize_end.tv_sec - optimize_start.tv_sec) +
                       (optimize_end.tv_nsec - optimize_start.tv_nsec) / 1e9;
  }

  if (args->parse) {
    if (args->optimize) {
      print_ast(&optimized_ast);
    } else {
      print_ast(&labeled_ast);
    }
    goto cleanup_after_loop_label;
  }

  clock_gettime(CLOCK_MONOTONIC, &compile_start);

  Compiler *compiler = current_compiler = new_compiler();

  CompileResult compile_result;
  if (args->optimize) {
    compile_result = compile(&optimized_ast);
  } else {
    compile_result = compile(&labeled_ast);
  }

  if (!compile_result.is_ok) {
    char *errctx =
        mkerrctx(source, compile_result.span.line, compile_result.span.start,
                 compile_result.span.end, 3, 3);
    alloc_err_str(&result.msg, "compiler: %s\n%s\n", compile_result.msg,
                  errctx);
    free(errctx);
    result.is_ok = false;
    result.errcode = compile_result.errcode;
    goto cleanup_after_compile;
  }

  clock_gettime(CLOCK_MONOTONIC, &compile_end);

  compile_elapsed = (compile_end.tv_sec - compile_start.tv_sec) +
                    (compile_end.tv_nsec - compile_start.tv_nsec) / 1e9;

  Bytecode *chunk = compile_result.chunk;

  DisassembleResult disassemble_result = {0};
  if (args->ir) {
    clock_gettime(CLOCK_MONOTONIC, &disassemble_start);

    disassemble_result = disassemble(chunk);
    if (!disassemble_result.is_ok) {
      alloc_err_str(&result.msg, "disassembler: %s\n", disassemble_result.msg);
      result.is_ok = false;
      result.errcode = disassemble_result.errcode;
    }

    clock_gettime(CLOCK_MONOTONIC, &disassemble_end);

    disassemble_elapsed =
        (disassemble_end.tv_sec - disassemble_start.tv_sec) +
        (disassemble_end.tv_nsec - disassemble_start.tv_nsec) / 1e9;

    goto cleanup_after_disassemble;
  }

  clock_gettime(CLOCK_MONOTONIC, &exec_start);

  VM vm;
  init_vm(&vm);

  ExecResult exec_result = exec(&vm, chunk);
  if (!exec_result.is_ok) {
    alloc_err_str(&result.msg, "vm: %s\n", exec_result.msg);
    result.is_ok = false;
    result.errcode = exec_result.errcode;
    goto cleanup_after_exec; /* just for the symmetry */
  }

  clock_gettime(CLOCK_MONOTONIC, &exec_end);

  exec_elapsed = (exec_end.tv_sec - exec_start.tv_sec) +
                 (exec_end.tv_nsec - exec_start.tv_nsec) / 1e9;

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
  } else {
    free(parse_result.msg);
  }

  if (args->optimize) {
    free_ast(&optimized_ast);
  }

cleanup_after_lex:
  dynarray_free(&tokenize_result.tokens);

  if (!tokenize_result.is_ok) {
    free(tokenize_result.msg);
  }

cleanup_after_read_file:
  if (read_file_result.is_ok) {
    free(source);
  } else {
    free(read_file_result.msg);
  }

  double total_all_stages = read_file_elapsed + tokenize_elapsed +
                            parse_elapsed + loop_label_elapsed +
                            optimize_elapsed + compile_elapsed +
                            disassemble_elapsed + exec_elapsed;

  if (args->measure_flags & MEASURE_READ_FILE) {
    printf("read_file stage took %.9f sec (%.2f%%)\n", read_file_elapsed,
           (read_file_elapsed / total_all_stages) * 100);
  }

  if (args->measure_flags & MEASURE_LEX) {
    printf("lex stage took %.9f sec (%.2f%%)\n", tokenize_elapsed,
           (tokenize_elapsed / total_all_stages) * 100);
  }

  if (args->measure_flags & MEASURE_PARSE) {
    printf("parse stage took %.9f sec (%.2f%%)\n", parse_elapsed,
           (parse_elapsed / total_all_stages) * 100);
  }

  if (args->measure_flags & MEASURE_LOOP_LABEL) {
    printf("loop_label stage took %.9f sec (%.2f%%)\n", loop_label_elapsed,
           (loop_label_elapsed / total_all_stages) * 100);
  }

  if (args->measure_flags & MEASURE_OPTIMIZE) {
    printf("optimize stage took %.9f sec (%.2f%%)\n", optimize_elapsed,
           (optimize_elapsed / total_all_stages) * 100);
  }

  if (args->measure_flags & MEASURE_DISASSEMBLE) {
    printf("disasm stage took %.9f sec (%.2f%%)\n", disassemble_elapsed,
           (disassemble_elapsed / total_all_stages) * 100);
  }

  if (args->measure_flags & MEASURE_COMPILE) {
    printf("compile stage took %.9f sec (%.2f%%)\n", compile_elapsed,
           (compile_elapsed / total_all_stages) * 100);
  }

  if (args->measure_flags & MEASURE_EXEC) {
    printf("exec stage took %.9f sec (%.2f%%)\n", exec_elapsed,
           (exec_elapsed / total_all_stages) * 100);
  }

  return result;
}

int main(int argc, char **argv)
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
