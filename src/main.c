#include <getopt.h>
#include <stdio.h>

#include "ast.h"
#include "compiler.h"
#include "disassembler.h"
#include "optimizer.h"
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
    int optimize;
    char *file;
} Arguments;

typedef struct
{
    Arguments args;
    bool is_ok;
    char *msg;
} ArgParseResult;

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

    if (args->optimize)
    {
        optimize(&cooked_ast);
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
    while ((opt = getopt_long(argc, argv, "lpio", long_opts, &opt_idx)) != -1)
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
            case 'o':
                do_optimize = 1;
                break;
            default:
                return (ArgParseResult) {.args = {0},
                                         .msg = "usage: %s [--lex] [--parse] [--ir] [--optimize]",
                                         .is_ok = false};
        }
    }

    if (do_lex + do_parse + do_ir > 1)
    {
        return (ArgParseResult) {
            .args = {0}, .msg = "Please specify exactly one option.", .is_ok = false};
    }

    Arguments args;

    args.lex = do_lex;
    args.parse = do_parse;
    args.ir = do_ir;
    args.optimize = do_optimize;
    args.file = argv[optind];

    return (ArgParseResult) {.args = args, .is_ok = true, .msg = NULL};
}

int main(int argc, char *argv[])
{
    ArgParseResult arg_parse_result;
    Arguments args;

    arg_parse_result = parse_args(argc, argv);
    if (!arg_parse_result.is_ok)
    {
        fprintf(stderr, "venom: %s\n", arg_parse_result.msg);
        return -1;
    }

    args = arg_parse_result.args;
    int result = run(&args);

    return result;
}
