#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

#include "compiler.h"
#include "disassembler.h"
#include "dynarray.h"
#include "parser.h"
#include "semantics.h"
#include "tokenizer.h"
#include "util.h"
#include "vm.h"

typedef struct
{
    int lex;
    int parse;
    int ir;
    char *file;
} Arguments;

void run_file(Arguments *args)
{
    char *source = read_file(args->file);

    Tokenizer tokenizer;
    init_tokenizer(&tokenizer, source);

    DynArray_Token tokens = tokenize(&tokenizer);

    if (args->lex)
    {
        print_tokens(&tokens);
        exit(0);
    }

    Parser parser;
    init_parser(&parser, &tokens);

    DynArray_Stmt raw_ast = parse(&parser);
    DynArray_Stmt cooked_ast = loop_label_program(raw_ast, NULL);

    if (args->parse)
    {
        print_ast(&cooked_ast);
        exit(0);
    }

    Bytecode chunk;
    init_chunk(&chunk);

    Compiler *compiler = current_compiler = new_compiler();

    Module current_mod = {.path = own_string(args->file), .imports = {0}, .parent = NULL};

    compiler->current_mod = ALLOC(current_mod);
    compiler->root_mod = compiler->current_mod->path;

    table_insert(compiler->compiled_modules, args->file, compiler->current_mod);

    for (size_t i = 0; i < cooked_ast.count; i++)
    {
        compile(&chunk, cooked_ast.data[i]);
    }

    dynarray_insert(&chunk.code, OP_HLT);
   
    free_compiler(compiler);
    free(compiler);

    if (args->ir)
    {
        disassemble(&chunk);
        exit(0);
    } 

    VM vm;
    init_vm(&vm);
    run(&vm, &chunk);
    free_vm(&vm);

    free_parser(&parser);

    for (size_t i = 0; i < cooked_ast.count; i++)
    {
        free_stmt(cooked_ast.data[i]);
    }
    dynarray_free(&cooked_ast);

    free_chunk(&chunk);
    free(source);
}

Arguments *parse_args(int argc, char *argv[])
{
    static const struct option long_opts[] = {
        {"lex", no_argument, 0, 'l'},
        {"parse", no_argument, 0, 'p'},
        {"ir", no_argument, 0, 'i'},
        {0, 0, 0, 0},
    };

    int do_lex = 0;
    int do_parse = 0;
    int do_ir = 0;

    int opt, idx;
    while ((opt = getopt_long(argc, argv, "lpi", long_opts, &idx)) != -1)
    {
        switch (opt)
        {
            case 'l': do_lex = 1; break;
            case 'p': do_parse = 1; break;
            case 'i': do_ir = 1; break;
            default:
                fprintf(stderr, "usage: %s [--lex] [--parse] [--ir]\n", argv[0]);
                return NULL;
        }
    }

    if (do_lex + do_parse + do_ir > 1)
    {
        fprintf(stderr, "Please specify exactly one option.\n");
        return NULL;
    }

    Arguments *args = malloc(sizeof(Arguments));
    
    args->lex = do_lex;
    args->parse = do_parse;
    args->ir = do_ir;
    args->file = argv[optind];

    return args;
}

int main(int argc, char *argv[])
{
    Arguments *args;

    args = parse_args(argc, argv);
    if (!args)
        exit(1);

    run_file(args);

    free(args);
}