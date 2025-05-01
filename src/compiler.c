#include "compiler.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "dynarray.h"
#include "parser.h"
#include "semantics.h"
#include "table.h"
#include "tokenizer.h"
#include "util.h"

#ifdef venom_debug_compiler
static bool is_last(Module *parent, Module *child)
{
    return strcmp(parent->imports.data[parent->imports.count - 1]->path, child->path) == 0;
}

static void print_prefix(int depth, bool is_grandparent_last)
{
    for (int i = 0; i < depth; i++)
    {
        if (i == 0)
            continue;
        if (is_grandparent_last)
            printf("  ");
        else
            printf("┃ ");
    }
}

static void print_module_tree(Compiler *compiler, Module *parent, Module *mod, int depth,
                              bool is_parent_last)
{
    bool last = is_last(parent, mod);

    if (depth == 0)
    {
        printf("%s\n", mod->path);
    }
    else
    {
        bool is_grandparent_last = parent->parent && is_last(parent->parent, parent);
        print_prefix(depth, is_grandparent_last);
        char *prefix = last ? "┗━" : "┣━";
        printf("%s %s\n", prefix, mod->path);
    }

    for (size_t i = 0; i < mod->imports.count; i++)
    {
        print_module_tree(compiler, mod, mod->imports.data[i], depth + 1, last);
    }
}
#endif

#define COMPILER_ERROR(...)                                 \
    do                                                      \
    {                                                       \
        alloc_err_str(&compile_result->msg, __VA_ARGS__);   \
        compile_result->is_ok = false;                      \
        return -1;                                          \
    } while (0)

typedef struct
{
    char *name;
    size_t argcount;
} Builtin;

static Builtin builtins[] = {
    {"next", 1}, {"len", 1}, {"hasattr", 2}, {"getattr", 2}, {"setattr", 3},
};

Compiler *current_compiler = NULL;

static void free_table_int(Table_int *table)
{
    for (size_t i = 0; i < TABLE_MAX; i++)
    {
        if (table->indexes[i] != NULL)
        {
            Bucket *bucket = table->indexes[i];
            list_free(bucket);
        }
    }
}

static void free_table_function_ptr(Table_FunctionPtr *table)
{
    for (size_t i = 0; i < TABLE_MAX; i++)
    {
        if (table->indexes[i] != NULL)
        {
            Bucket *bucket = table->indexes[i];
            list_free(bucket);
        }
    }
    for (size_t i = 0; i < table->count; i++)
    {
        free(table->items[i]);
    }
}

void free_table_struct_blueprints(Table_StructBlueprint *table)
{
    for (size_t i = 0; i < TABLE_MAX; i++)
    {
        if (table->indexes[i] != NULL)
        {
            Bucket *bucket = table->indexes[i];
            list_free(bucket);
        }
    }
    for (size_t i = 0; i < table->count; i++)
    {
        free_table_int(table->items[i].property_indexes);
        free(table->items[i].property_indexes);

        free_table_function_ptr(table->items[i].methods);
        free(table->items[i].methods);
    }
}

static void free_table_functions(Table_Function *table)
{
    for (size_t i = 0; i < TABLE_MAX; i++)
    {
        if (table->indexes[i] != NULL)
        {
            Bucket *bucket = table->indexes[i];
            list_free(bucket);
        }
    }
}

static void free_table_compiled_modules(Table_module_ptr *table)
{
    for (size_t i = 0; i < TABLE_MAX; i++)
    {
        if (table->indexes[i] != NULL)
        {
            Bucket *bucket = table->indexes[i];
            list_free(bucket);
        }
    }
    for (size_t i = 0; i < table->count; i++)
    {
        free(table->items[i]->path);
        dynarray_free(&table->items[i]->imports);
        free(table->items[i]);
    }
}

static void free_table_labels(Table_Label *table)
{
    for (size_t i = 0; i < TABLE_MAX; i++)
    {
        if (table->indexes[i] != NULL)
        {
            Bucket *bucket = table->indexes[i];
            list_free(bucket);
        }
    }
}

void free_compiler(Compiler *compiler)
{
    dynarray_free(&compiler->upvalues);
    dynarray_free(&compiler->loop_depths);
    free_table_struct_blueprints(compiler->struct_blueprints);
    free(compiler->struct_blueprints);
    free_table_functions(compiler->functions);
    free(compiler->functions);
    free_table_compiled_modules(compiler->compiled_modules);
    free(compiler->compiled_modules);
    free_table_function_ptr(compiler->builtins);
    free(compiler->builtins);
    free_table_labels(compiler->labels);
    free(compiler->labels);
}

void init_chunk(Bytecode *code)
{
    memset(code, 0, sizeof(Bytecode));
}

void free_chunk(Bytecode *code)
{
    dynarray_free(&code->code);
    for (size_t i = 0; i < code->sp.count; i++)
        free(code->sp.data[i]);
    dynarray_free(&code->sp);
}

/* Check if the string is already present in the sp.
 * If not, add it first, and finally return the idx. */
static uint32_t add_string(Bytecode *code, char *string)
{
    for (size_t idx = 0; idx < code->sp.count; idx++)
    {
        if (strcmp(code->sp.data[idx], string) == 0)
        {
            return idx;
        }
    }
    dynarray_insert(&code->sp, own_string(string));
    return code->sp.count - 1;
}

static void emit_byte(Bytecode *code, uint8_t byte)
{
    dynarray_insert(&code->code, byte);
}

static void emit_bytes(Bytecode *code, int n, ...)
{
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++)
    {
        uint8_t byte = va_arg(ap, int);
        emit_byte(code, byte);
    }
    va_end(ap);
}

static void emit_uint32(Bytecode *code, uint32_t idx)
{
    emit_bytes(code, 4, (idx >> 24) & 0xFF, (idx >> 16) & 0xFF, (idx >> 8) & 0xFF, idx & 0xFF);
}

static void emit_double(Bytecode *code, double x)
{
    union {
        double d;
        uint64_t raw;
    } num;
    num.d = x;
    emit_bytes(code, 8, (uint8_t) ((num.raw >> 56) & 0xFF), (uint8_t) ((num.raw >> 48) & 0xFF),
               (uint8_t) ((num.raw >> 40) & 0xFF), (uint8_t) ((num.raw >> 32) & 0xFF),
               (uint8_t) ((num.raw >> 24) & 0xFF), (uint8_t) ((num.raw >> 16) & 0xFF),
               (uint8_t) ((num.raw >> 8) & 0xFF), (uint8_t) (num.raw & 0xFF));
}

static int emit_placeholder(Bytecode *code, Opcode op)
{
    emit_bytes(code, 3, op, 0xFF, 0xFF);
    /* The opcode, followed by its 2-byte offset are the last
     * emitted bytes.
     *
     * e.g. if `code->code.data` is:
     *
     * [OP_CONST, a0, a1, a2, a3, // 4-byte operands
     *  OP_CONST, b0, b1, b2, b3, // 4-byte operands
     *  OP_EQ,
     *  OP_JZ, c0, c1]
     *                 ^-- `code->code.count`
     *
     * `code->code.count` will be 14. Since the indexing is
     * 0-based, the count points just beyond the 2-byte off-
     * set. To get the opcode position, we need to go back 3
     * slots (two-byte operand + one more slot to adjust for
     * zero-based indexing). */
    return code->code.count - 3;
}

static void patch_placeholder(Bytecode *code, int op)
{
    /* This function takes a zero-based index of the opcode,
     * 'op', and patches the following offset with the numb-
     * er of emitted instructions that come after the opcode
     * and the placeholder.
     *
     * For example, if we have:
     *
     * [OP_CONST, a0, a1, a2, a3, // 4-byte operands
     *  OP_CONST, b0, b1, b2, b3, // 4-byte operands
     *  OP_EQ,
     *  OP_JZ, c0, c1,            // 2-byte operand
     *  OP_STR, d0, d1, d2, d3    // 4-byte operand
     *  OP_PRINT]
     *             ^-- `code->code.count`
     *
     * 'op' will be 11. To get the count of emitted instruc-
     * tions, the count is adjusted by subtracting 1 (so th-
     * at it points to the last element). Then, two is added
     * to the index to account for the two-byte operand that
     * comes after the opcode. The result of the subtraction
     * of these two is the number of emitted bytes, which is
     * used to build a signed 16-bit offset to patch the pl-
     * aceholder. */
    int16_t bytes_emitted = (code->code.count - 1) - (op + 2);
    code->code.data[op + 1] = (bytes_emitted >> 8) & 0xFF;
    code->code.data[op + 2] = bytes_emitted & 0xFF;
}

static void add_upvalue(DynArray_int *upvalues, int idx)
{
    for (size_t i = 0; i < upvalues->count; i++)
        if (upvalues->data[i] == idx)
            return;
    dynarray_insert(upvalues, idx);
}

static void emit_loop(Bytecode *code, int loop_start)
{
    /*
     * For example, consider the following bytecode for a simp-
     * le program on the side:
     *
     *  0: OP_CONST (value: 0)           |                    |
     *  5: OP_SET_GLOBAL (name: x)       |                    |
     *  10: OP_GET_GLOBAL (name: x)      |                    |
     *  15: OP_CONST (value: 5)          |   let x = 0;       |
     *  20: OP_LT                        |   while (x < 5) {  |
     *  21: OP_JZ + 2-byte offset: 25    |     print x;       |
     *  24: OP_GET_GLOBAL (name: x)      |     x = x + 1;     |
     *  29: OP_PRINT                     |   }                |
     *  30: OP_GET_GLOBAL (name: x)      |                    |
     *  35: OP_CONST (value: 1)          |                    |
     *  40: OP_ADD                       |                    |
     *  41: OP_SET_GLOBAL (name: x)      |                    |
     *  46: OP_JMP + 2-byte offset: -39  |                    |
     *
     *
     * In this case, the loop starts at `10`, OP_GET_GLOBAL.
     *
     * After emitting OP_JMP, `code->code.count` will be 47, and
     * it'll point to just beyond the end of the bytecode. To get
     * back to the beginning of the loop, we need to go backwards
     * 37 bytes:
     *
     *  `code->code.count` - `loop_start` = 47 - 10 = 37
     *
     * Or do we?
     *
     * By the time the vm is ready to jump, it will have read the
     * 2-byte offset as well, meaning we do not need to jump from
     * index `46`, but from `48`. So, we need to go back 39 bytes
     * and not 37, hence the +2 below:
     *
     *  `code->code.count` + 2 - `loop_start` = 47 + 2 - 10 = 39
     *
     * When we perform the jump, we will be at index `48`, so:
     *
     *   48 - 39 = 9
     *
     * Which is one byte before the beginning of the loop.
     *
     * This is exactly where we want to end up because we're rel-
     * ying on the vm to increment the instruction pointer by one
     * after having previously set it in the op_jmp handler. */
    emit_byte(code, OP_JMP);
    int16_t offset = -(code->code.count + 2 - loop_start);
    emit_byte(code, (offset >> 8) & 0xFF);
    emit_byte(code, offset & 0xFF);
}

static void emit_loop_cleanup(Bytecode *code)
{
    /* Example:
     *
     * fn main() {
     *     let x = 0;
     *     while (x < 5) {
     *         let e = 16;
     *         let a = 32;
     *         let b = 64;
     *         let z = x + 1;
     *         print z;
     *         x += 1;
     *         if (x == 2) {
     *             let egg = 128;
     *             let spam = 256;
     *             continue;
     *         }
     *     }
     *     return 0;
     * }
     * main();
     *
     * current_compiler->loop_depth = 1
     * current_compiler->depth = 3
     *
     * current_compiler->pops[2] = 4
     * current_compiler->pops[3] = 2
     *
     * We want to clean up everything deeper than the loop up to the
     * current current_compiler->depth. */
    size_t popcount = current_compiler->locals_count;

    int loop_depth = dynarray_peek(&current_compiler->loop_depths);

    while (current_compiler->locals_count > 0 &&
           current_compiler->locals[current_compiler->locals_count - 1].depth > loop_depth)
    {
        emit_byte(code, OP_POP);
        current_compiler->locals_count--;
    }
}

static void begin_scope()
{
    current_compiler->depth++;
}

static void end_scope(Bytecode *code)
{
    Compiler *c = current_compiler;
    c->depth--;
    while (c->locals_count > 0 && c->locals[c->locals_count - 1].depth > c->depth)
    {
        emit_byte(code, OP_POP);
        c->locals_count--;
    }
}

static void patch_jumps(Bytecode *code)
{
    for (size_t i = 0; i < TABLE_MAX; i++)
    {
        if (current_compiler->labels->indexes[i])
        {
            Label *l =
                table_get(current_compiler->labels, current_compiler->labels->indexes[i]->key);

            int location = l->location;
            int patch_with = l->patch_with;

            if (patch_with == -1)
                continue;

            code->code.data[location + 1] = ((patch_with - location - 3) >> 8) & 0xFF;
            code->code.data[location + 2] = ((patch_with) - (location) -3) & 0xFF;
        }
    }
}

/* Check if 'name' is present in the builtins table.
 * If it is, return its index in the sp, otherwise -1. */
static Function *resolve_builtin(char *name)
{
    Compiler *current = current_compiler;
    while (current)
    {
        Function **f = table_get(current->builtins, name);
        if (f)
            return *f;
        current = current->next;
    }
    return NULL;
}

/* Check if 'name' is present in the globals dynarray.
 * If it is, return its index in the sp, otherwise -1. */
static int resolve_global(Bytecode *code, char *name)
{
    Compiler *current = current_compiler;
    while (current)
    {
        for (size_t idx = 0; idx < current->globals_count; idx++)
        {
            if (strcmp(current->globals[idx].name, name) == 0)
                return add_string(code, name);
        }
        current = current->next;
    }
    return -1;
}

/* Check if 'name' is present in the locals dynarray.
 * If it is, return the index, otherwise return -1. */
static int resolve_local(char *name)
{
    for (size_t idx = 0; idx < current_compiler->locals_count; idx++)
    {
        if (strcmp(current_compiler->locals[idx].name, name) == 0)
            return idx;
    }
    return -1;
}

static int resolve_upvalue(char *name)
{
    Compiler *current = current_compiler->next;
    while (current)
    {
        for (size_t idx = 0; idx < current->locals_count; idx++)
        {
            if (strcmp(current->locals[idx].name, name) == 0)
            {
                current->locals[idx].captured = true;
                return idx;
            }
        }
        current = current->next;
    }
    return -1;
}

static StructBlueprint *resolve_blueprint(char *name)
{
    Compiler *current = current_compiler;
    while (current)
    {
        StructBlueprint *bp = table_get(current->struct_blueprints, name);
        if (bp)
            return bp;
        current = current->next;
    }
    return NULL;
}

static Module **resolve_module(char *name)
{
    Compiler *current = current_compiler;
    while (current)
    {
        Module **mp = table_get(current->compiled_modules, name);
        if (mp)
            return mp;
        current = current->next;
    }
    return NULL;
}

static Function *resolve_func(char *name)
{
    Compiler *current = current_compiler;
    while (current)
    {
        Function *f = table_get(current->functions, name);
        if (f)
            return f;
        current = current->next;
    }
    return NULL;
}

#define COMPILE_EXPR(code, exp)                         \
    result = compile_expr(code, (exp), compile_result); \
    if (result == -1)                                   \
        return result;

#define COMPILE_STMT(code, stmt)                         \
    result = compile_stmt(code, (stmt), compile_result); \
    if (result == -1)                                    \
        return result;

static int compile_expr(Bytecode *code, Expr exp, CompileResult *compile_result);

static int compile_expr_lit(Bytecode *code, Expr exp, CompileResult *compile_result)
{
    int result = 0;

    ExprLit e = TO_EXPR_LIT(exp);
    switch (e.kind)
    {
        case LIT_BOOL: {
            emit_byte(code, OP_TRUE);
            if (!e.as.bval)
            {
                emit_byte(code, OP_NOT);
            }
            break;
        }
        case LIT_NUM: {
            emit_byte(code, OP_CONST);
            emit_double(code, e.as.dval);
            break;
        }
        case LIT_STR: {
            uint32_t str_idx = add_string(code, e.as.sval);
            emit_byte(code, OP_STR);
            emit_uint32(code, str_idx);
            break;
        }
        case LIT_NULL: {
            emit_byte(code, OP_NULL);
            break;
        }
        default:
            assert(0);
    }

    return result;
}

static int compile_expr_var(Bytecode *code, Expr exp, CompileResult *compile_result)
{
    int result = 0;
    ExprVar e = TO_EXPR_VAR(exp);

    /* Try to resolve the variable as local. */
    int idx = resolve_local(e.name);
    if (idx != -1)
    {
        emit_byte(code, OP_DEEPGET);
        emit_uint32(code, idx);
        return result;
    }

    /* Try to resolve the variable as upvalue. */
    int upvalue_idx = resolve_upvalue(e.name);
    if (upvalue_idx != -1)
    {
        emit_byte(code, OP_GET_UPVALUE);
        emit_uint32(code, upvalue_idx);
        add_upvalue(&current_compiler->upvalues, upvalue_idx);
        return result;
    }

    /* Try to resolve the variable as global. */
    int name_idx = resolve_global(code, e.name);
    if (name_idx != -1)
    {
        emit_byte(code, OP_GET_GLOBAL);
        emit_uint32(code, name_idx);
        return result;
    }

    /* The variable is not defined, bail out. */
    COMPILER_ERROR("Variable '%s' is not defined.", e.name);
}

static int compile_expr_una(Bytecode *code, Expr exp, CompileResult *compile_result)
{
    int result = 0;

    ExprUnary e = TO_EXPR_UNA(exp);
    if (strcmp(e.op, "-") == 0)
    {
        COMPILE_EXPR(code, *e.exp);
        emit_byte(code, OP_NEG);
    }
    else if (strcmp(e.op, "!") == 0)
    {
        COMPILE_EXPR(code, *e.exp);
        emit_byte(code, OP_NOT);
    }
    else if (strcmp(e.op, "*") == 0)
    {
        COMPILE_EXPR(code, *e.exp);
        emit_byte(code, OP_DEREF);
    }
    else if (strcmp(e.op, "&") == 0)
    {
        switch (e.exp->kind)
        {
            case EXPR_VAR: {
                ExprVar var = TO_EXPR_VAR(*e.exp);

                /* Try to resolve the variable as local. */
                int idx = resolve_local(var.name);
                if (idx != -1)
                {
                    emit_byte(code, OP_DEEPGET_PTR);
                    emit_uint32(code, idx);
                    return result;
                }

                /* Try to resolve the variable as upvalue. */
                int upvalue_idx = resolve_upvalue(var.name);
                if (upvalue_idx != -1)
                {
                    emit_byte(code, OP_GET_UPVALUE_PTR);
                    emit_uint32(code, upvalue_idx);
                    return result;
                }

                int name_idx = resolve_global(code, var.name);
                if (name_idx != -1)
                {
                    emit_byte(code, OP_GET_GLOBAL_PTR);
                    emit_uint32(code, name_idx);
                    return result;
                }

                /* The variable is not defined, bail out. */
                COMPILER_ERROR("Variable '%s' is not defined.", var.name);

                break;
            }
            case EXPR_GET: {
                ExprGet getexp = TO_EXPR_GET(*e.exp);
                /* Compile the part that comes be-
                 * fore the member access operator. */
                COMPILE_EXPR(code, *getexp.exp);
                /* Deref if the operator is '->'. */
                if (strcmp(getexp.op, "->") == 0)
                {
                    emit_byte(code, OP_DEREF);
                }
                /* Add the 'property_name' string to the
                 * chunk's sp, and emit OP_GETATTR_PTR. */
                uint32_t property_name_idx = add_string(code, getexp.property_name);
                emit_byte(code, OP_GETATTR_PTR);
                emit_uint32(code, property_name_idx);
                break;
            }
            default:
                break;
        }
    }
    else if (strcmp(e.op, "~") == 0)
    {
        COMPILE_EXPR(code, *e.exp);
        emit_byte(code, OP_BITNOT);
    }

    return result;
}

static int compile_expr_bin(Bytecode *code, Expr exp, CompileResult *compile_result)
{
    int result = 0;

    ExprBin e = TO_EXPR_BIN(exp);

    COMPILE_EXPR(code, *e.lhs);
    COMPILE_EXPR(code, *e.rhs);

    if (strcmp(e.op, "+") == 0)
    {
        emit_byte(code, OP_ADD);
    }
    else if (strcmp(e.op, "-") == 0)
    {
        emit_byte(code, OP_SUB);
    }
    else if (strcmp(e.op, "*") == 0)
    {
        emit_byte(code, OP_MUL);
    }
    else if (strcmp(e.op, "/") == 0)
    {
        emit_byte(code, OP_DIV);
    }
    else if (strcmp(e.op, "%%") == 0)
    {
        emit_byte(code, OP_MOD);
    }
    else if (strcmp(e.op, "&") == 0)
    {
        emit_byte(code, OP_BITAND);
    }
    else if (strcmp(e.op, "|") == 0)
    {
        emit_byte(code, OP_BITOR);
    }
    else if (strcmp(e.op, "^") == 0)
    {
        emit_byte(code, OP_BITXOR);
    }
    else if (strcmp(e.op, ">") == 0)
    {
        emit_byte(code, OP_GT);
    }
    else if (strcmp(e.op, "<") == 0)
    {
        emit_byte(code, OP_LT);
    }
    else if (strcmp(e.op, ">=") == 0)
    {
        emit_bytes(code, 2, OP_LT, OP_NOT);
    }
    else if (strcmp(e.op, "<=") == 0)
    {
        emit_bytes(code, 2, OP_GT, OP_NOT);
    }
    else if (strcmp(e.op, "==") == 0)
    {
        emit_byte(code, OP_EQ);
    }
    else if (strcmp(e.op, "!=") == 0)
    {
        emit_bytes(code, 2, OP_EQ, OP_NOT);
    }
    else if (strcmp(e.op, "<<") == 0)
    {
        emit_byte(code, OP_BITSHL);
    }
    else if (strcmp(e.op, ">>") == 0)
    {
        emit_byte(code, OP_BITSHR);
    }
    else if (strcmp(e.op, "++") == 0)
    {
        emit_byte(code, OP_STRCAT);
    }

    return result;
}

static int compile_expr_call(Bytecode *code, Expr exp, CompileResult *compile_result)
{
    int result = 0;

    ExprCall e = TO_EXPR_CALL(exp);

    if (e.callee->kind == EXPR_GET)
    {
        ExprGet getexp = TO_EXPR_GET(*e.callee);

        /* Compile the part that comes before the member access
         * operator. */
        COMPILE_EXPR(code, *getexp.exp);

        /* Deref it if the operator is -> */
        if (strcmp(getexp.op, "->") == 0)
        {
            emit_byte(code, OP_DEREF);
        }

        char *method = getexp.property_name;

        for (size_t i = 0; i < e.arguments.count; i++)
        {
            COMPILE_EXPR(code, e.arguments.data[i]);
        }

        emit_byte(code, OP_CALL_METHOD);
        emit_uint32(code, add_string(code, method));
        emit_uint32(code, e.arguments.count);
    }
    else if (e.callee->kind == EXPR_VAR)
    {
        ExprVar var = TO_EXPR_VAR(*e.callee);

        Function *b = resolve_builtin(var.name);
        if (b)
        {
            if (strcmp(b->name, "next") == 0)
            {
                for (size_t i = 0; i < e.arguments.count; i++)
                {
                    COMPILE_EXPR(code, e.arguments.data[i]);
                }
                emit_byte(code, OP_RESUME);
            }
            else if (strcmp(b->name, "len") == 0)
            {
                for (size_t i = 0; i < e.arguments.count; i++)
                {
                    COMPILE_EXPR(code, e.arguments.data[i]);
                }
                emit_byte(code, OP_LEN);
            }
            else if (strcmp(b->name, "hasattr") == 0)
            {
                for (size_t i = 0; i < e.arguments.count; i++)
                {
                    COMPILE_EXPR(code, e.arguments.data[i]);
                }
                emit_byte(code, OP_HASATTR);
            }
            else if (strcmp(b->name, "getattr") == 0)
            {
                COMPILE_EXPR(code, e.arguments.data[0]);
                emit_bytes(code, 1, OP_GETATTR);
                emit_uint32(code, add_string(code, e.arguments.data[1].as.expr_lit.as.sval));
            }
            else if (strcmp(b->name, "setattr") == 0)
            {
                COMPILE_EXPR(code, e.arguments.data[0]);
                COMPILE_EXPR(code, e.arguments.data[2]);

                emit_byte(code, OP_SETATTR);
                emit_uint32(code, add_string(code, e.arguments.data[1].as.expr_lit.as.sval));
            }

            return 0;
        }

        Function *f = resolve_func(var.name);
        if (f && f->paramcount != e.arguments.count)
        {
            COMPILER_ERROR("Function '%s' requires %ld arguments.", f->name, f->paramcount);
        }

        /* Then compile the arguments */
        for (size_t i = 0; i < e.arguments.count; i++)
        {
            COMPILE_EXPR(code, e.arguments.data[i]);
        }

        bool is_global = false;
        bool is_upvalue = false;

        /* Try to resolve the variable as local. */
        int idx = resolve_local(var.name);

        /* If it is not a local, try resolving it as upvalue. */
        if (idx == -1)
        {
            idx = resolve_upvalue(var.name);
            if (idx != -1)
                is_upvalue = true;
        }

        /* If it is not an upvalue, try resolving it as global. */
        if (idx == -1)
        {
            idx = resolve_global(code, var.name);
            if (idx != -1)
                is_global = true;
        }

        /* Bail out if it's neither local nor a global. */
        if (idx == -1)
        {
            COMPILER_ERROR("Function '%s' is not defined.", var.name);
            return -1;
        }

        if (is_global)
        {
            emit_byte(code, OP_GET_GLOBAL);
            emit_uint32(code, idx);
        }
        else if (is_upvalue)
        {
            emit_byte(code, OP_GET_UPVALUE);
            emit_uint32(code, idx);
            add_upvalue(&current_compiler->upvalues, idx);
        }
        else
        {
            emit_byte(code, OP_DEEPGET);
            emit_uint32(code, idx);
        }

        if (f && f->is_gen)
            emit_byte(code, OP_MKGEN);
        else
            /* Emit OP_CALL followed by the argument count. */
            emit_bytes(code, 2, OP_CALL, e.arguments.count);
    }

    return result;
}

static int compile_expr_get(Bytecode *code, Expr exp, CompileResult *compile_result)
{
    int result = 0;

    ExprGet e = TO_EXPR_GET(exp);

    /* Compile the part that comes before the member access
     * operator. */
    COMPILE_EXPR(code, *e.exp);

    /* Deref it if the operator is -> */
    if (strcmp(e.op, "->") == 0)
    {
        emit_byte(code, OP_DEREF);
    }

    /* Emit OP_GETATTR with the index of the property name. */
    emit_byte(code, OP_GETATTR);
    emit_uint32(code, add_string(code, e.property_name));

    return result;
}

static void handle_specop(Bytecode *code, const char *op)
{
    if (strcmp(op, "+=") == 0)
        emit_byte(code, OP_ADD);
    else if (strcmp(op, "-=") == 0)
        emit_byte(code, OP_SUB);
    else if (strcmp(op, "*=") == 0)
        emit_byte(code, OP_MUL);
    else if (strcmp(op, "/=") == 0)
        emit_byte(code, OP_DIV);
    else if (strcmp(op, "%%=") == 0)
        emit_byte(code, OP_MOD);
    else if (strcmp(op, "&=") == 0)
        emit_byte(code, OP_BITAND);
    else if (strcmp(op, "|=") == 0)
        emit_byte(code, OP_BITOR);
    else if (strcmp(op, "^=") == 0)
        emit_byte(code, OP_BITXOR);
    else if (strcmp(op, ">>=") == 0)
        emit_byte(code, OP_BITSHR);
    else if (strcmp(op, "<<=") == 0)
        emit_byte(code, OP_BITSHL);
}

static int compile_assign_var(Bytecode *code, ExprAssign e, bool is_compound,
                              CompileResult *compile_result)
{
    int result = 0;

    ExprVar var = TO_EXPR_VAR(*e.lhs);

    bool is_global = false;
    bool is_upvalue = false;

    /* Try to resolve the variable as local. */
    int idx = resolve_local(var.name);

    /* If it is not an upvalue, try resolving it as global. */
    if (idx == -1)
    {
        idx = resolve_upvalue(var.name);
        if (idx != -1)
            is_upvalue = true;
    }

    /* If it is not an upvalue, try resolving it as global. */
    if (idx == -1)
    {
        idx = resolve_global(code, var.name);
        if (idx != -1)
            is_global = true;
    }

    /* Bail out if it's neither local nor a global. */
    if (idx == -1)
    {
        COMPILER_ERROR("Variable '%s' is not defined.", var.name);
    }

    if (is_compound)
    {
        /* Get the variable onto the top of the stack. */
        if (is_global)
            emit_byte(code, OP_GET_GLOBAL);
        else if (is_upvalue)
            emit_byte(code, OP_GET_UPVALUE);
        else
            emit_byte(code, OP_DEEPGET);

        emit_uint32(code, idx);

        /* Compile the right-hand side. */
        COMPILE_EXPR(code, *e.rhs);

        /* Handle the compound assignment. */
        handle_specop(code, e.op);
    }
    else
    {
        /* We don't need to get the variable onto the top of
         * the stack, because this is a regular assignment. */
        COMPILE_EXPR(code, *e.rhs);
    }

    /* Emit the appropriate assignment opcode. */
    if (is_global)
        emit_byte(code, OP_SET_GLOBAL);
    else if (is_upvalue)
        emit_byte(code, OP_SET_UPVALUE);
    else
        emit_byte(code, OP_DEEPSET);

    emit_uint32(code, idx);

    return result;
}

static int compile_assign_get(Bytecode *code, ExprAssign e, bool is_compound,
                              CompileResult *compile_result)
{
    int result = 0;

    ExprGet getexp = TO_EXPR_GET(*e.lhs);

    /* Compile the part that comes before the member access operator. */
    COMPILE_EXPR(code, *getexp.exp);

    /* Deref it if the operator is -> */
    if (strcmp(getexp.op, "->") == 0)
    {
        emit_byte(code, OP_DEREF);
    }

    if (is_compound)
    {
        /* Get the property onto the top of the stack. */
        emit_byte(code, OP_GETATTR);
        emit_uint32(code, add_string(code, getexp.property_name));

        /* Compile the right-hand side of the assignment. */
        COMPILE_EXPR(code, *e.rhs);

        /* Handle the compound assignment. */
        handle_specop(code, e.op);
    }
    else
    {
        COMPILE_EXPR(code, *e.rhs);
    }

    /* Set the property name to the rhs of the get expr. */
    emit_byte(code, OP_SETATTR);
    emit_uint32(code, add_string(code, getexp.property_name));

    /* Pop the struct off the stack. */
    emit_byte(code, OP_POP);

    return result;
}

static int compile_assign_una(Bytecode *code, ExprAssign e, bool is_compound,
                              CompileResult *compile_result)
{
    int result = 0;

    ExprUnary unary = TO_EXPR_UNA(*e.lhs);

    /* Compile the inner expression. */
    COMPILE_EXPR(code, *unary.exp);
    if (is_compound)
    {
        /* Compile the right-hand side of the assignment. */
        COMPILE_EXPR(code, *e.rhs);

        /* Handle the compound assignment. */
        handle_specop(code, e.op);
    }
    else
    {
        COMPILE_EXPR(code, *e.rhs);
    }

    /* Emit OP_DEREFSET. */
    emit_byte(code, OP_DEREFSET);

    return result;
}

static int compile_assign_sub(Bytecode *code, ExprAssign e, bool is_compound,
                              CompileResult *compile_result)
{
    int result = 0;

    ExprSubscript subscriptexpr = TO_EXPR_SUBSCRIPT(*e.lhs);

    /* Compile the subscriptee. */
    COMPILE_EXPR(code, *subscriptexpr.expr);

    /* Compile the index. */
    COMPILE_EXPR(code, *subscriptexpr.index);

    if (is_compound)
    {
        COMPILE_EXPR(code, *e.lhs);
        COMPILE_EXPR(code, *e.rhs);
        handle_specop(code, e.op);
    }
    else
    {
        COMPILE_EXPR(code, *e.rhs);
    }

    emit_byte(code, OP_ARRAYSET);

    return result;
}

static int compile_expr_ass(Bytecode *code, Expr exp, CompileResult *compile_result)
{
    int result = 0;

    ExprAssign e = TO_EXPR_ASS(exp);
    bool compound_assign = strcmp(e.op, "=") != 0;

    switch (e.lhs->kind)
    {
        case EXPR_VAR:
            compile_assign_var(code, e, compound_assign, compile_result);
            break;
        case EXPR_GET:
            compile_assign_get(code, e, compound_assign, compile_result);
            break;
        case EXPR_UNA:
            compile_assign_una(code, e, compound_assign, compile_result);
            break;
        case EXPR_SUBSCRIPT:
            compile_assign_sub(code, e, compound_assign, compile_result);
            break;
        default:
            COMPILER_ERROR("Invalid assignment.");
    }

    return result;
}

static int compile_expr_log(Bytecode *code, Expr exp, CompileResult *compile_result)
{
    int result = 0;

    ExprLogic e = TO_EXPR_LOG(exp);

    /* We first compile the left-hand side of the expression. */
    COMPILE_EXPR(code, *e.lhs);

    if (strcmp(e.op, "&&") == 0)
    {
        /* For logical AND, we need to short-circuit when the left-hand side
         * is falsey.
         *
         * When the left-hand side is falsey, OP_JZ will eat the boolean va-
         * lue (false) that was on the stack after evaluating the left side,
         * meaning we need to make sure to put that value back on the stack.
         * This does not happen if the left-hand side is truthy, because the
         * result of evaluating the rhs will remain on the stack. So, we ne-
         * ed to only care about pushing false.
         *
         * We emit two jumps:
         *
         * 1) a conditional jump that jumps over both a) the right-hand side
         * and b) the other jump (described below), and goes straight to pu-
         * shing 'false'
         * 2) an unconditional jump that skips over pushing 'false'
         *
         * If the left-hand side is falsey, the vm will take the conditional
         * jump and push 'false' on the stack.
         *
         * If the left-hand side is truthy, the vm will evaluate the rhs and
         * skip over pushing 'false' on the stack. */
        int end_jump = emit_placeholder(code, OP_JZ);
        COMPILE_EXPR(code, *e.rhs);
        int false_jump = emit_placeholder(code, OP_JMP);
        patch_placeholder(code, end_jump);
        emit_bytes(code, 2, OP_TRUE, OP_NOT);
        patch_placeholder(code, false_jump);
    }
    else if (strcmp(e.op, "||") == 0)
    {
        /* For logical OR, we need to short-circuit when the left-hand side
         * is truthy.
         *
         * When lhs is truthy, OP_JZ will eat the bool value (true) that was
         * on the stack after evaluating the lhs, which means we need to put
         * the value back on the stack. This doesn't happen if lhs is falsey
         * because the result of evaluating the right-hand side would remain
         * on the stack. So, we need to only care about pushing 'true'.
         *
         * We emit two jumps:
         *
         * 1) a conditional jump that jumps over both a) pushing true and b)
         * the other jump (described below), and goes straight to evaluating
         * the right-hand side
         * 2) an unconditional jump that jumps over evaluating the rhs
         *
         * If the left-hand side is truthy, the vm will first push 'true' on
         * the stack and then fall through to the second, unconditional jump
         * that skips evaluating the right-hand side.
         *
         * If the lhs is falsey, the vm will jump over pushing 'true' on the
         * stack and the unconditional jump, and will evaluate the rhs. */
        int true_jump = emit_placeholder(code, OP_JZ);
        emit_byte(code, OP_TRUE);
        int end_jump = emit_placeholder(code, OP_JMP);
        patch_placeholder(code, true_jump);
        COMPILE_EXPR(code, *e.rhs);
        patch_placeholder(code, end_jump);
    }

    return result;
}

static int compile_expr_struct(Bytecode *code, Expr exp, CompileResult *compile_result)
{
    int result = 0;

    ExprStruct e = TO_EXPR_STRUCT(exp);

    /* Look up the struct with that name in current_compiler->structs. */
    StructBlueprint *blueprint = resolve_blueprint(e.name);

    /* If it is not found, bail out. */
    if (!blueprint)
    {
        COMPILER_ERROR("struct '%s' is not defined.\n", e.name);
    }

    /* If the number of properties in the struct blueprint does
     * not match the number of provided initializers, bail out. */
    if (blueprint->property_indexes->count != e.initializers.count)
    {
        COMPILER_ERROR("struct '%s' requires %ld initializers.\n", blueprint->name,
                       blueprint->property_indexes->count);
    }

    /* Check if the initializer names match the property names. */
    for (size_t i = 0; i < e.initializers.count; i++)
    {
        ExprStructInit siexp = e.initializers.data[i].as.expr_s_init;
        char *propname = TO_EXPR_VAR(*siexp.property).name;
        int *propidx = table_get(blueprint->property_indexes, propname);
        if (!propidx)
        {
            COMPILER_ERROR("struct '%s' has no property '%s'", blueprint->name, propname);
        }
    }

    /* Everything is OK, we emit OP_STRUCT followed by
     * struct's name index in the string pool. */
    emit_byte(code, OP_STRUCT);
    emit_uint32(code, add_string(code, blueprint->name));

    /* Finally, we compile the initializers. */
    for (size_t i = 0; i < e.initializers.count; i++)
    {
        COMPILE_EXPR(code, e.initializers.data[i]);
    }

    return result;
}

static int compile_expr_s_init(Bytecode *code, Expr exp, CompileResult *compile_result)
{
    int result = 0;

    ExprStructInit e = TO_EXPR_S_INIT(exp);

    /* First, we compile the value of the initializer,
     * since OP_SETATTR expects it to be on the stack. */
    COMPILE_EXPR(code, *e.value);

    ExprVar property = TO_EXPR_VAR(*e.property);

    /* Finally, we emit OP_SETATTR with the property's
     * name index. */
    emit_byte(code, OP_SETATTR);
    emit_uint32(code, add_string(code, property.name));

    return result;
}

static int compile_expr_array(Bytecode *code, Expr exp, CompileResult *compile_result)
{
    int result = 0;

    ExprArray e = TO_EXPR_ARRAY(exp);

    /* First, we compile the array elements in reverse. Why? If we had
     * done [1, 2, 3], when the vm popped these elements, it would ha-
     * ve got them in reversed order, that is, [3, 2, 1]. Compiling in
     * reverse avoids the overhead of sorting the elements at runtime. */
    for (int i = e.elements.count - 1; i >= 0; i--)
    {
        COMPILE_EXPR(code, e.elements.data[i]);
    }

    /* Then, we emit OP_ARRAY and the number of elements. */
    emit_byte(code, OP_ARRAY);
    emit_uint32(code, e.elements.count);

    return result;
}

static int compile_expr_subscript(Bytecode *code, Expr exp, CompileResult *compile_result)
{
    int result = 0;
    ExprSubscript e = TO_EXPR_SUBSCRIPT(exp);

    /* First, we compile the expr. */
    COMPILE_EXPR(code, *e.expr);

    /* Then, we compile the index. */
    COMPILE_EXPR(code, *e.index);

    /* Then, we emit OP_SUBSCRIPT. */
    emit_byte(code, OP_SUBSCRIPT);

    return result;
}

typedef int (*CompileExprHandlerFn)(Bytecode *code, Expr exp, CompileResult *compile_result);

typedef struct
{
    CompileExprHandlerFn fn;
    char *name;
} CompileExprHandler;

static CompileExprHandler expression_handler[] = {
    [EXPR_LIT] = {.fn = compile_expr_lit, .name = "EXPR_LIT"},
    [EXPR_VAR] = {.fn = compile_expr_var, .name = "EXPR_VAR"},
    [EXPR_UNA] = {.fn = compile_expr_una, .name = "EXPR_UNA"},
    [EXPR_BIN] = {.fn = compile_expr_bin, .name = "EXPR_BIN"},
    [EXPR_CALL] = {.fn = compile_expr_call, .name = "EXPR_CALL"},
    [EXPR_GET] = {.fn = compile_expr_get, .name = "EXPR_GET"},
    [EXPR_ASS] = {.fn = compile_expr_ass, .name = "EXPR_ASS"},
    [EXPR_LOG] = {.fn = compile_expr_log, .name = "EXPR_LOG"},
    [EXPR_STRUCT] = {.fn = compile_expr_struct, .name = "EXPR_STRUCT"},
    [EXPR_S_INIT] = {.fn = compile_expr_s_init, .name = "EXPR_S_INIT"},
    [EXPR_ARRAY] = {.fn = compile_expr_array, .name = "EXPR_ARRAY"},
    [EXPR_SUBSCRIPT] = {.fn = compile_expr_subscript, .name = "EXPR_SUBSCRIPT"},
};

static int compile_expr(Bytecode *code, Expr exp, CompileResult *compile_result)
{
    return expression_handler[exp.kind].fn(code, exp, compile_result);
}

static int compile_stmt(Bytecode *code, Stmt stmt, CompileResult *compile_result);

static int compile_stmt_print(Bytecode *code, Stmt stmt, CompileResult *compile_result)
{
    int result = 0;

    StmtPrint s = TO_STMT_PRINT(stmt);
    COMPILE_EXPR(code, s.exp);
    emit_byte(code, OP_PRINT);

    return result;
}

static int compile_stmt_let(Bytecode *code, Stmt stmt, CompileResult *compile_result)
{
    int result = 0;

    if (current_compiler->locals_count >= 256)
    {
        COMPILER_ERROR("Maximum 256 locals.");
    }

    StmtLet s = TO_STMT_LET(stmt);

    /* Compile the initializer. */
    COMPILE_EXPR(code, s.initializer);

    /* Add the variable name to the string pool. */
    uint32_t name_idx = add_string(code, s.name);

    /* If we're in global scope, emit OP_SET_GLOBAL,
     * otherwise, we want the value to remain on the
     * stack, so we will just make the compiler know
     * it is a local variable, and do some bookkeep-
     * ing regarding the number of variables we need
     * to pop off the stack when we do stack cleanup. */

    if (current_compiler->depth == 0)
    {
        current_compiler->globals[current_compiler->globals_count++] = (Local) {
            .name = code->sp.data[name_idx],
            .captured = false,
            .depth = current_compiler->depth,
        };
    }
    else
    {
        current_compiler->locals[current_compiler->locals_count++] = (Local) {
            .name = code->sp.data[name_idx],
            .captured = false,
            .depth = current_compiler->depth,
        };
    }

    if (current_compiler->depth == 0)
    {
        emit_byte(code, OP_SET_GLOBAL);
        emit_uint32(code, name_idx);
    }

    return result;
}

static int compile_stmt_expr(Bytecode *code, Stmt stmt, CompileResult *compile_result)
{
    int result = 0;

    StmtExpr e = TO_STMT_EXPR(stmt);

    COMPILE_EXPR(code, e.exp);

    /* If the expression statement was just a call, like:
     *
     * ...
     * main(4);
     * ...
     *
     * Pop the return value off the stack, so it does not
     * interfere with later execution. */
    if (e.exp.kind == EXPR_CALL)
    {
        emit_byte(code, OP_POP);
    }

    return result;
}

static int compile_stmt_block(Bytecode *code, Stmt stmt, CompileResult *compile_result)
{
    int result = 0;

    begin_scope();
    StmtBlock s = TO_STMT_BLOCK(&stmt);

    /* Compile the body of the black. */
    for (size_t i = 0; i < s.stmts.count; i++)
    {
        COMPILE_STMT(code, s.stmts.data[i]);
    }

    end_scope(code);

    return result;
}

static int compile_stmt_if(Bytecode *code, Stmt stmt, CompileResult *compile_result)
{
    int result = 0;

    StmtIf s = TO_STMT_IF(stmt);

    /* We first compile the conditional expression because the VM
    .* expects a bool placed on the stack by the time it encount-
     * ers a conditional jump, that is, OP_JZ. */
    COMPILE_EXPR(code, s.condition);

    /* Then, we emit OP_JZ, which jumps to the else clause if the
     * condition is falsey. Because we don't know the size of the
     * bytecode in the 'then' branch ahead of time, we do backpa-
     * tching: first, we emit 0xFFFF as the relative jump offset,
     * which serves as a stand-in for the real jump offset, which
     * will be known only after we compile the 'then' branch, and
     * find out its size. */
    int then_jump = emit_placeholder(code, OP_JZ);

    COMPILE_STMT(code, *s.then_branch);

    /* Then, we emit OP_JMP, which jumps over the else branch, in
     * case the then branch was taken. */
    int else_jump = emit_placeholder(code, OP_JMP);

    /* Then, we patch the then jump because now we know its size. */
    patch_placeholder(code, then_jump);

    /* Then, we compile the else branch if it exists. */
    if (s.else_branch != NULL)
    {
        COMPILE_STMT(code, *s.else_branch);
    }

    /* Finally, we patch the else jump. If the else branch wasn't
     * compiled, the offset should be zeroed out. */
    patch_placeholder(code, else_jump);

    return result;
}

static int compile_stmt_while(Bytecode *code, Stmt stmt, CompileResult *compile_result)
{
    int result = 0;

    StmtWhile s = TO_STMT_WHILE(stmt);

    /* We need to mark the beginning of the loop before we compile
     * the conditional expression, so that we know where to return
     * after the body of the loop is executed. */

    int loop_start = code->code.count;

    Label label = {.location = loop_start, .patch_with = -1};
    table_insert(current_compiler->labels, s.label, label);

    /* We then compile the condition because the VM expects a bool
     * placed on the stack by the time it encounters a conditional
     * jump, that is, OP_JZ. */
    COMPILE_EXPR(code, s.condition);

    /* We then emit OP_JZ which breaks out of the loop if the con-
     * dition is falsey. Because we don't know the size of the by-
     * tecode in the body of the 'while' loop ahead of time, we do
     * backpatching: first, we emit 0xFFFF as a relative jump off-
     * set which serves as a placeholder for the real jump offset. */
    int exit_jump = emit_placeholder(code, OP_JZ);

    /* Mark the loop depth (needed for break and continue). */
    dynarray_insert(&current_compiler->loop_depths, current_compiler->depth);

    /* Then, we compile the body of the loop. */
    COMPILE_STMT(code, *s.body);

    /* Pop the loop depth as it's no longer needed. */
    dynarray_pop(&current_compiler->loop_depths);

    /* Then, we emit OP_JMP with a negative offset which jumps just
     * before the condition, so that we could evaluate it again and
     * see if we need to continue looping. */
    emit_loop(code, loop_start);

    char *exit_label = malloc(256);
    snprintf(exit_label, 256, "%s_exit", s.label);

    Label *loop_exit = table_get(current_compiler->labels, exit_label);
    if (!loop_exit)
    {
        Label le = {.location = code->code.count, .patch_with = -1};
        table_insert(current_compiler->labels, exit_label, le);
    }
    else
    {
        loop_exit->patch_with = code->code.count;
        table_insert(current_compiler->labels, exit_label, *loop_exit);
    }

    /* Finally, we patch the exit jump. */
    patch_placeholder(code, exit_jump);
    patch_jumps(code);

    free(exit_label);

    return result;
}

static int compile_stmt_for(Bytecode *code, Stmt stmt, CompileResult *compile_result)
{
    int result = 0;

    StmtFor s = TO_STMT_FOR(stmt);

    ExprAssign assignment = TO_EXPR_ASS(s.initializer);
    ExprVar variable = TO_EXPR_VAR(*assignment.lhs);

    /* Insert the initializer variable name into the current_compiler->locals
     * dynarray, since the condition that follows the initializer ex-
     * pects it to be there. */
    current_compiler->locals[current_compiler->locals_count++] = (Local) {
        .name = variable.name,
        .captured = false,
        .depth = current_compiler->depth,
    };

    /* Compile the right-hand side of the initializer first. */
    COMPILE_EXPR(code, *assignment.rhs);

    /* Mark the beginning of the loop before compiling the condition,
     * so that we know where to jump after the loop body is executed. */
    int loop_start = code->code.count;

    Label label = {.location = loop_start, .patch_with = -1};
    table_insert(current_compiler->labels, s.label, label);

    /* Compile the conditional expression. */
    COMPILE_EXPR(code, s.condition);

    /* Emit OP_JZ in case the condition is falsey so that we can break
     * out of the loop. Because we don't know the size of the bytecode
     * in the body of the loop ahead of time, we do backpatching: fir-
     * st, we emit 0xFFFF as a relative jump offset acting as a place-
     * holder for the real jump offset. */
    int exit_jump = emit_placeholder(code, OP_JZ);

    /* In case the condition is truthy, we want to jump over the adva-
     * ncement expression. */
    int jump_over_advancement = emit_placeholder(code, OP_JMP);

    /* Mark the place we should jump to after continuing looping, whi-
     * ch is just before the advancement expression. */
    int loop_continuation = code->code.count;

    /* Compile the advancement expression. */
    COMPILE_EXPR(code, s.advancement);

    /* After the loop body is executed, we jump to the advancement and
     * execute it. This means we need to evaluate the condition again,
     * so we're emitting a backward unconditional jump, which jumps to
     * just before the condition. */
    emit_loop(code, loop_start);

    /* Patch the jump now that we know the size of the advancement. */
    patch_placeholder(code, jump_over_advancement);

    /* Patch the loop_start we inserted to point to loop_continuation.
     * This is the place just before the advancement. */
    Label *ls = table_get(current_compiler->labels, s.label);
    ls->location = loop_continuation;
    table_insert(current_compiler->labels, s.label, *ls);

    /* Mark the loop depth (needed for break and continue). */
    dynarray_insert(&current_compiler->loop_depths, current_compiler->depth);

    /* Compile the loop body. */
    COMPILE_STMT(code, *s.body);

    /* Pop the loop depth as it's no longer needed. */
    dynarray_pop(&current_compiler->loop_depths);

    /* Emit backward jump back to the advancement. */
    emit_loop(code, loop_continuation);

    current_compiler->locals_count--;

    char *exit_label = malloc(256);
    snprintf(exit_label, 256, "%s_exit", s.label);

    Label *loop_exit = table_get(current_compiler->labels, exit_label);
    if (!loop_exit)
    {
        Label l = {.location = code->code.count, .patch_with = -1};
        table_insert(current_compiler->labels, exit_label, l);
    }
    else
    {
        loop_exit->patch_with = code->code.count;
        table_insert(current_compiler->labels, exit_label, *loop_exit);
    }

    /* Finally, we patch the exit jump. */
    patch_placeholder(code, exit_jump);
    patch_jumps(code);

    /* Pop the initializer from the stack. */
    emit_byte(code, OP_POP);

    free(exit_label);

    return result;
}

Compiler *new_compiler(void)
{
    Compiler compiler = {0};
    memset(&compiler, 0, sizeof(Compiler));
    compiler.functions = calloc(1, sizeof(Table_Function));
    compiler.struct_blueprints = calloc(1, sizeof(Table_StructBlueprint));
    compiler.compiled_modules = calloc(1, sizeof(Table_module_ptr));
    compiler.builtins = calloc(1, sizeof(Table_FunctionPtr));
    compiler.labels = calloc(1, sizeof(Table_Label));

    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++)
    {
        Function builtin = {0};
        builtin.name = builtins[i].name;
        builtin.paramcount = builtins[i].argcount;

        table_insert(compiler.builtins, builtin.name, ALLOC(builtin));
    }

    return ALLOC(compiler);
}

static int compile_stmt_fn(Bytecode *code, Stmt stmt, CompileResult *compile_result)
{
    int result = 0;

    Compiler *old_compiler = current_compiler;
    current_compiler = new_compiler();
    current_compiler->next = old_compiler;
    current_compiler->depth = old_compiler->depth;

    StmtFn s = TO_STMT_FN(stmt);

    int funcname_idx = add_string(code, s.name);

    Function func = {
        .name = code->sp.data[funcname_idx],
        .paramcount = s.parameters.count,
        .location = code->code.count + 3,
    };

    table_insert(current_compiler->next->functions, func.name, func);

    current_compiler->current_fn = &func;

    Local *array = current_compiler->depth == 0 ? current_compiler->next->globals
                                                : current_compiler->next->locals;
    size_t *count = current_compiler->depth == 0 ? &current_compiler->next->globals_count
                                                 : &current_compiler->next->locals_count;
    array[(*count)++] = (Local) {
        .name = func.name,
        .depth = current_compiler->depth,
        .captured = false,
    };

    for (size_t i = 0; i < s.parameters.count; i++)
    {
        current_compiler->locals[current_compiler->locals_count++] = (Local) {
            .name = s.parameters.data[i],
            .depth = current_compiler->depth,
            .captured = false,
        };
    }

    /* Emit the jump because we don't want to execute the code
     * the first time we encounter it. */
    int jump = emit_placeholder(code, OP_JMP);

    /* Compile the function body. */
    COMPILE_STMT(code, *s.body);

    /* Finally, patch the jump. */
    patch_placeholder(code, jump);

    func.upvalue_count = current_compiler->upvalues.count;

    table_insert(current_compiler->functions, func.name, func);

    emit_byte(code, OP_CLOSURE);
    emit_uint32(code, add_string(code, func.name));
    emit_uint32(code, func.paramcount);
    emit_uint32(code, func.location);
    emit_uint32(code, func.upvalue_count);

    for (size_t i = 0; i < current_compiler->upvalues.count; i++)
    {
        emit_uint32(code, current_compiler->upvalues.data[i]);
    }

    if (current_compiler->depth == 0)
    {
        emit_byte(code, OP_SET_GLOBAL);
        emit_uint32(code, add_string(code, func.name));
    }

    free_compiler(current_compiler);
    free(current_compiler);

    current_compiler = old_compiler;

    return result;
}

static int compile_stmt_deco(Bytecode *code, Stmt stmt, CompileResult *compile_result)
{
    int result = 0;

    COMPILE_STMT(code, *stmt.as.stmt_deco.fn);

    emit_byte(code, OP_GET_GLOBAL);
    emit_uint32(code, add_string(code, stmt.as.stmt_deco.fn->as.stmt_fn.name));

    emit_byte(code, OP_GET_GLOBAL);
    emit_uint32(code, add_string(code, stmt.as.stmt_deco.name));

    emit_byte(code, OP_CALL);

    uint32_t argcount;

    Function *f = resolve_func(stmt.as.stmt_deco.name);

    if (f)
    {
        argcount = f->paramcount;
    }
    else
    {
        COMPILER_ERROR("...");
    }

    emit_byte(code, argcount);

    emit_byte(code, OP_SET_GLOBAL);
    emit_uint32(code, add_string(code, stmt.as.stmt_deco.fn->as.stmt_fn.name));

    return result;
}

static int compile_stmt_struct(Bytecode *code, Stmt stmt, CompileResult *compile_result)
{
    int result = 0;

    StmtStruct s = TO_STMT_STRUCT(stmt);

    /* Emit some bytecode in the following format:
     *
     * OP_STRUCT_BLUEPRINT
     * 4-byte index of the struct name in the sp
     * 4-byte struct property count
     * for each property:
     *    4-byte index of the property name in the sp
     *    4-byte index of the property in the 'items' */
    emit_byte(code, OP_STRUCT_BLUEPRINT);
    emit_uint32(code, add_string(code, s.name));
    emit_uint32(code, s.properties.count);

    StructBlueprint blueprint = {.name = s.name,
                                 .property_indexes = calloc(1, sizeof(Table_int)),
                                 .methods = calloc(1, sizeof(Table_Function))};

    for (size_t i = 0; i < s.properties.count; i++)
    {
        emit_uint32(code, add_string(code, s.properties.data[i]));
        table_insert(blueprint.property_indexes, s.properties.data[i], i);
        emit_uint32(code, i);
    }

    /* Let the compiler know about the blueprint. */
    table_insert(current_compiler->struct_blueprints, blueprint.name, blueprint);

    return result;
}

static int compile_stmt_return(Bytecode *code, Stmt stmt, CompileResult *compile_result)
{
    int result = 0;

    StmtRet s = TO_STMT_RETURN(stmt);

    /* Compile the return value. */
    COMPILE_EXPR(code, s.returnval);

    /* We need to perform the stack cleanup, but the return value
     * mustn't be lost, so we'll (ab)use OP_DEEPSET for this job.
     *
     * For example, if the stack is:
     *
     * [..., 1, 2, 3, <return value>]
     *
     * The cleanup will look like this:
     *
     * [..., 1, 2, 3, <return value>]
     * [..., 1, 2, <return value>]
     * [..., 1, <return value>]
     * [..., <return value>] */

    /* Finally, emit OP_RET. */

    size_t deepset_no = current_compiler->locals_count - 1;

    for (int i = current_compiler->locals_count - 1; i >= 0; i--)
    {
        if (current_compiler->locals[i].captured)
        {
            emit_byte(code, OP_CLOSE_UPVALUE);
        }
        else
        {
            emit_byte(code, OP_DEEPSET);
            emit_uint32(code, deepset_no--);
        }
    }

    emit_byte(code, OP_RET);

    return result;
}

static void emit_named_jump(Bytecode *code, char *label)
{
    int jmp = emit_placeholder(code, OP_JMP);
    Label exit_label = {.location = jmp, .patch_with = -1};
    table_insert(current_compiler->labels, label, exit_label);
}

static int compile_stmt_break(Bytecode *code, Stmt stmt, CompileResult *compile_result)
{
    int result = 0;
    
    size_t len = lblen(stmt.as.stmt_break.label, 0) + strlen("_exit"); 
    
    char *exit_label = malloc(len);
    snprintf(exit_label, len, "%s_exit", stmt.as.stmt_break.label);
    emit_loop_cleanup(code);
    emit_named_jump(code, exit_label);
    free(exit_label);

    return result;
}

static int compile_stmt_continue(Bytecode *code, Stmt stmt, CompileResult *compile_result)
{
    int result = 0;

    Label *loop_start = table_get(current_compiler->labels, stmt.as.stmt_continue.label);
    emit_loop_cleanup(code);
    emit_loop(code, loop_start->location);

    return result;
}

static int compile_stmt_impl(Bytecode *code, Stmt stmt, CompileResult *compile_result)
{
    int result = 0;
    StmtImpl s = TO_STMT_IMPL(stmt);

    /* Look up the struct with that name in compiler->structs. */
    StructBlueprint *blueprint = table_get(current_compiler->struct_blueprints, s.name);

    /* If it is not found, bail out. */
    if (!blueprint)
    {
        COMPILER_ERROR("struct '%s' is not defined.\n", s.name);
    }

    for (size_t i = 0; i < s.methods.count; i++)
    {
        StmtFn func = TO_STMT_FN(s.methods.data[i]);
        Function f = {
            .name = func.name,
            .paramcount = func.parameters.count,
            .location = code->code.count + 3,
        };
        table_insert(blueprint->methods, func.name, ALLOC(f));
        COMPILE_STMT(code, s.methods.data[i]);
    }

    emit_byte(code, OP_IMPL);
    emit_uint32(code, add_string(code, blueprint->name));
    emit_uint32(code, s.methods.count);

    for (size_t i = 0; i < s.methods.count; i++)
    {
        StmtFn func = TO_STMT_FN(s.methods.data[i]);
        Function **f = table_get(blueprint->methods, func.name);
        emit_uint32(code, add_string(code, (*f)->name));
        emit_uint32(code, (*f)->paramcount);
        emit_uint32(code, (*f)->location);
    }

    return result;
}

static bool is_cyclic(Compiler *compiler, Module *mod)
{
    Module *current = mod;
    while (current->parent)
    {
        if (strcmp(current->parent->path, mod->path) == 0)
            return true;
        current = current->parent;
    }
    return false;
}

static int compile_stmt_use(Bytecode *code, Stmt stmt, CompileResult *compile_result)
{
    int result = 0;
    StmtUse stmt_use = TO_STMT_USE(stmt);

    Module **cached_module = resolve_module(stmt_use.path);

    if (!cached_module)
    {
        char *source = read_file(stmt_use.path);

        Module *importee = calloc(1, sizeof(Module));

        Tokenizer tokenizer;
        init_tokenizer(&tokenizer, source);

        TokenizeResult tokenize_result = tokenize(&tokenizer);

        if (!tokenize_result.is_ok)
        {
            fprintf(stderr, "tokenizer: %s\n", tokenize_result.msg);
            exit(1);
        }

        DynArray_Token tokens = tokenize_result.tokens;

        Parser parser;
        init_parser(&parser, &tokens);

        ParseResult parse_result = parse(&parser);

        if (!parse_result.is_ok)
        {
            fprintf(stderr, "parser: %s\n", parse_result.msg);
            exit(1);
        }

        DynArray_Stmt raw_ast = parse_result.ast;
        DynArray_Stmt cooked_ast = loop_label_program(raw_ast, NULL);

        Module *old_module = current_compiler->current_mod;

        importee->path = own_string(stmt_use.path);
        importee->parent = old_module;

        current_compiler->current_mod = importee;

        dynarray_insert(&importee->parent->imports, importee);

        table_insert(current_compiler->compiled_modules, stmt_use.path, importee);

        if (is_cyclic(current_compiler, importee))
            COMPILER_ERROR("Cycle.");

        for (size_t i = 0; i < cooked_ast.count; i++)
        {
            COMPILE_STMT(code, cooked_ast.data[i]);
        }

        current_compiler->current_mod = old_module;

        for (size_t i = 0; i < cooked_ast.count; i++)
        {
            free_stmt(cooked_ast.data[i]);
        }
        dynarray_free(&cooked_ast);

        dynarray_free(&tokens);

        free(source);
    }
    else
    {
        dynarray_insert(&current_compiler->current_mod->imports, *cached_module);
        current_compiler->current_mod->imports
            .data[current_compiler->current_mod->imports.count - 1]
            ->parent = current_compiler->current_mod;

        if (is_cyclic(current_compiler, *cached_module))
            COMPILER_ERROR("Cycle.");

#ifdef venom_debug_compiler
        printf("using cached import for: %s\n", (*cached_module)->path);
    }

    Module **root_mod = resolve_module(current_compiler->root_mod);
    print_module_tree(current_compiler, *root_mod, *root_mod, 0, false);
#else
    }
#endif
    return result;
}

static int compile_stmt_yield(Bytecode *code, Stmt stmt, CompileResult *compile_result)
{
    int result = 0;

    StmtYield stmt_yield = TO_STMT_YIELD(stmt);
    COMPILE_EXPR(code, stmt_yield.exp);
    emit_byte(code, OP_YIELD);

    Function *f = resolve_func(current_compiler->current_fn->name);

    f->is_gen = true;

    table_insert(current_compiler->functions, f->name, *f);

    return result;
}

static int compile_stmt_assert(Bytecode *code, Stmt stmt, CompileResult *compile_result)
{
    int result = 0;

    StmtAssert stmt_assert = TO_STMT_ASSERT(stmt);
    COMPILE_EXPR(code, stmt_assert.exp);
    emit_byte(code, OP_ASSERT);

    return result;
}

typedef int (*CompileHandlerFn)(Bytecode *code, Stmt stmt, CompileResult *compile_result);

typedef struct
{
    CompileHandlerFn fn;
    char *name;
} CompileHandler;

static CompileHandler stmt_handler[] = {
    [STMT_PRINT] = {.fn = compile_stmt_print, .name = "STMT_PRINT"},
    [STMT_LET] = {.fn = compile_stmt_let, .name = "STMT_LET"},
    [STMT_EXPR] = {.fn = compile_stmt_expr, .name = "STMT_EXPR"},
    [STMT_BLOCK] = {.fn = compile_stmt_block, .name = "STMT_BLOCK"},
    [STMT_IF] = {.fn = compile_stmt_if, .name = "STMT_IF"},
    [STMT_WHILE] = {.fn = compile_stmt_while, .name = "STMT_WHILE"},
    [STMT_FOR] = {.fn = compile_stmt_for, .name = "STMT_FOR"},
    [STMT_FN] = {.fn = compile_stmt_fn, .name = "STMT_FN"},
    [STMT_IMPL] = {.fn = compile_stmt_impl, .name = "STMT_IMPL"},
    [STMT_DECO] = {.fn = compile_stmt_deco, .name = "STMT_DECO"},
    [STMT_STRUCT] = {.fn = compile_stmt_struct, .name = "STMT_STRUCT"},
    [STMT_RETURN] = {.fn = compile_stmt_return, .name = "STMT_RETURN"},
    [STMT_BREAK] = {.fn = compile_stmt_break, .name = "STMT_BREAK"},
    [STMT_CONTINUE] = {.fn = compile_stmt_continue, .name = "STMT_CONTINUE"},
    [STMT_USE] = {.fn = compile_stmt_use, .name = "STMT_USE"},
    [STMT_YIELD] = {.fn = compile_stmt_yield, .name = "STMT_YIELD"},
    [STMT_ASSERT] = {.fn = compile_stmt_assert, .name = "STMT_ASSERT"},
};

static int compile_stmt(Bytecode *code, Stmt stmt, CompileResult *compile_result)
{
    return stmt_handler[stmt.kind].fn(code, stmt, compile_result);
}

void free_compile_result(CompileResult *result)
{
    if (result->chunk)
    {
        free_chunk(result->chunk);
        free(result->chunk);
    }
    if (result->msg)
    {
        free(result->msg);
    }
}

CompileResult compile(DynArray_Stmt *ast)
{
    CompileResult result = {0};

    Bytecode *chunk = malloc(sizeof(Bytecode));
    init_chunk(chunk);

    int tmp_result;
    for (size_t i = 0; i < ast->count; i++)
    {
        tmp_result = compile_stmt(chunk, ast->data[i], &result);
        if (tmp_result == -1)
        {
            free_chunk(chunk);
            free(chunk);
            return result;
        }
    }

    dynarray_insert(&chunk->code, OP_HLT);

    result.is_ok = true;
    result.chunk = chunk;

    return result;
}
