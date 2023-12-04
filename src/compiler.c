#include "compiler.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "util.h"

#define COMPILER_ERROR(...)                                                    \
  do {                                                                         \
    fprintf(stderr, "Compiler error: ");                                       \
    fprintf(stderr, __VA_ARGS__);                                              \
    fprintf(stderr, "\n");                                                     \
    exit(1);                                                                   \
  } while (0)

void init_compiler(Compiler *compiler) {
  memset(compiler, 0, sizeof(Compiler));
  compiler->functions = malloc(sizeof(Table_Function));
  compiler->struct_blueprints = malloc(sizeof(Table_StructBlueprint));
  memset(compiler->functions, 0, sizeof(Table_Function));
  memset(compiler->struct_blueprints, 0, sizeof(Table_StructBlueprint));
}

void free_table_int(Table_int *table) {
  for (size_t i = 0; i < TABLE_MAX; i++) {
    if (table->indexes[i] != NULL) {
      Bucket *bucket = table->indexes[i];
      list_free(bucket);
    }
  }
}

void free_table_struct_blueprints(Table_StructBlueprint *table) {
  for (size_t i = 0; i < TABLE_MAX; i++) {
    if (table->indexes[i] != NULL) {
      Bucket *bucket = table->indexes[i];
      list_free(bucket);
    }
  }
  for (size_t i = 0; i < table->count; i++) {
    free_table_int(table->items[i].property_indexes);
    free(table->items[i].property_indexes);
  }
}

void free_table_functions(Table_Function *table) {
  for (size_t i = 0; i < TABLE_MAX; i++) {
    if (table->indexes[i] != NULL) {
      Bucket *bucket = table->indexes[i];
      list_free(bucket);
    }
  }
}

void free_compiler(Compiler *compiler) {
  dynarray_free(&compiler->globals);
  dynarray_free(&compiler->locals);
  dynarray_free(&compiler->breaks);
  dynarray_free(&compiler->loop_starts);
  free_table_struct_blueprints(compiler->struct_blueprints);
  free_table_functions(compiler->functions);
  free(compiler->struct_blueprints);
  free(compiler->functions);
}

void init_chunk(BytecodeChunk *chunk) {
  memset(chunk, 0, sizeof(BytecodeChunk));
}

void free_chunk(BytecodeChunk *chunk) {
  dynarray_free(&chunk->code);
  dynarray_free(&chunk->sp);
  dynarray_free(&chunk->cp);
}

static void begin_scope(Compiler *compiler) { compiler->depth++; }

static void end_scope(Compiler *compiler) {
  for (int i = 0; i < compiler->pops[compiler->depth]; i++) {
    dynarray_pop(&compiler->locals);
  }
  compiler->pops[compiler->depth] = 0;
  compiler->depth--;
}

static uint32_t add_string(BytecodeChunk *chunk, char *string) {
  /* Check if the string is already present in the pool. */
  for (size_t idx = 0; idx < chunk->sp.count; idx++) {
    /* If it is, return the index. */
    if (strcmp(chunk->sp.data[idx], string) == 0) {
      return idx;
    }
  }
  /* Otherwise, insert it into the pool and return the index. */
  dynarray_insert(&chunk->sp, string);
  return chunk->sp.count - 1;
}

static uint32_t add_constant(BytecodeChunk *chunk, double constant) {
  /* Check if the constant is already present in the pool. */
  for (size_t idx = 0; idx < chunk->cp.count; idx++) {
    /* If it is, return the index. */
    if (chunk->cp.data[idx] == constant) {
      return idx;
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

static void emit_uint32(BytecodeChunk *chunk, uint32_t idx) {
  emit_bytes(chunk, 4, (idx >> 24) & 0xFF, (idx >> 16) & 0xFF,
             (idx >> 8) & 0xFF, idx & 0xFF);
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
  /* This function takes a zero-based index of the opcode,
   * 'op', and patches the following offset with the numb-
   * er of emitted instructions that come after the opcode
   * and the placeholder.
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
   * 'op' will be 5. To get the number of emitted instruc-
   * tions, the count is adjusted by subtracting 1 (so th-
   * at it points to the last element). Then, two is added
   * to the index to account for the two-byte operand that
   * comes after the opcode. The result of the subtraction
   * of these two is the number of emitted bytes, which is
   * used to build a signed 16-bit offset to patch the pl-
   * aceholder. */
  int16_t bytes_emitted = (chunk->code.count - 1) - (op + 2);
  chunk->code.data[op + 1] = (bytes_emitted >> 8) & 0xFF;
  chunk->code.data[op + 2] = bytes_emitted & 0xFF;
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

static void emit_stack_cleanup(Compiler *compiler, BytecodeChunk *chunk) {
  int pop_count = compiler->pops[compiler->depth];
  for (int i = 0; i < pop_count; i++) {
    emit_byte(chunk, OP_POP);
  }
}

static bool resolve_global(Compiler *compiler, char *name) {
  /* Check if 'name' is present in the globals dynarray. */
  for (size_t idx = 0; idx < compiler->globals.count; idx++) {
    if (strcmp(compiler->globals.data[idx], name) == 0) {
      return true;
    }
  }
  return false;
}

static int resolve_local(Compiler *compiler, char *name) {
  /* Check if 'name' is present in the locals dynarray.
   * If it is, return the index, otherwise return -1. */
  for (size_t idx = 0; idx < compiler->locals.count; idx++) {
    if (strcmp(compiler->locals.data[idx], name) == 0) {
      return idx;
    }
  }
  return -1;
}

static void compile_expression(Compiler *compiler, BytecodeChunk *chunk,
                               Expression exp);

static void handle_compile_expression_literal(Compiler *compiler,
                                              BytecodeChunk *chunk,
                                              Expression exp) {
  LiteralExpression e = TO_EXPR_LITERAL(exp);
  switch (e.kind) {
  case LITERAL_BOOL: {
    emit_byte(chunk, OP_TRUE);
    if (!e.as.bval) {
      emit_byte(chunk, OP_NOT);
    }
    break;
  }
  case LITERAL_NUMBER: {
    uint32_t const_idx = add_constant(chunk, e.as.dval);
    emit_byte(chunk, OP_CONST);
    emit_uint32(chunk, const_idx);
    break;
  }
  case LITERAL_STRING: {
    uint32_t str_idx = add_string(chunk, e.as.sval);
    emit_byte(chunk, OP_STR);
    emit_uint32(chunk, str_idx);
    break;
  }
  case LITERAL_NULL: {
    emit_byte(chunk, OP_NULL);
    break;
  }
  default:
    assert(0);
  }
}

static void handle_compile_expression_variable(Compiler *compiler,
                                               BytecodeChunk *chunk,
                                               Expression exp) {
  VariableExpression e = TO_EXPR_VARIABLE(exp);
  /* First try to resolve the variable as local. */
  int idx = resolve_local(compiler, e.name);
  if (idx != -1) {
    /* If it is found, emit OP_DEEPGET. */
    emit_byte(chunk, OP_DEEPGET);
    emit_uint32(chunk, idx);
  } else {
    /* Otherwise, try to resolve it as global. */
    bool is_defined = resolve_global(compiler, e.name);
    if (is_defined) {
      /* If it is found, emit OP_GET_GLOBAL. */
      uint32_t name_idx = add_string(chunk, e.name);
      emit_byte(chunk, OP_GET_GLOBAL);
      emit_uint32(chunk, name_idx);
    } else {
      /* Otherwise, the variable is not defined, so bail out. */
      COMPILER_ERROR("Variable '%s' is not defined.", e.name);
    }
  }
}

static void handle_compile_expression_unary(Compiler *compiler,
                                            BytecodeChunk *chunk,
                                            Expression exp) {
  UnaryExpression e = TO_EXPR_UNARY(exp);
  if (strcmp(e.op, "-") == 0) {
    compile_expression(compiler, chunk, *e.exp);
    emit_byte(chunk, OP_NEG);
  } else if (strcmp(e.op, "!") == 0) {
    compile_expression(compiler, chunk, *e.exp);
    emit_byte(chunk, OP_NOT);
  } else if (strcmp(e.op, "*") == 0) {
    compile_expression(compiler, chunk, *e.exp);
    emit_byte(chunk, OP_DEREF);
  } else if (strcmp(e.op, "&") == 0) {
    switch (e.exp->kind) {
    case EXP_VARIABLE: {
      VariableExpression var = TO_EXPR_VARIABLE(*e.exp);
      /* Try to resolve the variable as local. */
      int idx = resolve_local(compiler, var.name);
      if (idx != -1) {
        /* If it is found, push its address on the stack. */
        emit_byte(chunk, OP_DEEPGET_PTR);
        emit_uint32(chunk, idx);
      } else {
        /* Otherwise, try to resolve it as global. */
        bool is_defined = resolve_global(compiler, var.name);
        if (is_defined) {
          /* If it is found, emit OP_GET_GLOBAL. */
          uint32_t name_idx = add_string(chunk, var.name);
          emit_byte(chunk, OP_GET_GLOBAL_PTR);
          emit_uint32(chunk, name_idx);
        } else {
          /* Otherwise, the variable is not defined, so bail out. */
          COMPILER_ERROR("Variable '%s' is not defined.", var.name);
        }
      }
      break;
    }
    case EXP_GET: {
      GetExpression getexp = TO_EXPR_GET(*e.exp);
      /* Compile the part that comes be-
       * fore the member access operator. */
      compile_expression(compiler, chunk, *getexp.exp);
      /* Deref if the operator is '->'. */
      if (strcmp(getexp.op, "->") == 0) {
        emit_byte(chunk, OP_DEREF);
      }
      /* Add the 'property_name' string to the
       * chunk's sp, and emit OP_GETATTR_PTR. */
      uint32_t property_name_idx = add_string(chunk, getexp.property_name);
      emit_byte(chunk, OP_GETATTR_PTR);
      emit_uint32(chunk, property_name_idx);
      break;
    }
    default:
      break;
    }
  } else if (strcmp(e.op, "~") == 0) {
    compile_expression(compiler, chunk, *e.exp);
    emit_byte(chunk, OP_BITWISE_NOT);
  }
}

static void handle_compile_expression_binary(Compiler *compiler,
                                             BytecodeChunk *chunk,
                                             Expression exp) {
  BinaryExpression e = TO_EXPR_BINARY(exp);

  compile_expression(compiler, chunk, *e.lhs);
  compile_expression(compiler, chunk, *e.rhs);

  if (strcmp(e.op, "+") == 0) {
    emit_byte(chunk, OP_ADD);
  } else if (strcmp(e.op, "-") == 0) {
    emit_byte(chunk, OP_SUB);
  } else if (strcmp(e.op, "*") == 0) {
    emit_byte(chunk, OP_MUL);
  } else if (strcmp(e.op, "/") == 0) {
    emit_byte(chunk, OP_DIV);
  } else if (strcmp(e.op, "%%") == 0) {
    emit_byte(chunk, OP_MOD);
  } else if (strcmp(e.op, "&") == 0) {
    emit_byte(chunk, OP_BITWISE_AND);
  } else if (strcmp(e.op, "|") == 0) {
    emit_byte(chunk, OP_BITWISE_OR);
  } else if (strcmp(e.op, "^") == 0) {
    emit_byte(chunk, OP_BITWISE_XOR);
  } else if (strcmp(e.op, ">") == 0) {
    emit_byte(chunk, OP_GT);
  } else if (strcmp(e.op, "<") == 0) {
    emit_byte(chunk, OP_LT);
  } else if (strcmp(e.op, ">=") == 0) {
    emit_bytes(chunk, 2, OP_LT, OP_NOT);
  } else if (strcmp(e.op, "<=") == 0) {
    emit_bytes(chunk, 2, OP_GT, OP_NOT);
  } else if (strcmp(e.op, "==") == 0) {
    emit_byte(chunk, OP_EQ);
  } else if (strcmp(e.op, "!=") == 0) {
    emit_bytes(chunk, 2, OP_EQ, OP_NOT);
  } else if (strcmp(e.op, "<<") == 0) {
    emit_byte(chunk, OP_BITWISE_SHIFT_LEFT);
  } else if (strcmp(e.op, ">>") == 0) {
    emit_byte(chunk, OP_BITWISE_SHIFT_RIGHT);
  } else if (strcmp(e.op, "++") == 0) {
    emit_byte(chunk, OP_STRCAT);
  }
}

static void handle_compile_expression_call(Compiler *compiler,
                                           BytecodeChunk *chunk,
                                           Expression exp) {
  CallExpression e = TO_EXPR_CALL(exp);

  /* Error out at compile time if the function is not defined. */
  Function *func = table_get(compiler->functions, e.var.name);
  if (func == NULL) {
    COMPILER_ERROR("Function '%s' is not defined.", e.var.name);
  }

  if (func->paramcount != e.arguments.count) {
    COMPILER_ERROR("Function '%s' requires %ld arguments.", e.var.name,
                   func->paramcount);
  }

  /* Then compile the arguments */
  for (size_t i = 0; i < e.arguments.count; i++) {
    compile_expression(compiler, chunk, e.arguments.data[i]);
  }

  /* Emit a direct OP_JMP to the function's location.
   * The length of the jump sequence (OP_JMP + 2-byte offset,
   * which is 3), needs to be taken into the account, because
   * by the time the VM executes this jump, it will have read
   * both the jump and the offset, which means that, effectively,
   * we'll not be jumping from the current location, but three
   * slots after it. */
  emit_byte(chunk, OP_CALL);
  emit_uint32(chunk, e.arguments.count);

  int16_t jump = -(chunk->code.count + 3 - func->location);
  emit_bytes(chunk, 3, OP_JMP, (jump >> 8) & 0xFF, jump & 0xFF);
}

static void handle_compile_expression_get(Compiler *compiler,
                                          BytecodeChunk *chunk,
                                          Expression exp) {
  GetExpression e = TO_EXPR_GET(exp);

  /* Compile the part that comes before
   * the member access operator. */
  compile_expression(compiler, chunk, *e.exp);

  if (strcmp(e.op, "->") == 0) {
    emit_byte(chunk, OP_DEREF);
  }

  /* Add the 'property_name' string to the
   * chunk's sp, and emit OP_GETATTR with
   * the index. */
  uint32_t property_name_idx = add_string(chunk, e.property_name);
  emit_byte(chunk, OP_GETATTR);
  emit_uint32(chunk, property_name_idx);
}

static void handle_compile_expression_assign(Compiler *compiler,
                                             BytecodeChunk *chunk,
                                             Expression exp) {
  AssignExpression e = TO_EXPR_ASSIGN(exp);

  bool specialized_assignment = false;
  if (strcmp(e.op, "=") != 0) {
    specialized_assignment = true;
  }

  /* If the left-hand side is a variable (as opposed to a get expression) */
  if (e.lhs->kind == EXP_VARIABLE) {
    /* First compile the right-hand side of the assigment. */
    if (!specialized_assignment) {
      compile_expression(compiler, chunk, *e.rhs);
      VariableExpression var = TO_EXPR_VARIABLE(*e.lhs);
      /* Try to resolve it as a local. */
      int idx = resolve_local(compiler, var.name);
      if (idx != -1) {
        /* If it is found in locals, emit OP_DEEPSET. */
        emit_byte(chunk, OP_DEEPSET);
        emit_uint32(chunk, idx);
      } else {
        /* If it is not found in locals, try to resolve it as global. */
        bool is_defined = resolve_global(compiler, var.name);
        if (is_defined) {
          /* If it is found in globals, emit OP_SET_GLOBAL. */
          uint32_t name_idx = add_string(chunk, var.name);
          emit_byte(chunk, OP_SET_GLOBAL);
          emit_uint32(chunk, name_idx);
        } else {
          /* If it is not found in globals, bail out. */
          COMPILER_ERROR("Variable '%s' is not defined.", var.name);
        }
      }
    } else {
      uint32_t i = 0;
      bool is_global = false;
      VariableExpression var = TO_EXPR_VARIABLE(*e.lhs);
      /* Try to resolve it as a local. */
      int idx = resolve_local(compiler, var.name);
      if (idx != -1) {
        /* If it is found in locals, emit OP_DEEPSET. */
        emit_byte(chunk, OP_DEEPGET);
        emit_uint32(chunk, idx);
        i = idx;
      } else {
        /* If it is not found in locals, try to resolve it as global. */
        bool is_defined = resolve_global(compiler, var.name);
        if (is_defined) {
          /* If it is found in globals, emit OP_SET_GLOBAL. */
          uint32_t name_idx = add_string(chunk, var.name);
          emit_byte(chunk, OP_GET_GLOBAL);
          emit_uint32(chunk, name_idx);
          is_global = true;
          i = name_idx;
        } else {
          /* If it is not found in globals, bail out. */
          COMPILER_ERROR("Variable '%s' is not defined.", var.name);
        }
      }
      compile_expression(compiler, chunk, *e.rhs);
      if (strcmp(e.op, "+=") == 0)
        emit_byte(chunk, OP_ADD);
      if (strcmp(e.op, "-=") == 0)
        emit_byte(chunk, OP_SUB);
      if (strcmp(e.op, "*=") == 0)
        emit_byte(chunk, OP_MUL);
      if (strcmp(e.op, "/=") == 0)
        emit_byte(chunk, OP_DIV);
      if (strcmp(e.op, "%%=") == 0)
        emit_byte(chunk, OP_MOD);
      if (strcmp(e.op, "&=") == 0)
        emit_byte(chunk, OP_BITWISE_AND);
      if (strcmp(e.op, "|=") == 0)
        emit_byte(chunk, OP_BITWISE_OR);
      if (strcmp(e.op, "^=") == 0)
        emit_byte(chunk, OP_BITWISE_XOR);
      if (strcmp(e.op, ">>=") == 0)
        emit_byte(chunk, OP_BITWISE_SHIFT_RIGHT);
      if (strcmp(e.op, "<<=") == 0)
        emit_byte(chunk, OP_BITWISE_SHIFT_LEFT);

      if (is_global) {
        emit_byte(chunk, OP_SET_GLOBAL);
        emit_uint32(chunk, i);
      } else {
        emit_byte(chunk, OP_DEEPSET);
        emit_uint32(chunk, i);
      }
    }
  } else if (e.lhs->kind == EXP_GET) {
    if (!specialized_assignment) {
      /* If the left-hand side is a get expression (like 'egg.x') */
      GetExpression getexp = TO_EXPR_GET(*e.lhs);

      /* Compile the part before the member access operator ('egg'),
       * because OP_SETATTR expects the struct to be on the stack. */
      compile_expression(compiler, chunk, *getexp.exp);
      if (strcmp(getexp.op, "->") == 0) {
        emit_byte(chunk, OP_DEREF);
      }

      /* Then compile the right-hand side of the assigment. */
      compile_expression(compiler, chunk, *e.rhs);

      /* Emit OP_SETATTR which will pop the value, pop the struct,
       * and set the struct's property to the popped value.*/
      uint32_t property_name_idx = add_string(chunk, getexp.property_name);
      emit_byte(chunk, OP_SETATTR);
      emit_uint32(chunk, property_name_idx);

      /* Since OP_SETATTR will leave the struct on the stack,
       * don't forget to pop it off. */
      emit_byte(chunk, OP_POP);
    } else {
      GetExpression getexp = TO_EXPR_GET(*e.lhs);

      /* Compile the part before the member access operator ('egg'),
       * because OP_SETATTR expects the struct to be on the stack. */
      compile_expression(compiler, chunk, *getexp.exp);
      if (strcmp(getexp.op, "->") == 0) {
        emit_byte(chunk, OP_DEREF);
      }
      /* Then compile the right-hand side of the assigment. */
      compile_expression(compiler, chunk, *e.rhs);

      if (strcmp(e.op, "+=") == 0)
        emit_byte(chunk, OP_ADD);
      if (strcmp(e.op, "-=") == 0)
        emit_byte(chunk, OP_SUB);
      if (strcmp(e.op, "*=") == 0)
        emit_byte(chunk, OP_MUL);
      if (strcmp(e.op, "/=") == 0)
        emit_byte(chunk, OP_DIV);
      if (strcmp(e.op, "%%=") == 0)
        emit_byte(chunk, OP_MOD);
      if (strcmp(e.op, "&=") == 0)
        emit_byte(chunk, OP_BITWISE_AND);
      if (strcmp(e.op, "|=") == 0)
        emit_byte(chunk, OP_BITWISE_OR);
      if (strcmp(e.op, "^=") == 0)
        emit_byte(chunk, OP_BITWISE_XOR);
      if (strcmp(e.op, ">>=") == 0)
        emit_byte(chunk, OP_BITWISE_SHIFT_RIGHT);
      if (strcmp(e.op, "<<=") == 0)
        emit_byte(chunk, OP_BITWISE_SHIFT_LEFT);

      /* Emit OP_SETATTR which will pop the value, pop the struct,
       * and set the struct's property to the popped value.*/
      uint32_t property_name_idx = add_string(chunk, getexp.property_name);
      emit_byte(chunk, OP_SETATTR);
      emit_uint32(chunk, property_name_idx);

      /* Since OP_SETATTR will leave the struct on the stack,
       * don't forget to pop it off. */
      emit_byte(chunk, OP_POP);
    }
  } else if (e.lhs->kind == EXP_UNARY) {
    if (!specialized_assignment) {
      /* First compile the left-hand side of the assigment. */
      UnaryExpression unary = TO_EXPR_UNARY(*e.lhs);
      compile_expression(compiler, chunk, *unary.exp);

      /* Then compile the right-hand side. */
      compile_expression(compiler, chunk, *e.rhs);

      /* Emit OP_DEREFSET. */
      emit_byte(chunk, OP_DEREFSET);
    } else {
      UnaryExpression unary = TO_EXPR_UNARY(*e.lhs);
      compile_expression(compiler, chunk, *unary.exp);

      /* Then compile the right-hand side. */
      compile_expression(compiler, chunk, *e.rhs);

      if (strcmp(e.op, "+=") == 0)
        emit_byte(chunk, OP_ADD);
      if (strcmp(e.op, "-=") == 0)
        emit_byte(chunk, OP_SUB);
      if (strcmp(e.op, "*=") == 0)
        emit_byte(chunk, OP_MUL);
      if (strcmp(e.op, "/=") == 0)
        emit_byte(chunk, OP_DIV);
      if (strcmp(e.op, "%%=") == 0)
        emit_byte(chunk, OP_MOD);
      if (strcmp(e.op, "&=") == 0)
        emit_byte(chunk, OP_BITWISE_AND);
      if (strcmp(e.op, "|=") == 0)
        emit_byte(chunk, OP_BITWISE_OR);
      if (strcmp(e.op, "^=") == 0)
        emit_byte(chunk, OP_BITWISE_XOR);
      if (strcmp(e.op, ">>=") == 0)
        emit_byte(chunk, OP_BITWISE_SHIFT_RIGHT);
      if (strcmp(e.op, "<<=") == 0)
        emit_byte(chunk, OP_BITWISE_SHIFT_LEFT);

      /* Emit OP_DEREFSET. */
      emit_byte(chunk, OP_DEREFSET);
    }
  } else {
    /* If the left-hand side is neither a variable expression
     * nor a get expression, bail out. */
    COMPILER_ERROR("Invalid assignment.");
  }
}

static void handle_compile_expression_logical(Compiler *compiler,
                                              BytecodeChunk *chunk,
                                              Expression exp) {
  LogicalExpression e = TO_EXPR_LOGICAL(exp);
  /* We first compile the left-hand side of the expression. */
  compile_expression(compiler, chunk, *e.lhs);
  if (strcmp(e.op, "&&") == 0) {
    /* For logical AND, we need to short-circuit when the left-hand side is
     * falsey.
     *
     * When the left-hand side is falsey, OP_JZ will eat the boolean value
     * ('false') that was on stack after evaluating the left side, which means
     * we need to make sure to put that value back on the stack. This does not
     * happen if the left-hand side is truthy, because the result of evaluating
     * the right-hand side will remain on the stack. Hence why we need to only
     * care about pushing 'false'.
     *
     * We emit two jumps:
     *
     * 1) a conditial jump that jumps over both the right-hand side and the
     * other jump (described below) - and goes straight to pushing 'false' 2) an
     * unconditional jump that skips over pushing 'false
     *
     * If the left-hand side is falsey, the vm will take the conditional jump
     * and push 'false' on the stack.
     *
     * If the left-hand side is truthy, the vm will evaluate the right-hand side
     * and skip over pushing 'false' on the stack.
     */
    int end_jump = emit_placeholder(chunk, OP_JZ);
    compile_expression(compiler, chunk, *e.rhs);
    int false_jump = emit_placeholder(chunk, OP_JMP);
    patch_placeholder(chunk, end_jump);
    emit_bytes(chunk, 2, OP_TRUE, OP_NOT);
    patch_placeholder(chunk, false_jump);
  } else if (strcmp(e.op, "||") == 0) {
    /* For logical OR, we need to short-circuit when the left-hand side is
     * truthy.
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
     *    (described below) - and goes straight to evaluating the right-hand
     * side 2) an unconditional jump that jumps over evaluating the right-hand
     * side
     *
     * If the left-hand side is truthy, the vm will first push 'true' back on
     * the stack and then fall through to the second, unconditional jump that
     * skips evaluating the right-hand side.
     *
     * If the left-hand side is falsey, it jumps over pushing 'true' back on
     * the stack and the unconditional jump, and evaluates the right-hand side.
     */
    int true_jump = emit_placeholder(chunk, OP_JZ);
    emit_byte(chunk, OP_TRUE);
    int end_jump = emit_placeholder(chunk, OP_JMP);
    patch_placeholder(chunk, true_jump);
    compile_expression(compiler, chunk, *e.rhs);
    patch_placeholder(chunk, end_jump);
  }
}

static void handle_compile_expression_struct(Compiler *compiler,
                                             BytecodeChunk *chunk,
                                             Expression exp) {
  StructExpression e = TO_EXPR_STRUCT(exp);

  /* Look up the struct with that name in compiler's structs table. */
  StructBlueprint *blueprint = table_get(compiler->struct_blueprints, e.name);

  /* If it is not found, bail out. */
  if (!blueprint) {
    COMPILER_ERROR("struct '%s' is not defined.\n", e.name);
  }

  /* If the number of properties in the struct blueprint
   * doesn't match the number of provided initializers, bail out. */
  if (blueprint->property_indexes->count != e.initializers.count) {
    COMPILER_ERROR("struct '%s' requires %ld initializers.\n", blueprint->name,
                   blueprint->property_indexes->count);
  }

  /* Check if the initializer names match the property names. */
  for (size_t i = 0; i < e.initializers.count; i++) {
    StructInitializerExpression siexp =
        e.initializers.data[i].as.expr_struct_init;
    char *propname = TO_EXPR_VARIABLE(*siexp.property).name;
    int *propidx = table_get(blueprint->property_indexes, propname);
    if (!propidx) {
      COMPILER_ERROR("struct '%s' has no property '%s'", blueprint->name,
                     propname);
    }
  }

  /* Everything is okay, so we emit OP_STRUCT followed by struct's
   * name index in the chunk's sp, and the count of initializers. */
  uint32_t name_idx = add_string(chunk, blueprint->name);
  emit_byte(chunk, OP_STRUCT);
  emit_uint32(chunk, name_idx);

  /* Finally, we compile the initializers. */
  for (size_t i = 0; i < e.initializers.count; i++) {
    compile_expression(compiler, chunk, e.initializers.data[i]);
  }
}

static void handle_compile_expression_struct_init(Compiler *compiler,
                                                  BytecodeChunk *chunk,
                                                  Expression exp) {
  StructInitializerExpression e = TO_EXPR_STRUCT_INIT(exp);

  /* First, we compile the value of the initializer, since
   * OP_SETATTR expects the value to already be on the stack. */
  compile_expression(compiler, chunk, *e.value);

  /* Then, we add the property name string into the chunk's sp. */
  VariableExpression property = TO_EXPR_VARIABLE(*e.property);
  uint32_t property_name_idx = add_string(chunk, property.name);

  /* Finally, we emit OP_SETATTR with the returned index. */
  emit_byte(chunk, OP_SETATTR);
  emit_uint32(chunk, property_name_idx);
}

typedef void (*CompileExpressionHandlerFn)(Compiler *compiler,
                                           BytecodeChunk *chunk,
                                           Expression exp);

typedef struct {
  CompileExpressionHandlerFn fn;
  char *name;
} CompileExpressionHandler;

static CompileExpressionHandler expression_handler[] = {
    [EXP_LITERAL] = {.fn = handle_compile_expression_literal,
                     .name = "EXP_LITERAL"},
    [EXP_VARIABLE] = {.fn = handle_compile_expression_variable,
                      .name = "EXP_VARIABLE"},
    [EXP_UNARY] = {.fn = handle_compile_expression_unary, .name = "EXP_UNARY"},
    [EXP_BINARY] = {.fn = handle_compile_expression_binary,
                    .name = "EXP_BINARY"},
    [EXP_CALL] = {.fn = handle_compile_expression_call, .name = "EXP_CALL"},
    [EXP_GET] = {.fn = handle_compile_expression_get, .name = "EXP_GET"},
    [EXP_ASSIGN] = {.fn = handle_compile_expression_assign,
                    .name = "EXP_ASSIGN"},
    [EXP_LOGICAL] = {.fn = handle_compile_expression_logical,
                     .name = "EXP_LOGICAL"},
    [EXP_STRUCT] = {.fn = handle_compile_expression_struct,
                    .name = "EXP_STRUCT"},
    [EXP_STRUCT_INIT] = {.fn = handle_compile_expression_struct_init,
                         .name = "EXP_STRUCT_INIT"},
};

static void compile_expression(Compiler *compiler, BytecodeChunk *chunk,
                               Expression exp) {
  expression_handler[exp.kind].fn(compiler, chunk, exp);
}

static void handle_compile_statement_print(Compiler *compiler,
                                           BytecodeChunk *chunk,
                                           Statement stmt) {
  PrintStatement s = TO_STMT_PRINT(stmt);
  compile_expression(compiler, chunk, s.exp);
  emit_byte(chunk, OP_PRINT);
}

static void handle_compile_statement_let(Compiler *compiler,
                                         BytecodeChunk *chunk, Statement stmt) {
  LetStatement s = TO_STMT_LET(stmt);
  compile_expression(compiler, chunk, s.initializer);
  uint32_t name_idx = add_string(chunk, s.name);
  if (compiler->depth == 0) {
    dynarray_insert(&compiler->globals, s.name);
    emit_byte(chunk, OP_SET_GLOBAL);
    emit_uint32(chunk, name_idx);
  } else {
    dynarray_insert(&compiler->locals, chunk->sp.data[name_idx]);
    compiler->pops[compiler->depth]++;
  }
}

static void handle_compile_statement_expr(Compiler *compiler,
                                          BytecodeChunk *chunk,
                                          Statement stmt) {
  ExpressionStatement e = TO_STMT_EXPR(stmt);
  compile_expression(compiler, chunk, e.exp);
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

static void handle_compile_statement_block(Compiler *compiler,
                                           BytecodeChunk *chunk,
                                           Statement stmt) {
  begin_scope(compiler);
  BlockStatement s = TO_STMT_BLOCK(&stmt);

  /* Compile the body of the black. */
  for (size_t i = 0; i < s.stmts.count; i++) {
    compile(compiler, chunk, s.stmts.data[i]);
  }

  emit_stack_cleanup(compiler, chunk);
  end_scope(compiler);
}

static void handle_compile_statement_if(Compiler *compiler,
                                        BytecodeChunk *chunk, Statement stmt) {
  /* We first compile the conditional expression because the VM
  .* expects something like OP_EQ to have already been executed
   * and a boolean placed on the stack by the time it encounters
   * an instruction like OP_JZ. */
  IfStatement s = TO_STMT_IF(stmt);
  compile_expression(compiler, chunk, s.condition);

  /* Then, we emit an OP_JZ which jumps to the else clause if the
   * condition is falsey. Because we do not know the size of the
   * bytecode in the 'then' branch ahead of time, we do backpatching:
   * first, we emit 0xFFFF as the relative jump offset which acts as
   * a placeholder for the real jump offset that will be known only
   * after we compile the 'then' branch because at that point the
   * size of the 'then' branch is known. */
  int then_jump = emit_placeholder(chunk, OP_JZ);

  compile(compiler, chunk, *s.then_branch);

  int else_jump = emit_placeholder(chunk, OP_JMP);

  /* Then, we patch the 'then' jump. */
  patch_placeholder(chunk, then_jump);

  if (s.else_branch != NULL) {
    compile(compiler, chunk, *s.else_branch);
  }

  /* Finally, we patch the 'else' jump. If the 'else' branch
   * wasn't compiled, the offset should be zeroed out. */
  patch_placeholder(chunk, else_jump);
}

static void handle_compile_statement_while(Compiler *compiler,
                                           BytecodeChunk *chunk,
                                           Statement stmt) {
  /* We need to mark the beginning of the loop before we compile
   * the conditional expression, so that we know where to return
   * after the body of the loop is executed. */
  WhileStatement s = TO_STMT_WHILE(stmt);

  int loop_start = chunk->code.count;
  dynarray_insert(&compiler->loop_starts, loop_start);
  size_t breakcount = compiler->breaks.count;

  /* We then compile the conditional expression because the VM
  .* expects something like OP_EQ to have already been executed
   * and a boolean placed on the stack by the time it encounters
   * an instruction like OP_JZ. */
  compile_expression(compiler, chunk, s.condition);

  /* Then, we emit an OP_JZ which jumps to the else clause if the
   * condition is falsey. Because we do not know the size of the
   * bytecode in the body of the 'while' loop ahead of time, we do
   * backpatching: first, we emit 0xFFFF as the relative jump offset
   * which acts as a placeholder for the real jump offset that will
   * be known only after we compile the body of the 'while' loop,
   * because at that point its size is known. */
  int exit_jump = emit_placeholder(chunk, OP_JZ);

  /* Then, we compile the body of the loop. */
  compile(compiler, chunk, *s.body);

  /* Then, we emit OP_JMP with a negative offset. */
  emit_loop(chunk, loop_start);

  int to_pop = compiler->breaks.count - breakcount;
  for (int i = 0; i < to_pop; i++) {
    int break_jump = dynarray_pop(&compiler->breaks);
    patch_placeholder(chunk, break_jump);
  }

  dynarray_pop(&compiler->loop_starts);

  /* Finally, we patch the jump. */
  patch_placeholder(chunk, exit_jump);

  if (compiler->depth == 0) {
    assert(compiler->breaks.count == 0);
    assert(compiler->loop_starts.count == 0);
  }
}

static void handle_compile_statement_fn(Compiler *compiler,
                                        BytecodeChunk *chunk, Statement stmt) {
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
  table_insert(compiler->functions, func.name, func);
  compiler->pops[1] += s.parameters.count;

  /* Copy the function parameters into the current
   * compiler's locals array. */
  COPY_DYNARRAY(&compiler->locals, &s.parameters);

  /* Emit the jump because we don't want to execute
   * the code the first time we encounter it. */
  int jump = emit_placeholder(chunk, OP_JMP);

  /* Compile the function body. */
  compile(compiler, chunk, *s.body);

  /* Finally, patch the jump. */
  patch_placeholder(chunk, jump);

  assert(compiler->breaks.count == 0);
  assert(compiler->loop_starts.count == 0);
  assert(compiler->locals.count == 0);
  assert(compiler->pops[1] == 0);
}

static void handle_compile_statement_struct(Compiler *compiler,
                                            BytecodeChunk *chunk,
                                            Statement stmt) {
  StructStatement s = TO_STMT_STRUCT(stmt);

  emit_byte(chunk, OP_STRUCT_BLUEPRINT);

  uint32_t name_idx = add_string(chunk, s.name);
  emit_uint32(chunk, name_idx);

  emit_uint32(chunk, s.properties.count);

  StructBlueprint blueprint = {.name = s.name,
                               .property_indexes = malloc(sizeof(Table_int))};

  memset(blueprint.property_indexes, 0, sizeof(Table_int));

  for (size_t i = 0; i < s.properties.count; i++) {
    uint32_t propname_idx = add_string(chunk, s.properties.data[i]);
    emit_uint32(chunk, propname_idx);
    table_insert(blueprint.property_indexes, s.properties.data[i], i);
    emit_uint32(chunk, i);
  }

  table_insert(compiler->struct_blueprints, blueprint.name, blueprint);
}

static void handle_compile_statement_return(Compiler *compiler,
                                            BytecodeChunk *chunk,
                                            Statement stmt) {
  ReturnStatement s = TO_STMT_RETURN(stmt);

  /* Compile the return value. */
  compile_expression(compiler, chunk, s.returnval);

  /* We need to perform the stack cleanup, but
   * the return value must not be lost, we'll
   * (ab)use OP_DEEPSET for this job.
   *
   * For example, if the stack is:
   *
   * [ptr, 1, 2, 3, <return value>]
   *
   * The cleanup will look like this:
   *
   * [ptr, 1, 2, 3, <return value>]
   * [ptr, 1, 2, <return value>]
   * [ptr, 1, <return value>]
   * [ptr, <return value>]
   *
   * Which is the exact state of the stack that OP_RET expects. */
  int deepset_no = compiler->locals.count - 1;
  for (size_t i = 0; i < compiler->locals.count; i++) {
    emit_byte(chunk, OP_DEEPSET);
    emit_uint32(chunk, deepset_no--);
  }

  /* Finally, emit OP_RET. */
  emit_byte(chunk, OP_RET);
}

static void handle_compile_statement_break(Compiler *compiler,
                                           BytecodeChunk *chunk,
                                           Statement stmt) {
  /* (Ab)use 'compiler->loop_starts' to check if there
   * is any loop_start inserted. If there is, it means
   * that we are in the loop. */
  if (compiler->loop_starts.count > 0) {
    emit_stack_cleanup(compiler, chunk);
    int break_jump = emit_placeholder(chunk, OP_JMP);
    dynarray_insert(&compiler->breaks, break_jump);
  } else {
    COMPILER_ERROR("'break' outside of loop.");
  }
}

static void handle_compile_statement_continue(Compiler *compiler,
                                              BytecodeChunk *chunk,
                                              Statement stmt) {
  if (compiler->loop_starts.count > 0) {
    int loop_start = dynarray_peek(&compiler->loop_starts);
    emit_stack_cleanup(compiler, chunk);
    emit_loop(chunk, loop_start);
  } else {
    COMPILER_ERROR("'continue' outside of loop.");
  }
}

typedef void (*CompileHandlerFn)(Compiler *compiler, BytecodeChunk *chunk,
                                 Statement stmt);

typedef struct {
  CompileHandlerFn fn;
  char *name;
} CompileHandler;

static CompileHandler handler[] = {
    [STMT_PRINT] = {.fn = handle_compile_statement_print, .name = "STMT_PRINT"},
    [STMT_LET] = {.fn = handle_compile_statement_let, .name = "STMT_LET"},
    [STMT_EXPR] = {.fn = handle_compile_statement_expr, .name = "STMT_EXPR"},
    [STMT_BLOCK] = {.fn = handle_compile_statement_block, .name = "STMT_BLOCK"},
    [STMT_IF] = {.fn = handle_compile_statement_if, .name = "STMT_IF"},
    [STMT_WHILE] = {.fn = handle_compile_statement_while, .name = "STMT_WHILE"},
    [STMT_FN] = {.fn = handle_compile_statement_fn, .name = "STMT_FN"},
    [STMT_STRUCT] = {.fn = handle_compile_statement_struct,
                     .name = "STMT_STRUCT"},
    [STMT_RETURN] = {.fn = handle_compile_statement_return,
                     .name = "STMT_RETURN"},
    [STMT_BREAK] = {.fn = handle_compile_statement_break, .name = "STMT_BREAK"},
    [STMT_CONTINUE] = {.fn = handle_compile_statement_continue,
                       .name = "STMT_CONTINUE"},
};

void compile(Compiler *compiler, BytecodeChunk *chunk, Statement stmt) {
  handler[stmt.kind].fn(compiler, chunk, stmt);
}