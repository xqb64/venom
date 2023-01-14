#include <stdio.h>
#include <stdlib.h>
#include "compiler.h"
#include "dynarray.h"
#include "tokenizer.h"
#include "parser.h"
#include "vm.h"

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    char *buffer = malloc(size+1);
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

void run_file(char *file) {
    char *source = read_file(file);

    DynArray_Statement stmts = {0};
    Parser parser;
    Tokenizer tokenizer;
    init_tokenizer(&tokenizer, source);
    init_parser(&parser);
    parse(&parser, &tokenizer, &stmts);

    BytecodeChunk chunk;
    init_chunk(&chunk);

    Compiler compiler;
    init_compiler(&compiler, 0);
    for (size_t i = 0; i < stmts.count; i++) {
        compile(&chunk, stmts.data[i]);
    }
    end_compiler(&compiler);

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