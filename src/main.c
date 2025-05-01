#include <getopt.h>
#include <stdio.h>

#include "ast.h"
#include "compiler.h"
#include "disassembler.h"
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

static int run(Arguments *args)
{
    int result = 0;

    char *source = read_file(args->file);

    Tokenizer tokenizer;
    init_tokenizer(&tokenizer, source);

    TokenizeResult tokenize_result = tokenize(&tokenizer);

    if (!tokenize_result.is_ok)
    {
        fprintf(stderr, "tokenizer: %s\n", tokenize_result.msg);
        result = -1;
        goto cleanup_after_lex;
    }

    DynArray_Token tokens = tokenize_result.tokens;

    if (args->lex)
    {
        print_tokens(&tokens);
        goto cleanup_after_lex;
    }

    Parser parser;
    init_parser(&parser, &tokens);

    ParseResult parse_result = parse(&parser);

    if (!parse_result.is_ok)
    {
        fprintf(stderr, "parser: %s\n", parse_result.msg);
        result = -1;
        goto cleanup_after_parse;
    }

    DynArray_Stmt raw_ast = parse_result.ast;

    LoopLabelResult loop_label_result = loop_label_program(&raw_ast, NULL);
    if (!loop_label_result.is_ok)
    {
        fprintf(stderr, "loop labeler: %s\n", loop_label_result.msg);
        result = -1;
        goto cleanup_after_parse;
    }

    DynArray_Stmt cooked_ast = loop_label_result.ast;

    if (args->parse)
    {
        pretty_print(&cooked_ast);
        goto cleanup_after_parse;
    }

    Compiler *compiler = current_compiler = new_compiler();

    Module current_mod = {.path = own_string(args->file), .imports = {0}, .parent = NULL};

    compiler->current_mod = ALLOC(current_mod);
    compiler->root_mod = compiler->current_mod->path;

    table_insert(compiler->compiled_modules, args->file, compiler->current_mod);

    CompileResult compile_result = compile(&cooked_ast);

    if (!compile_result.is_ok)
    {
        fprintf(stderr, "compiler: %s\n", compile_result.msg);
        result = -1;
        goto cleanup_after_compile;
    }

    Bytecode *chunk = compile_result.chunk;

    if (args->ir)
    {
        disassemble(chunk);
        goto cleanup_after_compile;
    }

    VM vm;
    init_vm(&vm);

    ExecResult exec_result = exec(&vm, chunk);
    if (!exec_result.is_ok)
    {
        fprintf(stderr, "vm: %s\n", exec_result.msg);
        result = -1;
        goto cleanup_after_exec; /* just for the symmetry */
    }

cleanup_after_exec:
    free_vm(&vm);

cleanup_after_compile:
    free_compiler(compiler);
    free(compiler);
    free_compile_result(&compile_result);

cleanup_after_parse:
    for (size_t i = 0; i < cooked_ast.count; i++)
    {
        free_stmt(cooked_ast.data[i]);
    }
    dynarray_free(&cooked_ast);

cleanup_after_lex:
    dynarray_free(&tokenize_result.tokens);
    free(source);

    return result;
}

static Arguments parse_args(int argc, char *argv[])
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

    int opt, opt_idx = 0;
    while ((opt = getopt_long(argc, argv, "lpi", long_opts, &opt_idx)) != -1)
    {
        switch (opt)
        {
            case 'l':
                do_lex = 1;
                break;
            case 'p':
                do_parse = 1;
                break;
            case 'i':
                do_ir = 1;
                break;
            default:
                fprintf(stderr, "usage: %s [--lex] [--parse] [--ir]\n", argv[0]);
                exit(1);
        }
    }

    if (do_lex + do_parse + do_ir > 1)
    {
        fprintf(stderr, "Please specify exactly one option.\n");
        exit(1);
    }

    Arguments args;

    args.lex = do_lex;
    args.parse = do_parse;
    args.ir = do_ir;
    args.file = argv[optind];

    return args;
}

int main(int argc, char *argv[])
{
    Arguments args;

    args = parse_args(argc, argv);
    int result = run(&args);

    return result;
}
