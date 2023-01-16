#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "compiler.h"
#include "vm.h"
#include "util.h"

Compiler compiler;

#define COMPILER_ERROR(...) \
do { \
    fprintf(stderr, "Compiler error: "); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
    exit(1); \
} while (0)

#define COPY_DYNARRAY(dest, src) \
do { \
    for (size_t i = 0; i < (src)->count; i++) { \
        dynarray_insert((dest), (src)->data[i]); \
    } \
} while (0)

void init_compiler() {
    /* Zero-initialize the compiler. */
    memset(&compiler, 0, sizeof(Compiler));
}

void free_compiler() {
    dynarray_free(&compiler.globals);
    dynarray_free(&compiler.locals);
    dynarray_free(&compiler.breaks);
    dynarray_free(&compiler.continues);
    table_free(&compiler.structs);
    table_free(&compiler.functions);
}

void init_chunk(BytecodeChunk *chunk) {
    memset(chunk, 0, sizeof(BytecodeChunk));
}

void free_chunk(BytecodeChunk *chunk) {
    dynarray_free(&chunk->code);
    for (int i = 0; i < chunk->sp.count; i++) 
        free(chunk->sp.data[i]);
    dynarray_free(&chunk->sp);
    dynarray_free(&chunk->cp);
}

static uint32_t add_string(BytecodeChunk *chunk, const char *string) {
    /* Check if the string is already present in the pool. */
    for (size_t i = 0; i < chunk->sp.count; i++) {
        /* If it is, return the index. */
        if (strcmp(chunk->sp.data[i], string) == 0) {
            return i;
        }
    }
    /* Otherwise, own the string, insert it into the pool
     * and return the index. */
    char *s = own_string(string);
    dynarray_insert(&chunk->sp, s);
    return chunk->sp.count - 1;
}

static uint32_t add_constant(BytecodeChunk *chunk, double constant) {
    /* Check if the constant is already present in the pool. */
    for (size_t i = 0; i < chunk->cp.count; i++) {
        /* If it is, return the index. */
        if (chunk->cp.data[i] == constant) {
            return i;
        }
    }
    /* Otherwise, insert the constant into the pool
     * and return the index. */
    dynarray_insert(&chunk->cp, constant);
    return chunk->cp.count - 1;
}

static void emit_byte(BytecodeChunk *chunk, uint8_t byte) {
    dynarray_insert(&chunk->code, byte);
}

static void emit_bytes(BytecodeChunk *chunk, uint8_t n, ...) {
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        uint8_t byte = va_arg(ap, int);
        emit_byte(chunk, byte);
    }
    va_end(ap);
}

static void emit_uint32(BytecodeChunk *chunk, uint32_t index) {
    emit_bytes(
        chunk, 4,
        (index >> 24) & 0xFF,
        (index >> 16) & 0xFF,
        (index >> 8) & 0xFF,
        index & 0xFF
    );
}

static int emit_placeholder(BytecodeChunk *chunk, Opcode op) {
    emit_bytes(chunk, 3, op, 0xFF, 0xFF);
    /* The opcode, followed by its 2-byte offset is the last
     * emitted instruction. 
     *
     * e.g. if `chunk->code.data` is:
     * 
     * [OP_CONST, operand,
     *  OP_CONST, operand,
     *  OP_EQ, 
     *  OP_JZ, operand, operand]
     *                            ^-- `chunk->code.count`
     *
     * `chunk->code.count` will be 8. Because the indexing is
     * zero-based, the count points to just beyond the 2-byte
     * offset. In order to get the position of the opcode, we
     * need to go back 3 slots (two operands plus one 'extra'
     * slot to adjust for zero-based indexing). */
    return chunk->code.count - 3;
}

static void patch_placeholder(BytecodeChunk *chunk, int op) {
    /* We are given the 0-based index of the opcode, and we want
     * to patch the offset that follows with the number of emitted
     * instructions after the opcode and the placeholder.
     *
     * For example, if we have:
     *
     * [OP_CONST, operand,
     *  OP_CONST, operand,
     *  OP_EQ, 
     *  OP_JZ, operand, operand,
     *  OP_STR, operand, 
     *  OP_PRINT]
     *             ^-- `chunk->code.count`
     *
     * `op` will be 5. To get the number of emitted instructions,
     * we first adjust for zero-based indexing by subtracting 1
     * (such that count points to the last element. Then we take
     * the index of the opcode (OP_JZ, i.e., 5 in this case) and add
     * 2 because we need to adjust for the operands. The result of
     * the subtraction of these two is the number of emitted bytes
     * we need, and we use it to build a signed 16-bit offset to
     * patch the placeholder. */
    int16_t bytes_emitted = (chunk->code.count - 1) - (op + 2);
    chunk->code.data[op+1] = (bytes_emitted >> 8) & 0xFF;
    chunk->code.data[op+2] = bytes_emitted & 0xFF;
}

static void emit_loop(BytecodeChunk *chunk, int loop_start) {
    /* 
     * For example, consider the following bytecode for the program
     * on the side:
     *
     *     0: OP_CONST (value: 0.000000)    |
     *     2: OP_SET_GLOBAL (index: 0)      |
     *     4: OP_GET_GLOBAL (index: 0)      |
     *     6: OP_CONST (value: 5.000000)    |   let x = 0;
     *     8: OP_LT                         |   while (x < 5) {
     *     9: OP_JZ (offset: 13)            |       print x;
     *     12: OP_GET_GLOBAL (index: 0)     |       x = x+1;
     *     14: OP_PRINT                     |   }
     *     15: OP_GET_GLOBAL (index: 0)     |
     *     17: OP_CONST (value: 1.000000)   |
     *     19: OP_ADD                       |
     *     20: OP_SET_GLOBAL (index: 0)     |
     *     22: OP_JMP (offset: -21)         |
     *
     * In this case, the loop starts at `4`, OP_GET_GLOBAL.
     *
     * We first emit OP_JMP, so the count will be 23 and will
     * point to one instruction beyond the end of the bytecode.
     * In order to go back to the beginning of the loop, we need
     * to go backwards 19 instructions:
     *
     *     `chunk->code.count` - `loop_start` = 23 - 4 = 19
     *
     * However, we need to make sure we include the 2-byte offset
     * for OP_JMP (23 - 4 + 2 = 21), because by the time the vm
     * encounters this jump, it will have read the 2-byte offset
     * as well - which means it will need to jump from `24` to `4`.
     * In order to get there, we need to be one step behind so that
     * the main loop will increment the instruction pointer to the
     * next instruction.
     *
     * So, 24 - 21 = 3, and we wait for the main loop to increment ip.
     */
    emit_byte(chunk, OP_JMP);
    int16_t offset = -(chunk->code.count - loop_start + 2);
    emit_byte(chunk, (offset >> 8) & 0xFF);
    emit_byte(chunk, offset & 0xFF);
}

static void emit_stack_cleanup(BytecodeChunk *chunk) {
    int pop_count = compiler.pops[compiler.depth];
    for (int i = 0; i < pop_count; i++) {
        emit_byte(chunk, OP_POP);
    }
}

static bool resolve_global(char *name) {
    for (int i = compiler.globals.count - 1; i >= 0; i--) {
        if (strcmp(compiler.globals.data[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static int resolve_local(char *name) {
    for (int i = compiler.locals.count - 1; i >= 0; i--) {
        if (strcmp(compiler.locals.data[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

static Object *resolve_func(char *name) {
    return table_get(&compiler.functions, name);
}

static Object *resolve_struct(char *name) {
    return table_get(&compiler.structs, name);
}

static void compile_expression(BytecodeChunk *chunk, Expression exp);

static void handle_compile_expression_literal(BytecodeChunk *chunk, Expression exp) {
    LiteralExpression e = TO_EXPR_LITERAL(exp);
    if (e.specval == NULL) {
        uint32_t const_index = add_constant(chunk, e.dval);
        emit_byte(chunk, OP_CONST);
        emit_uint32(chunk, const_index);
    } else {
        if (strcmp(e.specval, "true") == 0) {
            emit_byte(chunk, OP_TRUE);
        } else if (strcmp(e.specval, "false") == 0) {
            emit_bytes(chunk, 2, OP_TRUE, OP_NOT);
        } else if (strcmp(e.specval, "null") == 0) {
            emit_byte(chunk, OP_NULL);
        }
    }
}

static void handle_compile_expression_string(BytecodeChunk *chunk, Expression exp) {
    StringExpression e = TO_EXPR_STRING(exp);
    uint32_t const_index = add_string(chunk, e.str);
    emit_byte(chunk, OP_STR);
    emit_uint32(chunk, const_index);
}

static void handle_compile_expression_variable(BytecodeChunk *chunk, Expression exp) {
    VariableExpression e = TO_EXPR_VARIABLE(exp);
    int index = resolve_local(e.name);
    if (index != -1) {
        emit_byte(chunk, OP_DEEPGET);
        emit_uint32(chunk, index+1);
    } else {
        bool is_defined = resolve_global(e.name);
        if (is_defined) {
            uint32_t name_index = add_string(chunk, e.name);
            emit_byte(chunk, OP_GET_GLOBAL);
            emit_uint32(chunk, name_index);
        } else {
            COMPILER_ERROR("Variable '%s' is not defined.", e.name);
        }
    }
}

static void handle_compile_expression_unary(BytecodeChunk *chunk, Expression exp) {
    UnaryExpression e = TO_EXPR_UNARY(exp);
    compile_expression(chunk, *e.exp);
    emit_byte(chunk, OP_NEG);
}

static void handle_compile_expression_binary(BytecodeChunk *chunk, Expression exp) {
    BinaryExpression e = TO_EXPR_BINARY(exp);

    compile_expression(chunk, *e.lhs);
    compile_expression(chunk, *e.rhs);

    if (strcmp(e.operator, "+") == 0) {
        emit_byte(chunk, OP_ADD);
    } else if (strcmp(e.operator, "-") == 0) {
        emit_byte(chunk, OP_SUB);                
    } else if (strcmp(e.operator, "*") == 0) {
        emit_byte(chunk, OP_MUL);
    } else if (strcmp(e.operator, "/") == 0) {
        emit_byte(chunk, OP_DIV);
    } else if (strcmp(e.operator, "%%") == 0) {
        emit_byte(chunk, OP_MOD);
    } else if (strcmp(e.operator, ">") == 0) {
        emit_byte(chunk, OP_GT);
    } else if (strcmp(e.operator, "<") == 0) {
        emit_byte(chunk, OP_LT);
    } else if (strcmp(e.operator, ">=") == 0) {
        emit_bytes(chunk, 2, OP_LT, OP_NOT);
    } else if (strcmp(e.operator, "<=") == 0) {
        emit_bytes(chunk, 2, OP_GT, OP_NOT);
    } else if (strcmp(e.operator, "==") == 0) {
        emit_byte(chunk, OP_EQ);
    } else if (strcmp(e.operator, "!=") == 0) {
        emit_bytes(chunk, 2, OP_EQ, OP_NOT);
    }
}

static void handle_compile_expression_call(BytecodeChunk *chunk, Expression exp) {
    CallExpression e = TO_EXPR_CALL(exp);
    int ip = emit_placeholder(chunk, OP_IP);
    for (size_t i = 0; i < e.arguments.count; i++) {
        compile_expression(chunk, e.arguments.data[i]);
    }
    emit_byte(chunk, OP_INC_FPCOUNT);
    Object *func = resolve_func(e.var.name);
    int16_t jump = -(chunk->code.count - TO_FUNC(*func)->location) - 3;
    emit_bytes(chunk, 3, OP_JMP, (jump >> 8) & 0xFF, jump & 0xFF);
    patch_placeholder(chunk, ip);
}

static void handle_compile_expression_get(BytecodeChunk *chunk, Expression exp) {
    GetExpression e = TO_EXPR_GET(exp);
    compile_expression(chunk, *e.exp);
    uint32_t index = add_string(chunk, e.property_name);
    emit_byte(chunk, OP_GETATTR);
    emit_uint32(chunk, index);
}

static void handle_compile_expression_assign(BytecodeChunk *chunk, Expression exp) {
    AssignExpression e = TO_EXPR_ASSIGN(exp);
    compile_expression(chunk, *e.rhs);
    if (e.lhs->kind == EXP_VARIABLE) {
        VariableExpression var = TO_EXPR_VARIABLE(*e.lhs);
        int index = resolve_local(var.name);
        if (index != -1) {
            emit_byte(chunk, OP_DEEPSET);
            emit_uint32(chunk, index+1);
        } else {
            bool is_defined = resolve_global(var.name);
            if (is_defined) {
                uint32_t name_index = add_string(chunk, var.name);
                emit_byte(chunk, OP_SET_GLOBAL);
                emit_uint32(chunk, name_index);
            } else {
                COMPILER_ERROR("Variable '%s' is not defined.", var.name);
            }
        }
    } else if (e.lhs->kind == EXPR_GET) {
        compile_expression(chunk, *TO_EXPR_GET(*e.lhs).exp);
        uint32_t index = add_string(chunk, TO_EXPR_GET(*e.lhs).property_name);
        emit_byte(chunk, OP_SETATTR);
        emit_uint32(chunk, index);
    } else {
        COMPILER_ERROR("invalid assignment.\n");
    }
}

static void handle_compile_expression_logical(BytecodeChunk *chunk, Expression exp) {
    LogicalExpression e = TO_EXPR_LOGICAL(exp);
    /* We first compile the left-hand side of the expression. */
    compile_expression(chunk, *e.lhs);
    if (strcmp(e.operator, "&&") == 0) {
        /* For logical AND, we need to short-circuit when the left-hand side is falsey.
         *
         * When the left-hand side is falsey, OP_JZ will eat the boolean value ('false')
         * that was on stack after evaluating the left side, which means we need to
         * make sure to put that value back on the stack. This does not happen if the
         * left-hand side is truthy, because the result of evaluating the right-hand
         * side will remain on the stack. Hence why we need to only care about pushing
         * 'false'.
         *
         * We emit two jumps:
         *
         * 1) a conditial jump that jumps over both the right-hand side and the other
         *    jump (described below) - and goes straight to pushing 'false'
         * 2) an unconditional jump that skips over pushing 'false
         *
         * If the left-hand side is falsey, the vm will take the conditional jump and
         * push 'false' on the stack.
         *
         * If the left-hand side is truthy, the vm will evaluate the right-hand side
         * and skip over pushing 'false' on the stack.
         */
        int end_jump = emit_placeholder(chunk, OP_JZ);
        compile_expression(chunk, *e.rhs);
        int false_jump = emit_placeholder(chunk, OP_JMP);
        patch_placeholder(chunk, end_jump);
        emit_bytes(chunk, 2, OP_TRUE, OP_NOT);
        patch_placeholder(chunk, false_jump);
    } else if (strcmp(e.operator, "||") == 0) {
        /* For logical OR, we need to short-circuit when the left-hand side is truthy.
         *
         * When left-hand side is truthy, OP_JZ will eat the boolean value ('true')
         * that was on the stack after evaluating the left-hand side, which means
         * we need to make sure to put that value back on the stack. This does not
         * happen if the left-hand sie is falsey, because the result of evaluating
         * the right-hand side will remain on the stack. Hence why we need to only
         * care about pushing 'true'.
         *
         * We emit two jumps:
         *
         * 1) a conditional jump that jumps over pushing 'true' and the other jump
         *    (described below) - and goes straight to evaluating the right-hand side
         * 2) an unconditional jump that jumps over evaluating the right-hand side
         *
         * If the left-hand side is truthy, the vm will first push 'true' back on
         * the stack and then fall through to the second, unconditional jump that
         * skips evaluating the right-hand side.
         *
         * If the left-hand side is falsey, it jumps over pushing 'true' back on
         * the stack and the unconditional jump, and evaluates the right-hand side. */
        int true_jump = emit_placeholder(chunk, OP_JZ);
        emit_byte(chunk, OP_TRUE);
        int end_jump = emit_placeholder(chunk, OP_JMP);
        patch_placeholder(chunk, true_jump);
        compile_expression(chunk, *e.rhs);
        patch_placeholder(chunk, end_jump);
    }
}

static void handle_compile_expression_struct(BytecodeChunk *chunk, Expression exp) {
    StructExpression e = TO_EXPR_STRUCT(exp);
    Object *blueprintobj = resolve_struct(e.name);
    if (blueprintobj == NULL) {
        COMPILER_ERROR("struct '%s' is not defined.\n", e.name);
    }
    StructBlueprint *sb = TO_STRUCT_BLUEPRINT(*blueprintobj);
    if (sb->properties.count != e.initializers.count) {
        COMPILER_ERROR(
            "struct '%s' requires %ld initializers.\n",
            sb->name,
            sb->properties.count
        );
    }
    for (size_t i = 0; i < sb->properties.count; i++) {
        char *property = sb->properties.data[i];
        bool found = false;
        for (size_t j = 0; j < e.initializers.count; j++) {
            StructInitializerExpression initializer = TO_EXPR_STRUCT_INIT(e.initializers.data[j]);
            VariableExpression key = TO_EXPR_VARIABLE(*initializer.property);
            if (strcmp(property, key.name) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            /* This call returns a malloc'd pointer, but since we
             * are immediately calling the COMPILER_ERROR macro,
             * we rely on the OS to free up the resources. */
            char *properties_str = strcat_dynarray(sb->properties);
            COMPILER_ERROR(
                "struct '%s' requires properties: [%s]", sb->name, properties_str
            );
        }
    }
    uint32_t name_index = add_string(chunk, sb->name);
    emit_byte(chunk, OP_STRUCT);
    emit_uint32(chunk, name_index);
    emit_uint32(chunk, e.initializers.count);
    for (size_t i = 0; i < e.initializers.count; i++) {
        compile_expression(chunk, e.initializers.data[i]);
    }
}

static void handle_compile_expression_struct_init(BytecodeChunk *chunk, Expression exp) {
    StructInitializerExpression e = TO_EXPR_STRUCT_INIT(exp);
    compile_expression(chunk, *e.value);
    VariableExpression key = TO_EXPR_VARIABLE(*e.property);
    uint32_t property_name_index = add_string(chunk, key.name);
    emit_byte(chunk, OP_SETATTR);
    emit_uint32(chunk, property_name_index);
}

typedef void (*CompileExpressionHandlerFn)(BytecodeChunk *chunk, Expression exp);

typedef struct {
    CompileExpressionHandlerFn fn;
    char *name;
} CompileExpressionHandler;

CompileExpressionHandler expression_handler[] = {
    [EXP_LITERAL] = { .fn = handle_compile_expression_literal, .name = "EXP_LITERAL" },
    [EXP_STRING] = { .fn = handle_compile_expression_string, .name = "EXP_STRING" },
    [EXP_VARIABLE] = { .fn = handle_compile_expression_variable, .name = "EXP_VARIABLE" },
    [EXP_UNARY] = { .fn = handle_compile_expression_unary, .name = "EXP_UNARY" },
    [EXP_BINARY] = { .fn = handle_compile_expression_binary, .name = "EXP_BINARY" },
    [EXP_CALL] = { .fn = handle_compile_expression_call, .name = "EXP_CALL" },
    [EXPR_GET] = { .fn = handle_compile_expression_get, .name = "EXP_GET" },
    [EXP_ASSIGN] = { .fn = handle_compile_expression_assign, .name = "EXP_ASSIGN" },
    [EXP_LOGICAL] = { .fn = handle_compile_expression_logical, .name = "EXP_LOGICAL" },
    [EXP_STRUCT] = { .fn = handle_compile_expression_struct, .name = "EXP_STRUCT" },
    [EXP_STRUCT_INIT] = { .fn = handle_compile_expression_struct_init, .name = "EXP_STRUCT_INIT" },
};

static void compile_expression(BytecodeChunk *chunk, Expression exp) {
    expression_handler[exp.kind].fn(chunk, exp);
}

void compile(BytecodeChunk *chunk, Statement stmt);

static void handle_compile_statement_print(BytecodeChunk *chunk, Statement stmt) {
    PrintStatement s = TO_STMT_PRINT(stmt);
    compile_expression(chunk, s.exp);
    emit_byte(chunk, OP_PRINT);
}

static void handle_compile_statement_let(BytecodeChunk *chunk, Statement stmt) {
    LetStatement s = TO_STMT_LET(stmt);
    compile_expression(chunk, s.initializer);
    uint32_t name_index = add_string(chunk, s.name);
    if (compiler.depth == 0) {
        dynarray_insert(&compiler.globals, s.name);
        emit_byte(chunk, OP_SET_GLOBAL);
        emit_uint32(chunk, name_index);
    } else {
        dynarray_insert(&compiler.locals, chunk->sp.data[name_index]);
        compiler.pops[compiler.depth]++;
    }
}

static void handle_compile_statement_expr(BytecodeChunk *chunk, Statement stmt) {
    ExpressionStatement e = TO_STMT_EXPR(stmt);
    compile_expression(chunk, e.exp);
    /* If the expression statement was just a call, like:
     * ...
     * main(4);
     * ...
     * Pop the return value off the stack so it does not
     * interfere with later execution. */
    if (e.exp.kind == EXP_CALL) {
        emit_byte(chunk, OP_POP);
    }
}

static void begin_scope() {
    compiler.depth++;
}

static void end_scope() {
    for (int i = 0; i < compiler.pops[compiler.depth]; i++) {
        dynarray_pop(&compiler.locals);
    }
    compiler.pops[compiler.depth] = 0;
    compiler.depth--;    
}

static void handle_compile_statement_block(BytecodeChunk *chunk, Statement stmt) {
    begin_scope();
    BlockStatement s = TO_STMT_BLOCK(&stmt);

    /* Compile the body of the black. */
    for (size_t i = 0; i < s.stmts.count; i++) {
        compile(chunk, s.stmts.data[i]);
    }

    emit_stack_cleanup(chunk);
    end_scope();
}

static void handle_compile_statement_if(BytecodeChunk *chunk, Statement stmt) {
    /* We first compile the conditional expression because the VM
    .* expects something like OP_EQ to have already been executed
     * and a boolean placed on the stack by the time it encounters
     * an instruction like OP_JZ. */
    IfStatement s = TO_STMT_IF(stmt);
    compile_expression(chunk, s.condition);

    /* Then, we emit an OP_JZ which jumps to the else clause if the
     * condition is falsey. Because we do not know the size of the
     * bytecode in the 'then' branch ahead of time, we do backpatching:
     * first, we emit 0xFFFF as the relative jump offset which acts as
     * a placeholder for the real jump offset that will be known only
     * after we compile the 'then' branch because at that point the
     * size of the 'then' branch is known. */ 
    int then_jump = emit_placeholder(chunk, OP_JZ);
    
    compile(chunk, *s.then_branch);

    int else_jump = emit_placeholder(chunk, OP_JMP);

    /* Then, we patch the 'then' jump. */
    patch_placeholder(chunk, then_jump);

    if (s.else_branch != NULL) {
        compile(chunk, *s.else_branch);
    }

    /* Finally, we patch the 'else' jump. If the 'else' branch
     * wasn't compiled, the offset should be zeroed out. */
    patch_placeholder(chunk, else_jump);
}

static void handle_compile_statement_while(BytecodeChunk *chunk, Statement stmt) {
    /* We need to mark the beginning of the loop before we compile
     * the conditional expression, so that we know where to return
     * after the body of the loop is executed. */
    WhileStatement s = TO_STMT_WHILE(stmt);

    int loop_start = chunk->code.count;
    dynarray_insert(&compiler.continues, loop_start);
    size_t loop_start_count = compiler.continues.count;

    /* We then compile the conditional expression because the VM
    .* expects something like OP_EQ to have already been executed
     * and a boolean placed on the stack by the time it encounters
     * an instruction like OP_JZ. */
    compile_expression(chunk, s.condition);

    /* Then, we emit an OP_JZ which jumps to the else clause if the
     * condition is falsey. Because we do not know the size of the
     * bytecode in the body of the 'while' loop ahead of time, we do
     * backpatching: first, we emit 0xFFFF as the relative jump offset
     * which acts as a placeholder for the real jump offset that will
     * be known only after we compile the body of the 'while' loop,
     * because at that point its size is known. */ 
    int exit_jump = emit_placeholder(chunk, OP_JZ);
    
    /* Then, we compile the body of the loop. */
    compile(chunk, *s.body);

    /* Then, we emit OP_JMP with a negative offset. */
    emit_loop(chunk, loop_start);

    if (compiler.breaks.count > 0) {
        int break_jump = dynarray_pop(&compiler.breaks);
        patch_placeholder(chunk, break_jump);
    }

    /* If a 'continue' wasn't popped off the backjmp stack, pop it. */
    if (compiler.continues.count == loop_start_count) {
        dynarray_pop(&compiler.continues);
    }

    /* Finally, we patch the jump. */
    patch_placeholder(chunk, exit_jump);
}

static void handle_compile_statement_fn(BytecodeChunk *chunk, Statement stmt) {
    FunctionStatement s = TO_STMT_FN(stmt);

    /* Create a new Function object and insert it
     * into the enclosing compiler's functions Table.
     * NOTE: the enclosing compiler will be the one
     * with scope depth=0. */
    Function func = {
        .name = s.name,
        .paramcount = s.parameters.count,
        .location = chunk->code.count + 3,
    };
    table_insert(&compiler.functions, func.name, AS_FUNC(func));
    compiler.pops[1] += s.parameters.count;

    /* Copy the function parameters into the current
     * compiler's locals array. */
    COPY_DYNARRAY(&compiler.locals, &s.parameters);

    /* Emit the jump because we don't want to execute
     * the code the first time we encounter it. */
    int jump = emit_placeholder(chunk, OP_JMP);

    /* Compile the function body. */
    compile(chunk, *s.body);

    /* If the function does not have a return statement,
     * clean the stack up here (because statement_block()
     * does not clean up the stack for scope depths of 1)
     * and emit OP_NULL because OP_RET that follows expects
     * some return value to be on the stack. */
    emit_byte(chunk, OP_NULL);
    emit_byte(chunk, OP_RET);
 
    /* Finally, patch the jump. */
    patch_placeholder(chunk, jump);
}

static void handle_compile_statement_struct(BytecodeChunk *chunk, Statement stmt) {
    StructStatement s = TO_STMT_STRUCT(stmt);
    DynArray_char_ptr properties = {0};
    for (size_t i = 0; i < s.properties.count; i++) {
        dynarray_insert(
            &properties,
            own_string(s.properties.data[i])
        );
    }
    StructBlueprint blueprint = {
        .name = own_string(s.name),
        .properties = properties
    };
    table_insert(&compiler.structs, blueprint.name, AS_STRUCT_BLUEPRINT(blueprint));
}

static void handle_compile_statement_return(BytecodeChunk *chunk, Statement stmt) {
    /* Compile the return value and emit OP_RET. */
    ReturnStatement s = TO_STMT_RETURN(stmt);
    compile_expression(chunk, s.returnval);
    int deepset_no = compiler.locals.count;
    for (int i = 0; i < compiler.locals.count; i++) {
        emit_byte(chunk, OP_DEEPSET);
        emit_uint32(chunk, deepset_no--);
    }
    emit_byte(chunk, OP_RET);
}

static void handle_compile_statement_break(BytecodeChunk *chunk, Statement stmt) {
    emit_stack_cleanup(chunk);
    int break_jump = emit_placeholder(chunk, OP_JMP);
    dynarray_insert(&compiler.breaks, break_jump);
}

static void handle_compile_statement_continue(BytecodeChunk *chunk, Statement stmt) {
    int loop_start = dynarray_pop(&compiler.continues);
    emit_stack_cleanup(chunk);
    emit_loop(chunk, loop_start);
}

typedef void (*CompileHandlerFn)(BytecodeChunk *chunk, Statement stmt);

typedef struct {
    CompileHandlerFn fn;
    char *name;
} CompileHandler;

CompileHandler handler[] = {
    [STMT_PRINT] = { .fn = handle_compile_statement_print, .name = "STMT_PRINT" },
    [STMT_LET] = { .fn = handle_compile_statement_let, .name = "STMT_LET" },
    [STMT_EXPR] = { .fn = handle_compile_statement_expr, .name = "STMT_EXPR" },
    [STMT_BLOCK] = { .fn = handle_compile_statement_block, .name = "STMT_BLOCK" },
    [STMT_IF] = { .fn = handle_compile_statement_if, .name = "STMT_IF" },
    [STMT_WHILE] = { .fn = handle_compile_statement_while, .name = "STMT_WHILE" },
    [STMT_FN] = { .fn = handle_compile_statement_fn, .name = "STMT_FN" },
    [STMT_STRUCT] = { .fn = handle_compile_statement_struct, .name = "STMT_STRUCT" },
    [STMT_RETURN] = { .fn = handle_compile_statement_return, .name = "STMT_RETURN" },
    [STMT_BREAK] = { .fn = handle_compile_statement_break, .name = "STMT_BREAK" },
    [STMT_CONTINUE] = { .fn = handle_compile_statement_continue, .name = "STMT_CONTINUE" },
};

void compile(BytecodeChunk *chunk, Statement stmt) {
    handler[stmt.kind].fn(chunk, stmt);
}