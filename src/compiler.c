#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "compiler.h"
#include "vm.h"
#include "util.h"

Compiler *current_compiler = NULL;

void init_compiler(Compiler *compiler, size_t depth) {
    memset(compiler, 0, sizeof(Compiler));
    compiler->enclosing = current_compiler;
    if (current_compiler != NULL) {
        /* Inherit everythig else from the current compiler. */
        memcpy(compiler->locals, current_compiler->locals, sizeof(current_compiler->locals));
        compiler->locals_count = current_compiler->locals_count;

        memcpy(compiler->backjmp_stack, current_compiler->backjmp_stack, sizeof(current_compiler->backjmp_stack));
        compiler->backjmp_tos = current_compiler->backjmp_tos;

        memcpy(compiler->jmp_stack, current_compiler->jmp_stack, sizeof(current_compiler->jmp_stack));
        compiler->jmp_tos = current_compiler->jmp_tos;
    }
    compiler->depth = depth;
    current_compiler = compiler;
}

void end_compiler(Compiler *compiler) {
    if (compiler->enclosing != NULL) {
        /* Inherit everythig else from the current compiler. */
        memcpy(compiler->enclosing->backjmp_stack, current_compiler->backjmp_stack, sizeof(current_compiler->backjmp_stack));
        compiler->enclosing->backjmp_tos = current_compiler->backjmp_tos;

        memcpy(compiler->enclosing->jmp_stack, current_compiler->jmp_stack, sizeof(current_compiler->jmp_stack));
        compiler->enclosing->jmp_tos = current_compiler->jmp_tos;        
    }
    current_compiler = compiler->enclosing;
}

void init_chunk(BytecodeChunk *chunk) {
    memset(chunk, 0, sizeof(BytecodeChunk));
}

void free_chunk(BytecodeChunk *chunk) {
    dynarray_free(&chunk->code);
    for (int i = 0; i < chunk->sp_count; i++) 
        free(chunk->sp[i]);
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

static int emit_jump(BytecodeChunk *chunk, Opcode jump) {
    emit_byte(chunk, jump);
    emit_bytes(chunk, 2, 0xFF, 0xFF);
    /* In this case, the jump is the last emitted instruction. 
     *
     * For example, if we have: [
     *     OP_CONST, operand,
     *     OP_CONST, operand,
     *     OP_EQ, 
     *     OP_JZ, operand, operand
     * ]                             ^-- count
     *
     * the count will be 8. Because the indexing is zero-based,
     * the count points to just beyond the two operands, so to
     * get to OP_JZ, we need to subtract 3 (two operands plus
     * one 'extra' slot to adjust for zero-based indexing). */
    return chunk->code.count - 3;
}

static int emit_ip(BytecodeChunk *chunk) {
    emit_bytes(chunk, 3, OP_IP, 0xFF, 0xFF);
    return chunk->code.count - 3;
}

static void patch_ip(BytecodeChunk *chunk, int ip) {
    int16_t bytes_emitted = (chunk->code.count - 1) - (ip + 2);
    chunk->code.data[ip+1] = (bytes_emitted >> 8) & 0xFF;
    chunk->code.data[ip+2] = bytes_emitted & 0xFF;
}

static void patch_jump(BytecodeChunk *chunk, int jump) {
    /* In this case, one or both branches have been compiled.
     *
     * For example, if we have: [
     *     OP_CONST, operand,
     *     OP_CONST, operand,
     *     OP_EQ, 
     *     OP_JZ, operand, operand,
     *     OP_CONST, operand,
     *     OP_PRINT,
     * ]             ^-- count
     *
     * We first adjust for zero-based indexing by subtracting 1
     * (such that count points to the last element. Then we take
     * the index of OP_JZ (5 in this case) and add 2 because we
     * need to adjust for the operands. The result of subtraction
     * of these two is the number of emitted bytes after the jump,
     * and we use that number to build a 16-bit offset that we use
     * to patch the jump. */
    int16_t bytes_emitted = (chunk->code.count - 1) - (jump + 2);
    chunk->code.data[jump+1] = (bytes_emitted >> 8) & 0xFF;
    chunk->code.data[jump+2] = bytes_emitted & 0xFF;
}

static void emit_loop(BytecodeChunk *chunk, int loop_start) {
    /* In this case, the conditional expression has been compiled.
     *
     * For example, if we have: [
     *     OP_STR_CONST @ 14 ('x')
     *     OP_CONST @ 14 ('0.00')
     *     OP_SET_GLOBAL
     *     OP_GET_GLOBAL @ 14
     *     OP_CONST @ 0 ('10.00')
     *     OP_LT
     *     OP_JZ
     *     OP_GET_GLOBAL @ 14
     *     OP_PRINT
     *     OP_STR_CONST @ 14 ('x')
     *     OP_GET_GLOBAL @ 14
     *     OP_CONST @ 13 ('1.00')
     *     OP_ADD
     *     OP_SET_GLOBAL
     *     OP_JMP
     * ]            ^-- count
     *
     * In this case, the loop_start is at '5'. If the count points to
     * one instruction beyond the end of the bytecode '23'), in order
     * to get to the beginning of the loop, we need to go backwards 18
     * instructions (chunk->code.count - loop_start = 23 - 5 = 18).
     * However, we need to make sure we include the 2-byte operand for
     * OP_JMP, so we add +2 to the offset.
     */
    emit_byte(chunk, OP_JMP);
    int16_t offset = -(chunk->code.count - loop_start + 2);
    emit_byte(chunk, (offset >> 8) & 0xFF);
    emit_byte(chunk, offset & 0xFF);
}

static int resolve_local(char *name) {
    Compiler *current = current_compiler;
    while (current != NULL) {
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
    if (current_compiler->depth > 0) {
        int index = resolve_local(e.name);
        if (index != -1) {
            emit_bytes(chunk, 2, OP_DEEPGET, index);
        } else {
            printf("Compiler error: Variable '%s' is not defined.", e.name);
            exit(1);
        }
    } else {
        uint8_t name_index = add_string(chunk, e.name);
        emit_bytes(chunk, 2, OP_GET_GLOBAL, name_index);
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
    int ip = emit_ip(chunk);
    for (size_t i = 0; i < e.arguments.count; i++) {
        compile_expression(chunk, e.arguments.data[i]);
    }
    emit_byte(chunk, OP_INC_FPCOUNT);
    Object *func = resolve_func(e.var.name);
    int16_t jump = -(chunk->code.count - TO_FUNC(*func)->location) - 3;
    emit_bytes(chunk, 3, OP_JMP, (jump >> 8) & 0xFF, jump & 0xFF);
    patch_ip(chunk, ip);
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
        int index = resolve_local(TO_EXPR_VARIABLE(*e.lhs).name);
        if (index != -1) {
            emit_bytes(chunk, 2, OP_DEEPSET, index);
        } else {
            uint8_t name_index = add_string(chunk, TO_EXPR_VARIABLE(*e.lhs).name);
            emit_bytes(chunk, 2, OP_SET_GLOBAL, name_index);
        }
    } else if (e.lhs->kind == EXPR_GET) {
        compile_expression(chunk, *TO_EXPR_GET(*e.lhs).exp);
        uint8_t index = add_string(chunk, TO_EXPR_GET(*e.lhs).property_name);
        emit_bytes(chunk, 2, OP_SETATTR, index);
    } else {
        printf("Compiler error.\n");
        exit(1);
    }
}

static void handle_compile_expression_logical(BytecodeChunk *chunk, Expression exp) {
    LogicalExpression e = TO_EXPR_LOGICAL(exp);
    /* We first compile the left-hand side of the expression. */
    compile_expression(chunk, *e.lhs);
    if (strcmp(e.operator, "&&") == 0) {
        /* For logical AND, we emit a conditional jump which we'll use
         * to jump over the right-hand side operand if the left operand
         * was falsey (aka short-circuiting). Effectively, we will leave 
         * the left operand on the stack as the result of evaluating this
         * expression. */
        int end_jump = emit_jump(chunk, OP_JZ);
        compile_expression(chunk, *e.rhs);
        patch_jump(chunk, end_jump);
    } else if (strcmp(e.operator, "||") == 0) {
        /* For logical OR, we need to short-circuit when the left-hand side
         * is truthy. Thus, we have two jumps: the first one is conditional
         * jump that we use to jump over the code for the right-hand side. 
         * If the left-hand side was truthy, the execution falls through to
         * the second, unconditional jump that skips the code for the right
         * operand. However, if the left-hand side was falsey, it jumps over
         * the unconditional jump and evaluates the right-hand side operand. */
        int else_jump = emit_jump(chunk, OP_JZ);
        int end_jump = emit_jump(chunk, OP_JMP);
        patch_jump(chunk, else_jump);
        compile_expression(chunk, *e.rhs);
        patch_jump(chunk, end_jump);
    }
}

static void handle_compile_expression_struct(BytecodeChunk *chunk, Expression exp) {
    StructExpression e = TO_EXPR_STRUCT(exp);
    Object *blueprintobj = resolve_struct(e.name);
    if (blueprintobj == NULL) {
        printf("Compiler error: struct '%s' is not defined.\n", e.name);
        exit(1);
    }
    StructBlueprint *sb = TO_STRUCT_BLUEPRINT(*blueprintobj);
    if (sb->properties.count != e.initializers.count) {
        printf(
            "Compiler error: struct '%s' requires %ld initializers.\n",
            sb->name,
            sb->properties.count
        );
        exit(1);
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
            printf(
                "Compiler error: struct '%s' requires properties: [",
                sb->name
            );
            for (size_t k = 0; k < sb->properties.count; k++) {
                printf("'%s', ", sb->properties.data[k]);                       
            }
            printf("]\n");
            exit(1);
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

void compile(BytecodeChunk *chunk, Statement stmt, bool scoped);

static void handle_compile_statement_print(BytecodeChunk *chunk, Statement stmt, bool scoped) {
    PrintStatement s = TO_STMT_PRINT(stmt);
    compile_expression(chunk, s.exp);
    emit_byte(chunk, OP_PRINT);
}

static void handle_compile_statement_let(BytecodeChunk *chunk, Statement stmt, bool scoped) {
    LetStatement s = TO_STMT_LET(stmt);
    compile_expression(chunk, s.initializer);
    uint8_t name_index = add_string(chunk, s.name);
    if (!scoped) {
        emit_bytes(chunk, 2, OP_SET_GLOBAL, name_index);
    } else {
        current_compiler->locals[current_compiler->locals_count++] = chunk->sp[name_index];
    }
}

static void handle_compile_statement_expr(BytecodeChunk *chunk, Statement stmt, bool scoped) {
    compile_expression(chunk, TO_STMT_EXPR(stmt).exp);
}

static void handle_compile_statement_block(BytecodeChunk *chunk, Statement stmt, bool scoped) {
    BlockStatement s = TO_STMT_BLOCK(&stmt);
    Compiler compiler;
    init_compiler(&compiler, s.depth);
    for (size_t i = 0; i < s.stmts.count; i++) {
        compile(chunk, s.stmts.data[i], scoped);
    }
    end_compiler(&compiler);
}

static void handle_compile_statement_if(BytecodeChunk *chunk, Statement stmt, bool scoped) {
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
    int then_jump = emit_jump(chunk, OP_JZ);
    
    compile(chunk, *s.then_branch, scoped);

    int else_jump = emit_jump(chunk, OP_JMP);

    /* Then, we patch the 'then' jump. */
    patch_jump(chunk, then_jump);

    if (s.else_branch != NULL) {
        compile(chunk, *s.else_branch, scoped);
    }

    /* Finally, we patch the 'else' jump. If the 'else' branch
     * wasn't compiled, the offset should be zeroed out. */
    patch_jump(chunk, else_jump);
}

static void handle_compile_statement_while(BytecodeChunk *chunk, Statement stmt, bool scoped) {
    /* We need to mark the beginning of the loop before we compile
     * the conditional expression, so that we know where to return
     * after the body of the loop is executed. */
    WhileStatement s = TO_STMT_WHILE(stmt);
    int loop_start = chunk->code.count;

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
    int exit_jump = emit_jump(chunk, OP_JZ);
    
    /* Then, we compile the body of the loop. */
    BlockStatement body = TO_STMT_BLOCK(s.body);
    for (size_t i = 0; i < body.stmts.count; i++) {
        if (body.stmts.data[i].kind == STMT_CONTINUE) {
            current_compiler->backjmp_stack[current_compiler->backjmp_tos++] = loop_start;
        }
        compile(chunk, body.stmts.data[i], scoped);
    }

    /* Then, we emit OP_JMP with a negative offset. */
    emit_loop(chunk, loop_start);

    if (current_compiler->jmp_tos > 0) {
        int break_jump = current_compiler->jmp_stack[--current_compiler->jmp_tos];
        patch_jump(chunk, break_jump);
    }

    /* Finally, we patch the jump. */
    patch_jump(chunk, exit_jump);
}

static void handle_compile_statement_fn(BytecodeChunk *chunk, Statement stmt, bool scoped) {
    FunctionStatement s = TO_STMT_FN(stmt);
    Compiler compiler;
    init_compiler(&compiler, 1);

    Function func = {
        .name = s.name,
        .paramcount = s.parameters.count,
        .location = (uint8_t)(chunk->code.count + 3),
    };

    /* Add parameter names to compiler->locals. */
    for (size_t i = 0; i < s.parameters.count; i++) {
        uint8_t parameter_index = add_string(chunk, s.parameters.data[i]);
        current_compiler->locals[current_compiler->locals_count++] = chunk->sp[parameter_index];
    }
                
    table_insert(&current_compiler->enclosing->functions, func.name, AS_FUNC(func));

    /* Emit the jump because we don't want to execute
     * the code the first time we encounter it. */
    int jump = emit_jump(chunk, OP_JMP);

    /* Compile the function body and check if it is void. */
    BlockStatement body = TO_STMT_BLOCK(s.body);
    bool is_void = true;
    for (size_t i = 0; i < body.stmts.count; i++) {
        if (body.stmts.data[i].kind == STMT_RETURN) {
            is_void = false;
        }
    }

    for (size_t i = 0; i < body.stmts.count; i++) {
        compile(chunk, body.stmts.data[i], true);
    }

    /* If the function does not have a return statement,
        * emit OP_NULL because we have to return something. */
    if (is_void) {
        emit_byte(chunk, OP_NULL);
        int deepset_no = current_compiler->locals_count - 1;
        for (int i = 0; i < current_compiler->locals_count; i++) {
            emit_bytes(chunk, 2, OP_DEEPSET, (uint8_t)deepset_no--);
        }
        emit_byte(chunk, OP_RET);
    }

    /* Finally, patch the jump. */
    patch_jump(chunk, jump);

    end_compiler(&compiler);
}

static void handle_compile_statement_struct(BytecodeChunk *chunk, Statement stmt, bool scoped) {
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

static void handle_compile_statement_return(BytecodeChunk *chunk, Statement stmt, bool scoped) {
    /* Compile the return value and emit OP_RET. */
    ReturnStatement s = TO_STMT_RETURN(stmt);
    compile_expression(chunk, s.returnval);
    int deepset_no = current_compiler->locals_count - 1;
    for (int i = 0; i < current_compiler->locals_count; i++) {
        emit_bytes(chunk, 2, OP_DEEPSET, (uint8_t)deepset_no--);
    }
    emit_byte(chunk, OP_RET);
}

static void handle_compile_statement_break(BytecodeChunk *chunk, Statement stmt, bool scoped) {
    int break_jump = emit_jump(chunk, OP_JMP);
    current_compiler->jmp_stack[current_compiler->jmp_tos++] = break_jump;
}

static void handle_compile_statement_continue(BytecodeChunk *chunk, Statement stmt, bool scoped) {
    if (current_compiler->backjmp_tos > 0) {
        int loop_start = current_compiler->backjmp_stack[--current_compiler->backjmp_tos];
        emit_loop(chunk, loop_start);
    }
}

typedef void (*CompileHandlerFn)(BytecodeChunk *chunk, Statement stmt, bool scoped);

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

void compile(BytecodeChunk *chunk, Statement stmt, bool scoped) {
    handler[stmt.kind].fn(chunk, stmt, scoped);
}