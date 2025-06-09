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
    char *errctx = mkerrctx(source, &tokenize_result.span, 3, 3);
    alloc_err_str(&result.msg, "tokenizer: %s\n%s\n", tokenize_result.msg,
                  errctx);
    free(errctx);
    result.is_ok = false;
    result.errcode = tokenize_result.errcode;
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
    char *errctx = mkerrctx(source, &parse_result.span, 3, 3);
    alloc_err_str(&result.msg, "parser: %s\n%s\n", parse_result.msg, errctx);
    free(errctx);
    result.is_ok = false;
    result.errcode = parse_result.errcode;
    goto cleanup_after_parse;
  }

  DynArray_Stmt raw_ast = parse_result.ast;

  LoopLabelResult loop_label_result = loop_label_program(&raw_ast, NULL);
  if (!loop_label_result.is_ok) {
    char *errctx = mkerrctx(source, &loop_label_result.span, 3, 3);
    alloc_err_str(&result.msg, "loop_labeler: %s\n%s\n", loop_label_result.msg,
                  errctx);
    free(errctx);
    result.is_ok = false;
    result.errcode = loop_label_result.errcode;
    goto cleanup_after_loop_label;
  }

  DynArray_Stmt labeled_ast = loop_label_result.as.ast;

  LabelCheckResult label_check_result = label_check_program(&labeled_ast);
  if (!label_check_result.is_ok) {
    char *errctx = mkerrctx(source, &label_check_result.span, 3, 3);
    alloc_err_str(&result.msg, "label_checker: %s\n%s\n",
                  label_check_result.msg, errctx);
    free(errctx);
    result.is_ok = false;
    result.errcode = label_check_result.errcode;
    goto cleanup_after_loop_label;
  }

  OptimizeResult optimize_result = {0};

  if (args->optimize) {
    optimize_result = optimize(&labeled_ast);
  }

  if (args->parse) {
    if (args->optimize) {
      print_ast(&optimize_result.payload);
    } else {
      print_ast(&labeled_ast);
    }
    goto cleanup_after_loop_label;
  }

  Compiler *compiler = current_compiler = new_compiler();

  CompileResult compile_result;
  if (args->optimize) {
    compile_result = compile(&optimize_result.payload);
  } else {
    compile_result = compile(&labeled_ast);
  }

  if (!compile_result.is_ok) {
    char *errctx = mkerrctx(source, &compile_result.span, 3, 3);
    alloc_err_str(&result.msg, "compiler: %s\n%s\n", compile_result.msg,
                  errctx);
    free(errctx);
    result.is_ok = false;
    result.errcode = compile_result.errcode;
    goto cleanup_after_compile;
  }

  Bytecode *chunk = compile_result.chunk;

  DisassembleResult disassemble_result = {0};
  if (args->ir) {
    disassemble_result = disassemble(chunk);
    if (!disassemble_result.is_ok) {
      alloc_err_str(&result.msg, "disassembler: %s\n", disassemble_result.msg);
      result.is_ok = false;
      result.errcode = disassemble_result.errcode;
    }

    goto cleanup_after_disassemble;
  }

  VM vm;
  init_vm(&vm);

  ExecResult exec_result = exec(&vm, chunk);
  if (!exec_result.is_ok) {
    alloc_err_str(&result.msg, "vm: %s\n", exec_result.msg);
    result.is_ok = false;
    result.errcode = exec_result.errcode;
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
    assert(chunk);
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
    free_ast(&optimize_result.payload);
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

  double total_all_stages = tokenize_result.time + parse_result.time +
                            loop_label_result.time + optimize_result.time +
                            compile_result.time + disassemble_result.time +
                            exec_result.time;

  if (args->measure_flags & MEASURE_LEX) {
    printf("lex stage took %.9f sec (%.2f%%)\n", tokenize_result.time,
           (tokenize_result.time / total_all_stages) * 100);
  }

  if (args->measure_flags & MEASURE_PARSE) {
    printf("parse stage took %.9f sec (%.2f%%)\n", parse_result.time,
           (parse_result.time / total_all_stages) * 100);
  }

  if (args->measure_flags & MEASURE_LOOP_LABEL) {
    printf("loop_label stage took %.9f sec (%.2f%%)\n", loop_label_result.time,
           (loop_label_result.time / total_all_stages) * 100);
  }

  if (args->measure_flags & MEASURE_OPTIMIZE) {
    printf("optimize stage took %.9f sec (%.2f%%)\n", optimize_result.time,
           (optimize_result.time / total_all_stages) * 100);
  }

  if (args->measure_flags & MEASURE_DISASSEMBLE) {
    printf("disasm stage took %.9f sec (%.2f%%)\n", disassemble_result.time,
           (disassemble_result.time / total_all_stages) * 100);
  }

  if (args->measure_flags & MEASURE_COMPILE) {
    printf("compile stage took %.9f sec (%.2f%%)\n", compile_result.time,
           (compile_result.time / total_all_stages) * 100);
  }

  if (args->measure_flags & MEASURE_EXEC) {
    printf("exec stage took %.9f sec (%.2f%%)\n", exec_result.time,
           (exec_result.time / total_all_stages) * 100);
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
