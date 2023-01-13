#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "compiler.h"
#include "vm.h"
#include "util.h"

Compiler *current_compiler = NULL;

#define COMPILER_ERROR(...) \
do { \
    fprintf(stderr, "Compiler error: "); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
    exit(1); \
} while (0)

#define COPY_ARRAY(dest, src) \
do { \
    memcpy((dest), (src), sizeof((src))); \
} while (0)

static void print_compiler(Compiler *compiler) {
    printf("Compiler { ");
    if (compiler == NULL) {
        printf("NULL }");
        return;
    }
    printf("id: %p, ", compiler);
    printf("depth: %d, ", compiler->depth);
    printf("locals: [");
    for (int i = 0; i < compiler->locals_count; i++) {
        printf("%s, ", compiler->locals[i]);
    }
    printf("]");
    printf(", enclosing: ");
    print_compiler(compiler->enclosing);
    printf(" }");
}

void init_compiler(Compiler *compiler, size_t depth) {
    /* If the scope depth of the compiler we're initializing
     * is the same as the depth of the current_compiler (which
     * happens when a function statement initializes a compiler,
     * followed by another initialization by the block statement),
     * don't iniatialize a new compiler and use the current one. */
    if (current_compiler && current_compiler->depth == depth) {
        return;
    }

    /* Zero-initialize the compiler. */
    memset(compiler, 0, sizeof(Compiler));

    /* Assign the depth. */
    compiler->depth = depth;

    /* Assign the parent compiler. */
    compiler->enclosing = current_compiler;

    if (current_compiler != NULL) {
        /* We want to inherit current_compiler's locals. */
        COPY_ARRAY(compiler->locals, current_compiler->locals);
        compiler->locals_count = current_compiler->locals_count;

        /* We want to forward-propagate current_compiler's backjmp_stack
         * (and the accompanying top of stack pointer) which contains the
         * addresses where loops begin, so that a 'continue' statement
         * could use the address to patch the backward jump. */
        COPY_ARRAY(compiler->backjmp_stack, current_compiler->backjmp_stack);
        compiler->backjmp_tos = current_compiler->backjmp_tos;
    }

#ifdef venom_debug_compiler
    printf("initting compiler: ");
    print_compiler(compiler);
    printf("\n");
#endif

    /* Finally, set the current_compiler to be the newly initialized compiler. */
    current_compiler = compiler;
}

void end_compiler(Compiler *compiler) {
    if (current_compiler->enclosing->depth == 0) {
        return;
    }

    /* We want to backward-propagate current_compiler's jmp_stack
     * (and the accompanying top of stack pointer) which contains the
     * addresses of the jumps, so that we could patch the jump after
     * we compile the entire loop. */
    COPY_ARRAY(compiler->enclosing->jmp_stack, current_compiler->jmp_stack);
    compiler->enclosing->jmp_tos = current_compiler->jmp_tos;

#ifdef venom_debug_compiler
    printf("ending compiler: ");
    print_compiler(compiler);
    printf("\n");
#endif

    /* Finally, set the current_compiler to be the compiler's parent. */
    current_compiler = compiler->enclosing;
}

void free_compiler(Compiler *compiler) {
    table_free(&compiler->structs);
    table_free(&compiler->functions);
}

void init_chunk(BytecodeChunk *chunk) {
    memset(chunk, 0, sizeof(BytecodeChunk));
}

void free_chunk(BytecodeChunk *chunk) {
    dynarray_free(&chunk->code);
    for (int i = 0; i < chunk->sp_count; i++) 
        free(chunk->sp[i]);
}

static bool sp_contains(BytecodeChunk *chunk, const char *string) {
    for (size_t i = 0; i < chunk->sp_count; i++) {
        if (strcmp(chunk->sp[i], string) == 0) {
            return true;
        }
    }
    return false;
}

static uint8_t add_string(BytecodeChunk *chunk, const char *string) {
    /* Check if the string is already present in the pool. */
    for (uint8_t i = 0; i < chunk->sp_count; i++) {
        /* If it is, return the index. */
        if (strcmp(chunk->sp[i], string) == 0) {
            return i;
        }
    }
    /* Otherwise, own the string, insert it into the pool
     * and return the index. */
    char *s = own_string(string);
    chunk->sp[chunk->sp_count++] = s;
    return chunk->sp_count - 1;
}

static uint8_t add_constant(BytecodeChunk *chunk, double constant) {
    /* Check if the constant is already present in the pool. */
    for (uint8_t i = 0; i < chunk->cp_count; i++) {
        /* If it is, return the index. */
        if (chunk->cp[i] == constant) {
            return i;
        }
    }
    /* Otherwise, insert the constant into the pool
     * and return the index. */
    chunk->cp[chunk->cp_count++] = constant;
    return chunk->cp_count - 1;
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
    int pop_count = current_compiler->locals_count - current_compiler->enclosing->locals_count;
    for (int i = 0; i < pop_count; i++) {
        emit_byte(chunk, OP_POP);
    }
}

static int resolve_local(char *name) {
    Compiler *current = current_compiler;
    while (current->depth != 0) {
        for (int i = current->locals_count - 1; i >= 0; i--) {
            if (strcmp(current->locals[i], name) == 0) {
                return i;
            }
        }
        current = current->enclosing;
    }
    return -1;
}

static Object *resolve_func(char *name) {
    Compiler *current = current_compiler;
    while (current != NULL) {
        Object *func = table_get(&current->functions, name);
        if (func != NULL) {
            return func;
        }
        current = current->enclosing;
    }
    return NULL;
}

static Object *resolve_struct(char *name) {
    Compiler *current = current_compiler;
    while (current != NULL) {
        Object *blueprint = table_get(&current->structs, name);
        if (blueprint != NULL) {
            return blueprint;
        }
        current = current->enclosing;
    }
    return NULL;
}

static void compile_expression(BytecodeChunk *chunk, Expression exp);

static void handle_compile_expression_literal(BytecodeChunk *chunk, Expression exp) {
    LiteralExpression e = TO_EXPR_LITERAL(exp);
    if (e.specval == NULL) {
        uint8_t const_index = add_constant(chunk, e.dval);
        emit_bytes(chunk, 2, OP_CONST, const_index);
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
    uint8_t const_index = add_string(chunk, e.str);
    emit_bytes(chunk, 2, OP_STR, const_index);
}

static void handle_compile_expression_variable(BytecodeChunk *chunk, Expression exp) {
    VariableExpression e = TO_EXPR_VARIABLE(exp);
    int index = resolve_local(e.name);
    if (index != -1) {
        emit_bytes(chunk, 2, OP_DEEPGET, index);
    } else {
        if (sp_contains(chunk, e.name)) {
            uint8_t name_index = add_string(chunk, e.name);
            emit_bytes(chunk, 2, OP_GET_GLOBAL, name_index);
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
    uint8_t index = add_string(chunk, e.property_name);
    emit_bytes(chunk, 2, OP_GETATTR, index);
}

static void handle_compile_expression_assign(BytecodeChunk *chunk, Expression exp) {
    AssignExpression e = TO_EXPR_ASSIGN(exp);
    compile_expression(chunk, *e.rhs);
    if (e.lhs->kind == EXP_VARIABLE) {
        VariableExpression var = TO_EXPR_VARIABLE(*e.lhs);
        int index = resolve_local(var.name);
        if (index != -1) {
            emit_bytes(chunk, 2, OP_DEEPSET, index);
        } else {
            if (sp_contains(chunk, var.name)) {
                uint8_t name_index = add_string(chunk, var.name);
                emit_bytes(chunk, 2, OP_SET_GLOBAL, name_index);
            } else {
                COMPILER_ERROR("Variable '%s' is not defined.", var.name);
            }
        }
    } else if (e.lhs->kind == EXPR_GET) {
        compile_expression(chunk, *TO_EXPR_GET(*e.lhs).exp);
        uint8_t index = add_string(chunk, TO_EXPR_GET(*e.lhs).property_name);
        emit_bytes(chunk, 2, OP_SETATTR, index);
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
            int msg_len = 0;
            for (size_t k = 0; k < sb->properties.count; k++) {
                msg_len += strlen(sb->properties.data[k]) + 2; // 2 = len(", ")                       
            }
            char properties[msg_len+1];
            for (size_t k = 0; k < sb->properties.count; k++) {
                strcat(properties, sb->properties.data[k]);
                strcat(properties, ", ");                      
            }
            properties[msg_len] = '\0';
            COMPILER_ERROR(
                "struct '%s' requires properties: [%s]", sb->name, properties
            );
        }
    }
    uint8_t name_index = add_string(chunk, sb->name);
    emit_bytes(chunk, 3, OP_STRUCT, name_index, e.initializers.count);
    for (size_t i = 0; i < e.initializers.count; i++) {
        compile_expression(chunk, e.initializers.data[i]);
    }
}

static void handle_compile_expression_struct_init(BytecodeChunk *chunk, Expression exp) {
    StructInitializerExpression e = TO_EXPR_STRUCT_INIT(exp);
    compile_expression(chunk, *e.value);
    VariableExpression key = TO_EXPR_VARIABLE(*e.property);
    uint8_t property_name_index = add_string(chunk, key.name);
    emit_bytes(chunk, 2, OP_SETATTR, property_name_index);
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
    uint8_t name_index = add_string(chunk, s.name);
    if (current_compiler->depth == 0) {
        emit_bytes(chunk, 2, OP_SET_GLOBAL, name_index);
    } else {
        current_compiler->locals[current_compiler->locals_count++] = chunk->sp[name_index];
    }
}

static void handle_compile_statement_expr(BytecodeChunk *chunk, Statement stmt) {
    compile_expression(chunk, TO_STMT_EXPR(stmt).exp);
}

static void handle_compile_statement_block(BytecodeChunk *chunk, Statement stmt) {
    BlockStatement s = TO_STMT_BLOCK(&stmt);
    Compiler compiler;

    init_compiler(&compiler, s.depth);

    /* Compile the body of the black. */
    for (size_t i = 0; i < s.stmts.count; i++) {
        compile(chunk, s.stmts.data[i]);
    }

    /* After the block ends, we want to clean up the stack. */
    emit_stack_cleanup(chunk);
    end_compiler(&compiler);
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

    // int logical_jump = current_compiler->logical_stack[--current_compiler->logical_count];
    // patch_placeholder(chunk, logical_jump);

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
    current_compiler->backjmp_stack[current_compiler->backjmp_tos++] = loop_start;
    size_t backjmp_tos_before = current_compiler->backjmp_tos;

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

    if (current_compiler->jmp_tos > 0) {
        int break_jump = current_compiler->jmp_stack[--current_compiler->jmp_tos];
        patch_placeholder(chunk, break_jump);
    }

    /* If a 'continue' wasn't popped off the backjmp stack, pop it. */
    if (current_compiler->backjmp_tos == backjmp_tos_before) {
        current_compiler->backjmp_tos--;
    }

    /* Finally, we patch the jump. */
    patch_placeholder(chunk, exit_jump);

}

static void handle_compile_statement_fn(BytecodeChunk *chunk, Statement stmt) {
    FunctionStatement s = TO_STMT_FN(stmt);
    Compiler compiler;
    init_compiler(&compiler, 1);

    Function func = {
        .name = s.name,
        .paramcount = s.parameters.count,
        .location = (uint8_t)(chunk->code.count + 3),
    };

    table_insert(&current_compiler->enclosing->functions, func.name, AS_FUNC(func));

    if (s.parameters.count > 0) {
        memcpy(current_compiler->locals, s.parameters.data, sizeof(*s.parameters.data));
        current_compiler->locals_count += s.parameters.count;
    }
                

    /* Emit the jump because we don't want to execute
     * the code the first time we encounter it. */
    int jump = emit_placeholder(chunk, OP_JMP);

    compile(chunk, *s.body);

    /* If the function does not have a return statement,
     * emit OP_NULL because we have to return something. */
    emit_byte(chunk, OP_NULL);
    emit_byte(chunk, OP_RET);
 
    /* Finally, patch the jump. */
    patch_placeholder(chunk, jump);

    end_compiler(&compiler);
}

static void handle_compile_statement_struct(BytecodeChunk *chunk, Statement stmt) {
    StructStatement s = TO_STMT_STRUCT(stmt);
    String_DynArray properties = {0};
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
    table_insert(&current_compiler->structs, blueprint.name, AS_STRUCT_BLUEPRINT(blueprint));
}

static void handle_compile_statement_return(BytecodeChunk *chunk, Statement stmt) {
    /* Compile the return value and emit OP_RET. */
    ReturnStatement s = TO_STMT_RETURN(stmt);
    compile_expression(chunk, s.returnval);
    int deepset_no = current_compiler->locals_count-1;
    for (int i = 0; i < current_compiler->locals_count; i++) {
        emit_bytes(chunk, 2, OP_DEEPSET, (uint8_t)deepset_no--);
    }
    emit_byte(chunk, OP_RET);
}

static void handle_compile_statement_break(BytecodeChunk *chunk, Statement stmt) {
    emit_stack_cleanup(chunk);
    int break_jump = emit_placeholder(chunk, OP_JMP);
    current_compiler->jmp_stack[current_compiler->jmp_tos++] = break_jump;
}

static void handle_compile_statement_continue(BytecodeChunk *chunk, Statement stmt) {
    int loop_start = current_compiler->backjmp_stack[--current_compiler->backjmp_tos];
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