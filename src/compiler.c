#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"

#define COMPILER_ERROR(...)                                                    \
  do {                                                                         \
    fprintf(stderr, "compiler: ");                                             \
    fprintf(stderr, __VA_ARGS__);                                              \
    fprintf(stderr, "\n");                                                     \
    exit(1);                                                                   \
  } while (0)

void init_compiler(Compiler *compiler) {
  memset(compiler, 0, sizeof(Compiler));
  compiler->functions = calloc(1, sizeof(Table_Function));
  compiler->struct_blueprints = calloc(1, sizeof(Table_StructBlueprint));
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
    free_table_functions(table->items[i].methods);
    free(table->items[i].methods);
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
  dynarray_free(&compiler->loop_depths);
  free_table_struct_blueprints(compiler->struct_blueprints);
  free_table_functions(compiler->functions);
  free(compiler->struct_blueprints);
  free(compiler->functions);
}

void init_chunk(Bytecode *code) { memset(code, 0, sizeof(Bytecode)); }

void free_chunk(Bytecode *code) {
  dynarray_free(&code->code);
  dynarray_free(&code->sp);
  dynarray_free(&code->cp);
}

static void begin_scope(Compiler *compiler) { compiler->depth++; }

static void end_scope(Compiler *compiler) {
  for (int i = 0; i < compiler->pops[compiler->depth]; i++) {
    dynarray_pop(&compiler->locals);
  }
  compiler->pops[compiler->depth] = 0;
  compiler->depth--;
}

/* Check if the string is already present in the sp.
 * If not, add it first, and finally return the idx. */
static uint32_t add_string(Bytecode *code, char *string) {
  for (size_t idx = 0; idx < code->sp.count; idx++) {
    if (strcmp(code->sp.data[idx], string) == 0) {
      return idx;
    }
  }
  dynarray_insert(&code->sp, string);
  return code->sp.count - 1;
}

/* Check if the uint32 is already present in the cp.
 * If not, add it first, and finally return the idx. */
static uint32_t add_constant(Bytecode *code, double constant) {
  for (size_t idx = 0; idx < code->cp.count; idx++) {
    if (code->cp.data[idx] == constant) {
      return idx;
    }
  }
  dynarray_insert(&code->cp, constant);
  return code->cp.count - 1;
}

static void emit_byte(Bytecode *code, uint8_t byte) {
  dynarray_insert(&code->code, byte);
}

static void emit_bytes(Bytecode *code, int n, ...) {
  va_list ap;
  va_start(ap, n);
  for (int i = 0; i < n; i++) {
    uint8_t byte = va_arg(ap, int);
    emit_byte(code, byte);
  }
  va_end(ap);
}

static void emit_uint32(Bytecode *code, uint32_t idx) {
  emit_bytes(code, 4, (idx >> 24) & 0xFF, (idx >> 16) & 0xFF, (idx >> 8) & 0xFF,
             idx & 0xFF);
}

static int emit_placeholder(Bytecode *code, Opcode op) {
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

static void patch_placeholder(Bytecode *code, int op) {
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

static void emit_loop(Bytecode *code, int loop_start) {
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

static void emit_stack_cleanup(Compiler *compiler, Bytecode *code) {
  int pop_count = compiler->pops[compiler->depth];
  for (int i = 0; i < pop_count; i++) {
    emit_byte(code, OP_POP);
  }
}

static void emit_loop_cleanup(Compiler *compiler, Bytecode *code) {
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
   * compiler->loop_depth = 1
   * compiler->depth = 3
   *
   * compiler->pops[2] = 4
   * compiler->pops[3] = 2
   *
   * We want to clean up everything deeper than the loop up to the
   * current compiler->depth. */
  for (int i = dynarray_peek(&compiler->loop_depths) + 1; i <= compiler->depth;
       i++) {
    for (int j = 0; j < compiler->pops[i]; j++) {
      emit_byte(code, OP_POP);
    }
  }
}

/* Check if 'name' is present in the globals dynarray.
 * If it is, return its index in the sp, otherwise -1. */
static int resolve_global(Compiler *compiler, Bytecode *code, char *name) {
  for (size_t idx = 0; idx < compiler->globals.count; idx++) {
    if (strcmp(compiler->globals.data[idx], name) == 0) {
      return add_string(code, name);
    }
  }
  return -1;
}

/* Check if 'name' is present in the locals dynarray.
 * If it is, return the index, otherwise return -1. */
static int resolve_local(Compiler *compiler, char *name) {
  for (size_t idx = 0; idx < compiler->locals.count; idx++) {
    if (strcmp(compiler->locals.data[idx], name) == 0) {
      return idx;
    }
  }
  return -1;
}

static void compile_expr(Compiler *compiler, Bytecode *code, Expr exp);

static void compile_expr_lit(Compiler *compiler, Bytecode *code, Expr exp) {
  ExprLit e = TO_EXPR_LIT(exp);
  switch (e.kind) {
  case LIT_BOOL: {
    emit_byte(code, OP_TRUE);
    if (!e.as.bval) {
      emit_byte(code, OP_NOT);
    }
    break;
  }
  case LIT_NUM: {
    uint32_t const_idx = add_constant(code, e.as.dval);
    emit_byte(code, OP_CONST);
    emit_uint32(code, const_idx);
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
}

static void compile_expr_var(Compiler *compiler, Bytecode *code, Expr exp) {
  ExprVar e = TO_EXPR_VAR(exp);

  /* Try to resolve the variable as local. */
  int idx = resolve_local(compiler, e.name);
  if (idx != -1) {
    /* print compiler->locals dynarray in the form [..., ..., ...] */
    emit_byte(code, OP_DEEPGET);
    emit_uint32(code, idx);
    return;
  }

  /* Try to resolve the variable as global. */
  int name_idx = resolve_global(compiler, code, e.name);
  if (name_idx != -1) {
    emit_byte(code, OP_GET_GLOBAL);
    emit_uint32(code, name_idx);
    return;
  }

  /* The variable is not defined, bail out. */
  COMPILER_ERROR("Variable '%s' is not defined.", e.name);
}

static void compile_expr_una(Compiler *compiler, Bytecode *code, Expr exp) {
  ExprUnary e = TO_EXPR_UNA(exp);
  if (strcmp(e.op, "-") == 0) {
    compile_expr(compiler, code, *e.exp);
    emit_byte(code, OP_NEG);
  } else if (strcmp(e.op, "!") == 0) {
    compile_expr(compiler, code, *e.exp);
    emit_byte(code, OP_NOT);
  } else if (strcmp(e.op, "*") == 0) {
    compile_expr(compiler, code, *e.exp);
    emit_byte(code, OP_DEREF);
  } else if (strcmp(e.op, "&") == 0) {
    switch (e.exp->kind) {
    case EXPR_VAR: {
      ExprVar var = TO_EXPR_VAR(*e.exp);

      /* Try to resolve the variable as local. */
      int idx = resolve_local(compiler, var.name);
      if (idx != -1) {
        emit_byte(code, OP_DEEPGET_PTR);
        emit_uint32(code, idx);
        return;
      }

      /* Try to resolve the variable as global. */
      int name_idx = resolve_global(compiler, code, var.name);
      if (name_idx != -1) {
        emit_byte(code, OP_GET_GLOBAL_PTR);
        emit_uint32(code, name_idx);
        return;
      }

      /* The variable is not defined, bail out. */
      COMPILER_ERROR("Variable '%s' is not defined.", var.name);

      break;
    }
    case EXPR_GET: {
      ExprGet getexp = TO_EXPR_GET(*e.exp);
      /* Compile the part that comes be-
       * fore the member access operator. */
      compile_expr(compiler, code, *getexp.exp);
      /* Deref if the operator is '->'. */
      if (strcmp(getexp.op, "->") == 0) {
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
  } else if (strcmp(e.op, "~") == 0) {
    compile_expr(compiler, code, *e.exp);
    emit_byte(code, OP_BITNOT);
  }
}

static void compile_expr_bin(Compiler *compiler, Bytecode *code, Expr exp) {
  ExprBin e = TO_EXPR_BIN(exp);

  compile_expr(compiler, code, *e.lhs);
  compile_expr(compiler, code, *e.rhs);

  if (strcmp(e.op, "+") == 0) {
    emit_byte(code, OP_ADD);
  } else if (strcmp(e.op, "-") == 0) {
    emit_byte(code, OP_SUB);
  } else if (strcmp(e.op, "*") == 0) {
    emit_byte(code, OP_MUL);
  } else if (strcmp(e.op, "/") == 0) {
    emit_byte(code, OP_DIV);
  } else if (strcmp(e.op, "%%") == 0) {
    emit_byte(code, OP_MOD);
  } else if (strcmp(e.op, "&") == 0) {
    emit_byte(code, OP_BITAND);
  } else if (strcmp(e.op, "|") == 0) {
    emit_byte(code, OP_BITOR);
  } else if (strcmp(e.op, "^") == 0) {
    emit_byte(code, OP_BITXOR);
  } else if (strcmp(e.op, ">") == 0) {
    emit_byte(code, OP_GT);
  } else if (strcmp(e.op, "<") == 0) {
    emit_byte(code, OP_LT);
  } else if (strcmp(e.op, ">=") == 0) {
    emit_bytes(code, 2, OP_LT, OP_NOT);
  } else if (strcmp(e.op, "<=") == 0) {
    emit_bytes(code, 2, OP_GT, OP_NOT);
  } else if (strcmp(e.op, "==") == 0) {
    emit_byte(code, OP_EQ);
  } else if (strcmp(e.op, "!=") == 0) {
    emit_bytes(code, 2, OP_EQ, OP_NOT);
  } else if (strcmp(e.op, "<<") == 0) {
    emit_byte(code, OP_BITSHL);
  } else if (strcmp(e.op, ">>") == 0) {
    emit_byte(code, OP_BITSHR);
  } else if (strcmp(e.op, "++") == 0) {
    emit_byte(code, OP_STRCAT);
  }
}

static void compile_expr_call(Compiler *compiler, Bytecode *code, Expr exp) {
  ExprCall e = TO_EXPR_CALL(exp);

  if (e.callee->kind == EXPR_GET) {
    ExprGet getexp = TO_EXPR_GET(*e.callee);

    /* Compile the part that comes before the member access
     * operator. */
    compile_expr(compiler, code, *getexp.exp);

    /* Deref it if the operator is -> */
    if (strcmp(getexp.op, "->") == 0) {
      emit_byte(code, OP_DEREF);
    }

    char *method = getexp.property_name;

    emit_byte(code, OP_CALL_METHOD);
    emit_uint32(code, add_string(code, method));

  } else if (e.callee->kind == EXPR_VAR) {
    ExprVar var = TO_EXPR_VAR(*e.callee);

    /* Bail out if the function is not defined. */
    Function *func = table_get(compiler->functions, var.name);
    if (func == NULL) {
      COMPILER_ERROR("Function '%s' is not defined.", var.name);
    }

    /* Bail out if the number of arguments does not match the
     * number of function's parameters. */
    if (func->paramcount != e.arguments.count) {
      COMPILER_ERROR("Function '%s' requires %ld arguments.", var.name,
                     func->paramcount);
    }

    /* Then compile the arguments */
    for (size_t i = 0; i < e.arguments.count; i++) {
      compile_expr(compiler, code, e.arguments.data[i]);
    }

    /* Emit OP_CALL followed by the argument count. */
    emit_byte(code, OP_CALL);
    emit_uint32(code, e.arguments.count);

    /* Emit the direct OP_JMP to the function's location. The
     * length of the jump sequence (OP_JMP + two-byte offset,
     * which is 3), needs to be taken into the account, beca-
     * use by the time the VM executes this jump, it will ha-
     * ve read both the jump and the offset, which means that
     * effectively, we'll not be jumping from the current lo-
     * cation, but three slots after it. */
    int16_t jump = -(code->code.count + 3 - func->location);
    emit_bytes(code, 3, OP_JMP, (jump >> 8) & 0xFF, jump & 0xFF);
  }
}

static void compile_expr_get(Compiler *compiler, Bytecode *code, Expr exp) {
  ExprGet e = TO_EXPR_GET(exp);

  /* Compile the part that comes before the member access
   * operator. */
  compile_expr(compiler, code, *e.exp);

  /* Deref it if the operator is -> */
  if (strcmp(e.op, "->") == 0) {
    emit_byte(code, OP_DEREF);
  }

  /* Emit OP_GETATTR with the index of the property name. */
  emit_byte(code, OP_GETATTR);
  emit_uint32(code, add_string(code, e.property_name));
}

static void handle_specop(Bytecode *code, const char *op) {
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

static void compile_assign_var(Compiler *compiler, Bytecode *code, ExprAssign e,
                               bool is_compound) {
  ExprVar var = TO_EXPR_VAR(*e.lhs);

  /* Try to resolve the variable as local. */
  int idx = resolve_local(compiler, var.name);

  bool is_global = false;

  /* If it is not a local, try resolving it as global. */
  if (idx == -1) {
    idx = resolve_global(compiler, code, var.name);
    if (idx != -1)
      is_global = true;
  }

  /* Bail out if it's neither local nor a global. */
  if (idx == -1) {
    COMPILER_ERROR("Variable '%s' is not defined.", var.name);
    return;
  }

  if (is_compound) {
    /* Get the variable onto the top of the stack. */
    emit_byte(code, is_global ? OP_GET_GLOBAL : OP_DEEPGET);
    emit_uint32(code, idx);

    /* Compile the right-hand side. */
    compile_expr(compiler, code, *e.rhs);

    /* Handle the compound assignment. */
    handle_specop(code, e.op);
  } else {
    /* We don't need to get the variable onto the top of
     * the stack, because this is a regular assignment. */
    compile_expr(compiler, code, *e.rhs);
  }

  /* Emit the appropriate assignment opcode. */
  emit_byte(code, is_global ? OP_SET_GLOBAL : OP_DEEPSET);
  emit_uint32(code, idx);
}

static void compile_assign_get(Compiler *compiler, Bytecode *code, ExprAssign e,
                               bool is_compound) {
  ExprGet getexp = TO_EXPR_GET(*e.lhs);

  /* Compile the part that comes before the member access operator. */
  compile_expr(compiler, code, *getexp.exp);

  /* Deref it if the operator is -> */
  if (strcmp(getexp.op, "->") == 0) {
    emit_byte(code, OP_DEREF);
  }

  if (is_compound) {
    /* Get the property onto the top of the stack. */
    emit_byte(code, OP_GETATTR);
    emit_uint32(code, add_string(code, getexp.property_name));

    /* Compile the right-hand side of the assignment. */
    compile_expr(compiler, code, *e.rhs);

    /* Handle the compound assignment. */
    handle_specop(code, e.op);
  } else {
    compile_expr(compiler, code, *e.rhs);
  }

  /* Set the property name to the rhs of the get expr. */
  emit_byte(code, OP_SETATTR);
  emit_uint32(code, add_string(code, getexp.property_name));

  /* Pop the struct off the stack. */
  emit_byte(code, OP_POP);
}

static void compile_assign_una(Compiler *compiler, Bytecode *code, ExprAssign e,
                               bool is_compound) {
  ExprUnary unary = TO_EXPR_UNA(*e.lhs);

  /* Compile the inner expression. */
  compile_expr(compiler, code, *unary.exp);
  if (is_compound) {
    /* Compile the right-hand side of the assignment. */
    compile_expr(compiler, code, *e.rhs);

    /* Handle the compound assignment. */
    handle_specop(code, e.op);
  } else {
    compile_expr(compiler, code, *e.rhs);
  }

  /* Emit OP_DEREFSET. */
  emit_byte(code, OP_DEREFSET);
}

static void compile_expr_ass(Compiler *compiler, Bytecode *code, Expr exp) {
  ExprAssign e = TO_EXPR_ASS(exp);
  bool compound_assign = strcmp(e.op, "=") != 0;

  switch (e.lhs->kind) {
  case EXPR_VAR:
    compile_assign_var(compiler, code, e, compound_assign);
    break;
  case EXPR_GET:
    compile_assign_get(compiler, code, e, compound_assign);
    break;
  case EXPR_UNA:
    compile_assign_una(compiler, code, e, compound_assign);
    break;
  default:
    COMPILER_ERROR("Invalid assignment.");
  }
}

static void compile_expr_log(Compiler *compiler, Bytecode *code, Expr exp) {
  ExprLogic e = TO_EXPR_LOG(exp);
  /* We first compile the left-hand side of the expression. */
  compile_expr(compiler, code, *e.lhs);
  if (strcmp(e.op, "&&") == 0) {
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
    compile_expr(compiler, code, *e.rhs);
    int false_jump = emit_placeholder(code, OP_JMP);
    patch_placeholder(code, end_jump);
    emit_bytes(code, 2, OP_TRUE, OP_NOT);
    patch_placeholder(code, false_jump);
  } else if (strcmp(e.op, "||") == 0) {
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
    compile_expr(compiler, code, *e.rhs);
    patch_placeholder(code, end_jump);
  }
}

static void compile_expr_struct(Compiler *compiler, Bytecode *code, Expr exp) {
  ExprStruct e = TO_EXPR_STRUCT(exp);

  /* Look up the struct with that name in compiler->structs. */
  StructBlueprint *blueprint = table_get(compiler->struct_blueprints, e.name);

  /* If it is not found, bail out. */
  if (!blueprint) {
    COMPILER_ERROR("struct '%s' is not defined.\n", e.name);
  }

  /* If the number of properties in the struct blueprint does
   * not match the number of provided initializers, bail out. */
  if (blueprint->property_indexes->count != e.initializers.count) {
    COMPILER_ERROR("struct '%s' requires %ld initializers.\n", blueprint->name,
                   blueprint->property_indexes->count);
  }

  /* Check if the initializer names match the property names. */
  for (size_t i = 0; i < e.initializers.count; i++) {
    ExprStructInit siexp = e.initializers.data[i].as.expr_s_init;
    char *propname = TO_EXPR_VAR(*siexp.property).name;
    int *propidx = table_get(blueprint->property_indexes, propname);
    if (!propidx) {
      COMPILER_ERROR("struct '%s' has no property '%s'", blueprint->name,
                     propname);
    }
  }

  /* Everything is OK, we emit OP_STRUCT followed by
   * struct's name index in the string pool. */
  emit_byte(code, OP_STRUCT);
  emit_uint32(code, add_string(code, blueprint->name));

  /* Finally, we compile the initializers. */
  for (size_t i = 0; i < e.initializers.count; i++) {
    compile_expr(compiler, code, e.initializers.data[i]);
  }
}

static void compile_expr_s_init(Compiler *compiler, Bytecode *code, Expr exp) {
  ExprStructInit e = TO_EXPR_S_INIT(exp);

  /* First, we compile the value of the initializer,
   * since OP_SETATTR expects it to be on the stack. */
  compile_expr(compiler, code, *e.value);

  ExprVar property = TO_EXPR_VAR(*e.property);

  /* Finally, we emit OP_SETATTR with the property's
   * name index. */
  emit_byte(code, OP_SETATTR);
  emit_uint32(code, add_string(code, property.name));
}

static void compile_expr_array(Compiler *compiler, Bytecode *code, Expr exp) {
  ExprArray e = TO_EXPR_ARRAY(exp);

  /* First, we compile the array elements. */
  for (size_t i = 0; i < e.elements.count; i++) {
    compile_expr(compiler, code, e.elements.data[i]);
  }

  /* Then, we emit OP_ARRAY and the number of elements. */
  emit_byte(code, OP_ARRAY);
  emit_uint32(code, e.elements.count);
}

typedef void (*CompileExprHandlerFn)(Compiler *compiler, Bytecode *code,
                                     Expr exp);

typedef struct {
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
};

static void compile_expr(Compiler *compiler, Bytecode *code, Expr exp) {
  expression_handler[exp.kind].fn(compiler, code, exp);
}

static void compile_stmt_print(Compiler *compiler, Bytecode *code, Stmt stmt) {
  StmtPrint s = TO_STMT_PRINT(stmt);
  compile_expr(compiler, code, s.exp);
  emit_byte(code, OP_PRINT);
}

static void compile_stmt_let(Compiler *compiler, Bytecode *code, Stmt stmt) {
  StmtLet s = TO_STMT_LET(stmt);

  /* Compile the initializer. */
  compile_expr(compiler, code, s.initializer);

  /* Add the variable name to the string pool. */
  uint32_t name_idx = add_string(code, s.name);

  /* If we're in global scope, emit OP_SET_GLOBAL,
   * otherwise, we want the value to remain on the
   * stack, so we will just make the compiler know
   * it is a local variable, and do some bookkeep-
   * ing regarding the number of variables we need
   * to pop off the stack when we do stack cleanup. */
  if (compiler->depth == 0) {
    dynarray_insert(&compiler->globals, s.name);
    emit_byte(code, OP_SET_GLOBAL);
    emit_uint32(code, name_idx);
  } else {
    dynarray_insert(&compiler->locals, code->sp.data[name_idx]);
    compiler->pops[compiler->depth]++;
  }
}

static void compile_stmt_expr(Compiler *compiler, Bytecode *code, Stmt stmt) {
  StmtExpr e = TO_STMT_EXPR(stmt);

  compile_expr(compiler, code, e.exp);

  /* If the expression statement was just a call, like:
   *
   * ...
   * main(4);
   * ...
   *
   * Pop the return value off the stack, so it does not
   * interfere with later execution. */
  if (e.exp.kind == EXPR_CALL) {
    emit_byte(code, OP_POP);
  }
}

static void compile_stmt_block(Compiler *compiler, Bytecode *code, Stmt stmt) {
  begin_scope(compiler);
  StmtBlock s = TO_STMT_BLOCK(&stmt);

  /* Compile the body of the black. */
  for (size_t i = 0; i < s.stmts.count; i++) {
    compile(compiler, code, s.stmts.data[i]);
  }

  emit_stack_cleanup(compiler, code);
  end_scope(compiler);
}

static void compile_stmt_if(Compiler *compiler, Bytecode *code, Stmt stmt) {
  StmtIf s = TO_STMT_IF(stmt);

  /* We first compile the conditional expression because the VM
  .* expects a bool placed on the stack by the time it encount-
   * ers a conditional jump, that is, OP_JZ. */
  compile_expr(compiler, code, s.condition);

  /* Then, we emit OP_JZ, which jumps to the else clause if the
   * condition is falsey. Because we don't know the size of the
   * bytecode in the 'then' branch ahead of time, we do backpa-
   * tching: first, we emit 0xFFFF as the relative jump offset,
   * which serves as a stand-in for the real jump offset, which
   * will be known only after we compile the 'then' branch, and
   * find out its size. */
  int then_jump = emit_placeholder(code, OP_JZ);

  compile(compiler, code, *s.then_branch);

  /* Then, we emit OP_JMP, which jumps over the else branch, in
   * case the then branch was taken. */
  int else_jump = emit_placeholder(code, OP_JMP);

  /* Then, we patch the then jump because now we know its size. */
  patch_placeholder(code, then_jump);

  /* Then, we compile the else branch if it exists. */
  if (s.else_branch != NULL) {
    compile(compiler, code, *s.else_branch);
  }

  /* Finally, we patch the else jump. If the else branch wasn't
   * compiled, the offset should be zeroed out. */
  patch_placeholder(code, else_jump);
}

static void compile_stmt_while(Compiler *compiler, Bytecode *code, Stmt stmt) {
  StmtWhile s = TO_STMT_WHILE(stmt);

  /* We need to mark the beginning of the loop before we compile
   * the conditional expression, so that we know where to return
   * after the body of the loop is executed. */
  int loop_start = code->code.count;
  dynarray_insert(&compiler->loop_starts, loop_start);
  size_t breakcount = compiler->breaks.count;

  /* We then compile the condition because the VM expects a bool
   * placed on the stack by the time it encounters a conditional
   * jump, that is, OP_JZ. */
  compile_expr(compiler, code, s.condition);

  /* We then emit OP_JZ which breaks out of the loop if the con-
   * dition is falsey. Because we don't know the size of the by-
   * tecode in the body of the 'while' loop ahead of time, we do
   * backpatching: first, we emit 0xFFFF as a relative jump off-
   * set which serves as a placeholder for the real jump offset. */
  int exit_jump = emit_placeholder(code, OP_JZ);

  /* Mark the loop depth (needed for break and continue). */
  dynarray_insert(&compiler->loop_depths, compiler->depth);

  /* Then, we compile the body of the loop. */
  compile(compiler, code, *s.body);

  /* Pop the loop depth as it's no longer needed. */
  dynarray_pop(&compiler->loop_depths);

  /* Then, we emit OP_JMP with a negative offset which jumps just
   * before the condition, so that we could evaluate it again and
   * see if we need to continue looping. */
  emit_loop(code, loop_start);

  /* Now we can also patch any 'break' statements that might have
   * occurred in the loop body, because now we know where the br-
   * eaks should be jumping to. */
  int to_pop = compiler->breaks.count - breakcount;
  for (int i = 0; i < to_pop; i++) {
    int break_jump = dynarray_pop(&compiler->breaks);
    patch_placeholder(code, break_jump);
  }

  /* Pop the loop start. */
  dynarray_pop(&compiler->loop_starts);

  /* Finally, we patch the exit jump. */
  patch_placeholder(code, exit_jump);

  if (compiler->depth == 0) {
    assert(compiler->breaks.count == 0);
    assert(compiler->loop_starts.count == 0);
  }
}

static void compile_stmt_for(Compiler *compiler, Bytecode *code, Stmt stmt) {
  StmtFor s = TO_STMT_FOR(stmt);

  ExprAssign assignment = TO_EXPR_ASS(s.initializer);
  ExprVar variable = TO_EXPR_VAR(*assignment.lhs);

  /* Insert the initializer variable name into the compiler->locals
   * dynarray, since the condition that follows the initializer ex-
   * pects it to be there. */
  dynarray_insert(&compiler->locals, variable.name);

  /* Compile the right-hand side of the initializer first. */
  compile_expr(compiler, code, *assignment.rhs);

  /* Mark the beginning of the loop before compiling the condition,
   * so that we know where to jump after the loop body is executed. */
  int loop_start = code->code.count;
  dynarray_insert(&compiler->loop_starts, loop_start);
  size_t breakcount = compiler->breaks.count;

  /* Compile the conditional expression. */
  compile_expr(compiler, code, s.condition);

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
  compile_expr(compiler, code, s.advancement);

  /* After the loop body is executed, we jump to the advancement and
   * execute it. This means we need to evaluate the condition again,
   * so we're emitting a backward unconditional jump, which jumps to
   * just before the condition. */
  emit_loop(code, loop_start);

  /* Patch the jump now that we know the size of the advancement. */
  patch_placeholder(code, jump_over_advancement);

  /* Patch the loop_start we inserted to point to loop_continuation.
   * This is the place just before the advancement. */
  compiler->loop_starts.data[compiler->loop_starts.count - 1] =
      loop_continuation;

  /* Mark the loop depth (needed for break and continue). */
  dynarray_insert(&compiler->loop_depths, compiler->depth);

  /* Compile the loop body. */
  compile(compiler, code, *s.body);

  /* Pop the loop depth as it's no longer needed. */
  dynarray_pop(&compiler->loop_depths);

  /* Emit backward jump back to the advancement. */
  emit_loop(code, loop_continuation);

  /* Now we can also patch any 'break' statements that might have
   * occurred in the loop body, because now we know where the br-
   * eaks should be jumping to. */
  int to_pop = compiler->breaks.count - breakcount;
  for (int i = 0; i < to_pop; i++) {
    int break_jump = dynarray_pop(&compiler->breaks);
    patch_placeholder(code, break_jump);
  }

  /* Pop the initializer from compiler->locals. */
  dynarray_pop(&compiler->locals);

  /* Pop the loop_start. */
  dynarray_pop(&compiler->loop_starts);

  /* Finally, we patch the exit jump. */
  patch_placeholder(code, exit_jump);

  /* Pop the initializer from the stack. */
  emit_byte(code, OP_POP);

  if (compiler->depth == 0) {
    assert(compiler->breaks.count == 0);
    assert(compiler->loop_starts.count == 0);
  }
}

static void compile_stmt_fn(Compiler *compiler, Bytecode *code, Stmt stmt) {
  StmtFn s = TO_STMT_FN(stmt);

  Function func = {
      .name = s.name,
      .paramcount = s.parameters.count,
      .location = code->code.count + 3,
  };

  if (compiler->depth == 0) {
    table_insert(compiler->functions, func.name, func);
  }

  compiler->pops[1] += s.parameters.count;

  /* Copy the function parameters into the compiler->locals. */
  COPY_DYNARRAY(&compiler->locals, &s.parameters);

  /* Emit the jump because we don't want to execute the code
   * the first time we encounter it. */
  int jump = emit_placeholder(code, OP_JMP);

  /* Compile the function body. */
  compile(compiler, code, *s.body);

  /* Finally, patch the jump. */
  patch_placeholder(code, jump);

  assert(compiler->breaks.count == 0);
  assert(compiler->loop_starts.count == 0);
  assert(compiler->locals.count == 0);
  assert(compiler->pops[1] == 0);
}

static void compile_stmt_struct(Compiler *compiler, Bytecode *code, Stmt stmt) {
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

  for (size_t i = 0; i < s.properties.count; i++) {
    emit_uint32(code, add_string(code, s.properties.data[i]));
    table_insert(blueprint.property_indexes, s.properties.data[i], i);
    emit_uint32(code, i);
  }

  /* Let the compiler know about the blueprint. */
  table_insert(compiler->struct_blueprints, blueprint.name, blueprint);
}

static void compile_stmt_impl(Compiler *compiler, Bytecode *code, Stmt stmt) {
  StmtImpl s = TO_STMT_IMPL(stmt);

  /* Look up the struct with that name in compiler->structs. */
  StructBlueprint *blueprint = table_get(compiler->struct_blueprints, s.name);

  /* If it is not found, bail out. */
  if (!blueprint) {
    COMPILER_ERROR("struct '%s' is not defined.\n", s.name);
  }

  for (size_t i = 0; i < s.methods.count; i++) {
    StmtFn func = TO_STMT_FN(s.methods.data[i]);
    Function f = {
        .name = func.name,
        .paramcount = func.parameters.count,
        .location = code->code.count + 3,
    };
    table_insert(blueprint->methods, func.name, f);
    compile(compiler, code, s.methods.data[i]);
  }

  emit_byte(code, OP_IMPL);
  emit_uint32(code, add_string(code, blueprint->name));
  emit_uint32(code, s.methods.count);

  for (size_t i = 0; i < s.methods.count; i++) {
    StmtFn func = TO_STMT_FN(s.methods.data[i]);
    Function *f = table_get(blueprint->methods, func.name);
    emit_uint32(code, add_string(code, f->name));
    emit_uint32(code, f->paramcount);
    emit_uint32(code, f->location);
  }
}

static void compile_stmt_return(Compiler *compiler, Bytecode *code, Stmt stmt) {
  StmtRet s = TO_STMT_RETURN(stmt);

  /* Compile the return value. */
  compile_expr(compiler, code, s.returnval);

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
  int deepset_no = compiler->locals.count - 1;
  for (size_t i = 0; i < compiler->locals.count; i++) {
    emit_byte(code, OP_DEEPSET);
    emit_uint32(code, deepset_no--);
  }

  /* Finally, emit OP_RET. */
  emit_byte(code, OP_RET);
}

static void compile_stmt_break(Compiler *compiler, Bytecode *code, Stmt stmt) {
  /* 'compiler->loop_starts' will contain at least one loop_start
   * if this code runs as part of compiling one of the loop stat-
   * ements. */
  if (compiler->loop_starts.count > 0) {
    /* First, clean up the stack. */
    emit_loop_cleanup(compiler, code);

    /* Then, emit the OP_JMP placeholder because we don't know yet
     * where in the bytecode we want to jump (we'll know that when
     * we compile the loop body and find out where the loop ends). */
    int break_jump = emit_placeholder(code, OP_JMP);
    dynarray_insert(&compiler->breaks, break_jump);
  } else {
    COMPILER_ERROR("'break' outside of loop.");
  }
}

static void compile_stmt_continue(Compiler *compiler, Bytecode *code,
                                  Stmt stmt) {
  /* 'compiler->loop_starts' will contain at least one loop_start
   * if this code runs as part of compiling one of the loop stat-
   * ements. */
  if (compiler->loop_starts.count > 0) {
    /* Use the most recent loop_start to emit a backwards jump to
     * the beginning of the most recent loop. */
    int loop_start = dynarray_peek(&compiler->loop_starts);

    /* Clean up the stack before the next iteration. */
    emit_loop_cleanup(compiler, code);

    /* Jump back to the beginning of the loop and evalute the co-
     * ndition one more time. */
    emit_loop(code, loop_start);
  } else {
    COMPILER_ERROR("'continue' outside of loop.");
  }
}

typedef void (*CompileHandlerFn)(Compiler *compiler, Bytecode *code, Stmt stmt);

typedef struct {
  CompileHandlerFn fn;
  char *name;
} CompileHandler;

static CompileHandler handler[] = {
    [STMT_PRINT] = {.fn = compile_stmt_print, .name = "STMT_PRINT"},
    [STMT_LET] = {.fn = compile_stmt_let, .name = "STMT_LET"},
    [STMT_EXPR] = {.fn = compile_stmt_expr, .name = "STMT_EXPR"},
    [STMT_BLOCK] = {.fn = compile_stmt_block, .name = "STMT_BLOCK"},
    [STMT_IF] = {.fn = compile_stmt_if, .name = "STMT_IF"},
    [STMT_WHILE] = {.fn = compile_stmt_while, .name = "STMT_WHILE"},
    [STMT_FOR] = {.fn = compile_stmt_for, .name = "STMT_FOR"},
    [STMT_FN] = {.fn = compile_stmt_fn, .name = "STMT_FN"},
    [STMT_STRUCT] = {.fn = compile_stmt_struct, .name = "STMT_STRUCT"},
    [STMT_IMPL] = {.fn = compile_stmt_impl, .name = "STMT_IMPL"},
    [STMT_RETURN] = {.fn = compile_stmt_return, .name = "STMT_RETURN"},
    [STMT_BREAK] = {.fn = compile_stmt_break, .name = "STMT_BREAK"},
    [STMT_CONTINUE] = {.fn = compile_stmt_continue, .name = "STMT_CONTINUE"},
};

void compile(Compiler *compiler, Bytecode *code, Stmt stmt) {
  handler[stmt.kind].fn(compiler, code, stmt);
}
