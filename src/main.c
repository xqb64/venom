#include <stdio.h>
#include <stdlib.h>
#include "compiler.h"
#include "dynarray.h"
#include "tokenizer.h"
#include "parser.h"
#include "vm.h"
#include "util.h"

void run_file(char *file) {
    char *source = read_file(file);

    Tokenizer tokenizer;
    init_tokenizer(&tokenizer, source);

    Parser parser;
    init_parser(&parser);

    DynArray_Statement stmts = parse(&parser, &tokenizer);

    BytecodeChunk chunk;
    init_chunk(&chunk);

    Compiler compiler;
    init_compiler(&compiler);
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
    if (argc == 2) run_file(argv[1]);
    else printf("Usage: venom [file]\n");
}