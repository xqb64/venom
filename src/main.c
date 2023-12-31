#include <stdio.h>
#include <stdlib.h>

#include "compiler.h"
#include "dynarray.h"
#include "parser.h"
#include "tokenizer.h"
#include "util.h"
#include "vm.h"

void run_file(char *file) {
  char *source = read_file(file);

  Tokenizer tokenizer;
  init_tokenizer(&tokenizer, source);

  Parser parser;
  init_parser(&parser);

  DynArray_Stmt stmts = parse(&parser, &tokenizer);

  Bytecode chunk;
  init_chunk(&chunk);

  Compiler compiler;
  init_compiler(&compiler);

  struct module current_mod =
      (struct module){.path = own_string(file), .imports = {0}, .parent = NULL};

  compiler.current_mod = ALLOC(current_mod);
  compiler.root_mod = compiler.current_mod->path;

  table_insert(compiler.compiled_modules, file, compiler.current_mod);

  for (size_t i = 0; i < stmts.count; i++) {
    compile(&compiler, &chunk, stmts.data[i]);
  }

  free_compiler(&compiler);

  VM vm;
  init_vm(&vm);
  run(&vm, &chunk);
  free_vm(&vm);

  for (size_t i = 0; i < stmts.count; i++) {
    free_stmt(stmts.data[i]);
  }
  dynarray_free(&stmts);
  free_chunk(&chunk);
  free(source);
}

int main(int argc, char *argv[]) {
  if (argc == 2)
    run_file(argv[1]);
  else
    printf("Usage: venom [file]\n");
}
