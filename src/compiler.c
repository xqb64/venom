#include "compiler.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "dynarray.h"
#include "table.h"
#include "util.h"

#define COMPILER_ERROR(...)                  \
  do {                                       \
    alloc_err_str(&result.msg, __VA_ARGS__); \
    Compiler *c;                             \
    while (current_compiler) {               \
      c = current_compiler;                  \
      current_compiler = c->next;            \
      free_compiler(c);                      \
      free(c);                               \
    }                                        \
    result.is_ok = false;                    \
    result.errcode = -1;                     \
    return result;                           \
  } while (0)

typedef struct {
  char *name;
  size_t argcount;
} Builtin;

static Builtin builtins[] = {
    {"next", 1}, {"len", 1}, {"hasattr", 2}, {"getattr", 2}, {"setattr", 3},
};

Compiler *current_compiler = NULL;

static void free_table_int(Table_int *table)
{
  for (size_t i = 0; i < TABLE_MAX; i++) {
    if (table->indexes[i] != NULL) {
      Bucket *bucket = table->indexes[i];
      list_free(bucket);
    }
  }
}

static void free_table_function_ptr(Table_FunctionPtr *table)
{
  for (size_t i = 0; i < TABLE_MAX; i++) {
    if (table->indexes[i] != NULL) {
      Bucket *bucket = table->indexes[i];
      list_free(bucket);
    }
  }

  for (size_t i = 0; i < table->count; i++) {
    free(table->items[i]);
  }
}

void free_table_struct_blueprints(Table_StructBlueprint *table)
{
  for (size_t i = 0; i < TABLE_MAX; i++) {
    if (table->indexes[i] != NULL) {
      Bucket *bucket = table->indexes[i];
      list_free(bucket);
    }
  }

  for (size_t i = 0; i < table->count; i++) {
    free_table_int(table->items[i].property_indexes);
    free(table->items[i].property_indexes);

    free_table_function_ptr(table->items[i].methods);
    free(table->items[i].methods);
  }
}

static void free_table_functions(Table_Function *table)
{
  for (size_t i = 0; i < TABLE_MAX; i++) {
    if (table->indexes[i] != NULL) {
      Bucket *bucket = table->indexes[i];
      list_free(bucket);
    }
  }
}

static void free_table_labels(Table_Label *table)
{
  for (size_t i = 0; i < TABLE_MAX; i++) {
    if (table->indexes[i] != NULL) {
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

  for (size_t i = 0; i < code->sp.count; i++) {
    free(code->sp.data[i]);
  }
  dynarray_free(&code->sp);
}

/* Check if the string is already present in the sp.
 * If not, add it first, and finally return the idx. */
static uint32_t add_string(Bytecode *code, const char *string)
{
  for (size_t idx = 0; idx < code->sp.count; idx++) {
    if (strcmp(code->sp.data[idx], string) == 0) {
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
  for (int i = 0; i < n; i++) {
    uint8_t byte = va_arg(ap, int);
    emit_byte(code, byte);
  }
  va_end(ap);
}

static void emit_uint32(Bytecode *code, uint32_t idx)
{
  emit_bytes(code, 4, (idx >> 24) & 0xFF, (idx >> 16) & 0xFF, (idx >> 8) & 0xFF,
             idx & 0xFF);
}

static void emit_double(Bytecode *code, double x)
{
  union {
    double d;
    uint64_t raw;
  } num;

  num.d = x;

  emit_bytes(
      code, 8, (uint8_t) ((num.raw >> 56) & 0xFF),
      (uint8_t) ((num.raw >> 48) & 0xFF), (uint8_t) ((num.raw >> 40) & 0xFF),
      (uint8_t) ((num.raw >> 32) & 0xFF), (uint8_t) ((num.raw >> 24) & 0xFF),
      (uint8_t) ((num.raw >> 16) & 0xFF), (uint8_t) ((num.raw >> 8) & 0xFF),
      (uint8_t) (num.raw & 0xFF));
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
  for (size_t i = 0; i < upvalues->count; i++) {
    if (upvalues->data[i] == idx) {
      return;
    }
  }
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
         current_compiler->locals[current_compiler->locals_count - 1].depth >
             loop_depth) {
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

  while (c->locals_count > 0 &&
         c->locals[c->locals_count - 1].depth > c->depth) {
    emit_byte(code, OP_POP);
    c->locals_count--;
  }
}

static void patch_jumps(Bytecode *code)
{
  for (size_t i = 0; i < TABLE_MAX; i++) {
    if (current_compiler->labels->indexes[i]) {
      Label *l = table_get(current_compiler->labels,
                           current_compiler->labels->indexes[i]->key);

      int location = l->location;
      int patch_with = l->patch_with;

      if (patch_with == -1) {
        continue;
      }

      code->code.data[location + 1] = ((patch_with - location - 3) >> 8) & 0xFF;
      code->code.data[location + 2] = ((patch_with) - (location) -3) & 0xFF;
    }
  }
}

/* Check if 'name' is present in the builtins table.
 * If it is, return its index in the sp, otherwise -1. */
static Function *resolve_builtin(const char *name)
{
  Compiler *current = current_compiler;

  while (current) {
    Function **f = table_get(current->builtins, name);
    if (f) {
      return *f;
    }
    current = current->next;
  }

  return NULL;
}

/* Check if 'name' is present in the globals dynarray.
 * If it is, return its index in the sp, otherwise -1. */
static int resolve_global(Bytecode *code, const char *name)
{
  Compiler *current = current_compiler;

  while (current) {
    for (size_t idx = 0; idx < current->globals_count; idx++) {
      if (strcmp(current->globals[idx].name, name) == 0) {
        return add_string(code, name);
      }
    }
    current = current->next;
  }

  return -1;
}

/* Check if 'name' is present in the locals dynarray.
 * If it is, return the index, otherwise return -1. */
static int resolve_local(const char *name)
{
  for (size_t idx = 0; idx < current_compiler->locals_count; idx++) {
    if (strcmp(current_compiler->locals[idx].name, name) == 0) {
      return idx;
    }
  }
  return -1;
}

static int resolve_upvalue(const char *name)
{
  Compiler *current = current_compiler->next;

  while (current) {
    for (size_t idx = 0; idx < current->locals_count; idx++) {
      if (strcmp(current->locals[idx].name, name) == 0) {
        current->locals[idx].captured = true;
        return idx;
      }
    }
    current = current->next;
  }

  return -1;
}

static StructBlueprint *resolve_blueprint(const char *name)
{
  Compiler *current = current_compiler;

  while (current) {
    StructBlueprint *bp = table_get(current->struct_blueprints, name);
    if (bp) {
      return bp;
    }
    current = current->next;
  }

  return NULL;
}

static Function *resolve_func(const char *name)
{
  Compiler *current = current_compiler;

  while (current) {
    Function *f = table_get(current->functions, name);
    if (f) {
      return f;
    }
    current = current->next;
  }

  return NULL;
}

#define COMPILE_EXPR(code, exp)       \
  result = compile_expr(code, (exp)); \
  if (!result.is_ok)                  \
    return result;

#define COMPILE_STMT(code, stmt)       \
  result = compile_stmt(code, (stmt)); \
  if (!result.is_ok)                   \
    return result;

static CompileResult compile_expr(Bytecode *code, const Expr *expr);

static CompileResult compile_expr_lit(Bytecode *code, const Expr *expr)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  ExprLiteral expr_lit = expr->as.expr_literal;

  switch (expr_lit.kind) {
    case LIT_BOOLEAN: {
      emit_byte(code, OP_TRUE);
      if (!expr_lit.as._bool) {
        emit_byte(code, OP_NOT);
      }
      break;
    }
    case LIT_NUMBER: {
      emit_byte(code, OP_CONST);
      emit_double(code, expr_lit.as._double);
      break;
    }
    case LIT_STRING: {
      uint32_t str_idx = add_string(code, expr_lit.as.str);
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

static CompileResult compile_expr_var(Bytecode *code, const Expr *expr)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  ExprVariable expr_var = expr->as.expr_variable;

  /* Try to resolve the variable as local. */
  int idx = resolve_local(expr_var.name);
  if (idx != -1) {
    emit_byte(code, OP_DEEPGET);
    emit_uint32(code, idx);
    return result;
  }

  /* Try to resolve the variable as upvalue. */
  int upvalue_idx = resolve_upvalue(expr_var.name);
  if (upvalue_idx != -1) {
    emit_byte(code, OP_GET_UPVALUE);
    emit_uint32(code, upvalue_idx);
    add_upvalue(&current_compiler->upvalues, upvalue_idx);
    return result;
  }

  /* Try to resolve the variable as global. */
  int name_idx = resolve_global(code, expr_var.name);
  if (name_idx != -1) {
    emit_byte(code, OP_GET_GLOBAL);
    emit_uint32(code, name_idx);
    return result;
  }

  /* The variable is not defined, bail out. */
  COMPILER_ERROR("Variable '%s' is not defined.", expr_var.name);
}

static CompileResult compile_expr_una(Bytecode *code, const Expr *expr)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  ExprUnary expr_unary = expr->as.expr_unary;

  if (strcmp(expr_unary.op, "-") == 0) {
    COMPILE_EXPR(code, expr_unary.expr);
    emit_byte(code, OP_NEG);
  } else if (strcmp(expr_unary.op, "!") == 0) {
    COMPILE_EXPR(code, expr_unary.expr);
    emit_byte(code, OP_NOT);
  } else if (strcmp(expr_unary.op, "*") == 0) {
    COMPILE_EXPR(code, expr_unary.expr);
    emit_byte(code, OP_DEREF);
  } else if (strcmp(expr_unary.op, "&") == 0) {
    switch (expr_unary.expr->kind) {
      case EXPR_VARIABLE: {
        ExprVariable var = expr_unary.expr->as.expr_variable;

        /* Try to resolve the variable as local. */
        int idx = resolve_local(var.name);
        if (idx != -1) {
          emit_byte(code, OP_DEEPGET_PTR);
          emit_uint32(code, idx);
          return result;
        }

        /* Try to resolve the variable as upvalue. */
        int upvalue_idx = resolve_upvalue(var.name);
        if (upvalue_idx != -1) {
          emit_byte(code, OP_GET_UPVALUE_PTR);
          emit_uint32(code, upvalue_idx);
          return result;
        }

        int name_idx = resolve_global(code, var.name);
        if (name_idx != -1) {
          emit_byte(code, OP_GET_GLOBAL_PTR);
          emit_uint32(code, name_idx);
          return result;
        }

        /* The variable is not defined, bail out. */
        COMPILER_ERROR("Variable '%s' is not defined.", var.name);

        break;
      }
      case EXPR_GET: {
        ExprGet expr_get = expr_unary.expr->as.expr_get;
        /* Compile the part that comes be-
         * fore the member access operator. */
        COMPILE_EXPR(code, expr_get.expr);
        /* Deref if the operator is '->'. */
        if (strcmp(expr_get.op, "->") == 0) {
          emit_byte(code, OP_DEREF);
        }
        /* Add the 'property_name' string to the
         * chunk's sp, and emit OP_GETATTR_PTR. */
        uint32_t property_name_idx = add_string(code, expr_get.property_name);
        emit_byte(code, OP_GETATTR_PTR);
        emit_uint32(code, property_name_idx);
        break;
      }
      default:
        break;
    }
  } else if (strcmp(expr_unary.op, "~") == 0) {
    COMPILE_EXPR(code, expr_unary.expr);
    emit_byte(code, OP_BITNOT);
  }

  return result;
}

static CompileResult compile_expr_bin(Bytecode *code, const Expr *expr)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  ExprBinary expr_bin = expr->as.expr_binary;

  COMPILE_EXPR(code, expr_bin.lhs);

  if (strcmp(expr_bin.op, "&&") == 0) {
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
    COMPILE_EXPR(code, expr_bin.rhs);
    int false_jump = emit_placeholder(code, OP_JMP);
    patch_placeholder(code, end_jump);
    emit_bytes(code, 2, OP_TRUE, OP_NOT);
    patch_placeholder(code, false_jump);

    return result;
  } else if (strcmp(expr_bin.op, "||") == 0) {
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
    COMPILE_EXPR(code, expr_bin.rhs);
    patch_placeholder(code, end_jump);

    return result;
  }

  COMPILE_EXPR(code, expr_bin.rhs);

  if (strcmp(expr_bin.op, "+") == 0) {
    emit_byte(code, OP_ADD);
  } else if (strcmp(expr_bin.op, "-") == 0) {
    emit_byte(code, OP_SUB);
  } else if (strcmp(expr_bin.op, "*") == 0) {
    emit_byte(code, OP_MUL);
  } else if (strcmp(expr_bin.op, "/") == 0) {
    emit_byte(code, OP_DIV);
  } else if (strcmp(expr_bin.op, "%") == 0) {
    emit_byte(code, OP_MOD);
  } else if (strcmp(expr_bin.op, "&") == 0) {
    emit_byte(code, OP_BITAND);
  } else if (strcmp(expr_bin.op, "|") == 0) {
    emit_byte(code, OP_BITOR);
  } else if (strcmp(expr_bin.op, "^") == 0) {
    emit_byte(code, OP_BITXOR);
  } else if (strcmp(expr_bin.op, ">") == 0) {
    emit_byte(code, OP_GT);
  } else if (strcmp(expr_bin.op, "<") == 0) {
    emit_byte(code, OP_LT);
  } else if (strcmp(expr_bin.op, ">=") == 0) {
    emit_bytes(code, 2, OP_LT, OP_NOT);
  } else if (strcmp(expr_bin.op, "<=") == 0) {
    emit_bytes(code, 2, OP_GT, OP_NOT);
  } else if (strcmp(expr_bin.op, "==") == 0) {
    emit_byte(code, OP_EQ);
  } else if (strcmp(expr_bin.op, "!=") == 0) {
    emit_bytes(code, 2, OP_EQ, OP_NOT);
  } else if (strcmp(expr_bin.op, "<<") == 0) {
    emit_byte(code, OP_BITSHL);
  } else if (strcmp(expr_bin.op, ">>") == 0) {
    emit_byte(code, OP_BITSHR);
  } else if (strcmp(expr_bin.op, "++") == 0) {
    emit_byte(code, OP_STRCAT);
  }

  return result;
}

static CompileResult compile_expr_call(Bytecode *code, const Expr *expr)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  ExprCall expr_call = expr->as.expr_call;

  if (expr_call.callee->kind == EXPR_GET) {
    ExprGet expr_get = expr_call.callee->as.expr_get;

    /* Compile the part that comes before the member access
     * operator. */
    COMPILE_EXPR(code, expr_get.expr);

    /* Deref it if the operator is -> */
    if (strcmp(expr_get.op, "->") == 0) {
      emit_byte(code, OP_DEREF);
    }

    char *method = expr_get.property_name;

    for (size_t i = 0; i < expr_call.arguments.count; i++) {
      COMPILE_EXPR(code, &expr_call.arguments.data[i]);
    }

    emit_byte(code, OP_CALL_METHOD);
    emit_uint32(code, add_string(code, method));
    emit_uint32(code, expr_call.arguments.count);
  } else if (expr_call.callee->kind == EXPR_VARIABLE) {
    ExprVariable var = expr_call.callee->as.expr_variable;

    Function *b = resolve_builtin(var.name);
    if (b) {
      if (strcmp(b->name, "next") == 0) {
        for (size_t i = 0; i < expr_call.arguments.count; i++) {
          COMPILE_EXPR(code, &expr_call.arguments.data[i]);
        }
        emit_byte(code, OP_RESUME);
      } else if (strcmp(b->name, "len") == 0) {
        for (size_t i = 0; i < expr_call.arguments.count; i++) {
          COMPILE_EXPR(code, &expr_call.arguments.data[i]);
        }
        emit_byte(code, OP_LEN);
      } else if (strcmp(b->name, "hasattr") == 0) {
        for (size_t i = 0; i < expr_call.arguments.count; i++) {
          COMPILE_EXPR(code, &expr_call.arguments.data[i]);
        }
        emit_byte(code, OP_HASATTR);
      } else if (strcmp(b->name, "getattr") == 0) {
        COMPILE_EXPR(code, &expr_call.arguments.data[0]);
        emit_bytes(code, 1, OP_GETATTR);
        emit_uint32(
            code,
            add_string(code,
                       expr_call.arguments.data[1].as.expr_literal.as.str));
      } else if (strcmp(b->name, "setattr") == 0) {
        COMPILE_EXPR(code, &expr_call.arguments.data[0]);
        COMPILE_EXPR(code, &expr_call.arguments.data[2]);

        emit_byte(code, OP_SETATTR);
        emit_uint32(
            code,
            add_string(code,
                       expr_call.arguments.data[1].as.expr_literal.as.str));
      }

      return result;
    }

    Function *f = resolve_func(var.name);
    if (f && f->paramcount != expr_call.arguments.count) {
      COMPILER_ERROR("Function '%s' requires %ld arguments.", f->name,
                     f->paramcount);
    }

    /* Then compile the arguments */
    for (size_t i = 0; i < expr_call.arguments.count; i++) {
      COMPILE_EXPR(code, &expr_call.arguments.data[i]);
    }

    bool is_global = false;
    bool is_upvalue = false;

    /* Try to resolve the variable as local. */
    int idx = resolve_local(var.name);

    /* If it is not a local, try resolving it as upvalue. */
    if (idx == -1) {
      idx = resolve_upvalue(var.name);
      if (idx != -1) {
        is_upvalue = true;
      }
    }

    /* If it is not an upvalue, try resolving it as global. */
    if (idx == -1) {
      idx = resolve_global(code, var.name);
      if (idx != -1) {
        is_global = true;
      }
    }

    /* Bail out if it's neither local nor a global. */
    if (idx == -1) {
      COMPILER_ERROR("Function '%s' is not defined.", var.name);
    }

    if (is_global) {
      emit_byte(code, OP_GET_GLOBAL);
      emit_uint32(code, idx);
    } else if (is_upvalue) {
      emit_byte(code, OP_GET_UPVALUE);
      emit_uint32(code, idx);
      add_upvalue(&current_compiler->upvalues, idx);
    } else {
      emit_byte(code, OP_DEEPGET);
      emit_uint32(code, idx);
    }

    if (f && f->is_gen) {
      emit_byte(code, OP_MKGEN);
    } else {
      /* Emit OP_CALL followed by the argument count. */
      emit_bytes(code, 2, OP_CALL, expr_call.arguments.count);
    }
  }

  return result;
}

static CompileResult compile_expr_get(Bytecode *code, const Expr *expr)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  ExprGet expr_get = expr->as.expr_get;

  /* Compile the part that comes before the member access
   * operator. */
  COMPILE_EXPR(code, expr_get.expr);

  /* Deref it if the operator is -> */
  if (strcmp(expr_get.op, "->") == 0) {
    emit_byte(code, OP_DEREF);
  }

  /* Emit OP_GETATTR with the index of the property name. */
  emit_byte(code, OP_GETATTR);
  emit_uint32(code, add_string(code, expr_get.property_name));

  return result;
}

static void handle_specop(Bytecode *code, const char *op)
{
  if (strcmp(op, "+=") == 0) {
    emit_byte(code, OP_ADD);
  } else if (strcmp(op, "-=") == 0) {
    emit_byte(code, OP_SUB);
  } else if (strcmp(op, "*=") == 0) {
    emit_byte(code, OP_MUL);
  } else if (strcmp(op, "/=") == 0) {
    emit_byte(code, OP_DIV);
  } else if (strcmp(op, "%=") == 0) {
    emit_byte(code, OP_MOD);
  } else if (strcmp(op, "&=") == 0) {
    emit_byte(code, OP_BITAND);
  } else if (strcmp(op, "|=") == 0) {
    emit_byte(code, OP_BITOR);
  } else if (strcmp(op, "^=") == 0) {
    emit_byte(code, OP_BITXOR);
  } else if (strcmp(op, ">>=") == 0) {
    emit_byte(code, OP_BITSHR);
  } else if (strcmp(op, "<<=") == 0) {
    emit_byte(code, OP_BITSHL);
  }
}

static CompileResult compile_assign_var(Bytecode *code, ExprAssign e,
                                        bool is_compound)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  ExprVariable expr_var = e.lhs->as.expr_variable;

  bool is_global = false;
  bool is_upvalue = false;

  /* Try to resolve the variable as local. */
  int idx = resolve_local(expr_var.name);

  /* If it is not an upvalue, try resolving it as global. */
  if (idx == -1) {
    idx = resolve_upvalue(expr_var.name);
    if (idx != -1) {
      is_upvalue = true;
    }
  }

  /* If it is not an upvalue, try resolving it as global. */
  if (idx == -1) {
    idx = resolve_global(code, expr_var.name);
    if (idx != -1) {
      is_global = true;
    }
  }

  /* Bail out if it's neither local nor a global. */
  if (idx == -1) {
    COMPILER_ERROR("Variable '%s' is not defined.", expr_var.name);
  }

  if (is_compound) {
    /* Get the variable onto the top of the stack. */
    if (is_global) {
      emit_byte(code, OP_GET_GLOBAL);
    } else if (is_upvalue) {
      emit_byte(code, OP_GET_UPVALUE);
    } else {
      emit_byte(code, OP_DEEPGET);
    }

    emit_uint32(code, idx);

    /* Compile the right-hand side. */
    COMPILE_EXPR(code, e.rhs);

    /* Handle the compound assignment. */
    handle_specop(code, e.op);
  } else {
    /* We don't need to get the variable onto the top of
     * the stack, because this is a regular assignment. */
    COMPILE_EXPR(code, e.rhs);
  }

  /* Emit the appropriate assignment opcode. */
  if (is_global) {
    emit_byte(code, OP_SET_GLOBAL);
  } else if (is_upvalue) {
    emit_byte(code, OP_SET_UPVALUE);
  } else {
    emit_byte(code, OP_DEEPSET);
  }

  emit_uint32(code, idx);

  return result;
}

static CompileResult compile_assign_get(Bytecode *code, ExprAssign e,
                                        bool is_compound)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  ExprGet expr_get = e.lhs->as.expr_get;

  /* Compile the part that comes before the member access operator. */
  COMPILE_EXPR(code, expr_get.expr);

  /* Deref it if the operator is -> */
  if (strcmp(expr_get.op, "->") == 0) {
    emit_byte(code, OP_DEREF);
  }

  if (is_compound) {
    /* Get the property onto the top of the stack. */
    emit_byte(code, OP_GETATTR);
    emit_uint32(code, add_string(code, expr_get.property_name));

    /* Compile the right-hand side of the assignment. */
    COMPILE_EXPR(code, e.rhs);

    /* Handle the compound assignment. */
    handle_specop(code, e.op);
  } else {
    COMPILE_EXPR(code, e.rhs);
  }

  /* Set the property name to the rhs of the get expr. */
  emit_byte(code, OP_SETATTR);
  emit_uint32(code, add_string(code, expr_get.property_name));

  /* Pop the struct off the stack. */
  emit_byte(code, OP_POP);

  return result;
}

static CompileResult compile_assign_una(Bytecode *code, ExprAssign e,
                                        bool is_compound)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  ExprUnary expr_unary = e.lhs->as.expr_unary;

  /* Compile the inner expression. */
  COMPILE_EXPR(code, expr_unary.expr);
  if (is_compound) {
    /* Compile the right-hand side of the assignment. */
    COMPILE_EXPR(code, e.rhs);

    /* Handle the compound assignment. */
    handle_specop(code, e.op);
  } else {
    COMPILE_EXPR(code, e.rhs);
  }

  /* Emit OP_DEREFSET. */
  emit_byte(code, OP_DEREFSET);

  return result;
}

static CompileResult compile_assign_sub(Bytecode *code, ExprAssign e,
                                        bool is_compound)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  ExprSubscript expr_subscript = e.lhs->as.expr_subscript;

  /* Compile the subscriptee. */
  COMPILE_EXPR(code, expr_subscript.expr);

  /* Compile the index. */
  COMPILE_EXPR(code, expr_subscript.index);

  if (is_compound) {
    COMPILE_EXPR(code, e.lhs);
    COMPILE_EXPR(code, e.rhs);
    handle_specop(code, e.op);
  } else {
    COMPILE_EXPR(code, e.rhs);
  }

  emit_byte(code, OP_ARRAYSET);

  return result;
}

static CompileResult compile_expr_ass(Bytecode *code, const Expr *expr)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  ExprAssign expr_assign = expr->as.expr_assign;
  bool compound_assign = strcmp(expr_assign.op, "=") != 0;

  switch (expr_assign.lhs->kind) {
    case EXPR_VARIABLE:
      compile_assign_var(code, expr_assign, compound_assign);
      break;
    case EXPR_GET:
      compile_assign_get(code, expr_assign, compound_assign);
      break;
    case EXPR_UNARY:
      compile_assign_una(code, expr_assign, compound_assign);
      break;
    case EXPR_SUBSCRIPT:
      compile_assign_sub(code, expr_assign, compound_assign);
      break;
    default:
      COMPILER_ERROR("Invalid assignment.");
  }

  return result;
}

static CompileResult compile_expr_struct(Bytecode *code, const Expr *expr)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  ExprStruct expr_struct = expr->as.expr_struct;

  /* Look up the struct with that name in current_compiler->structs. */
  StructBlueprint *blueprint = resolve_blueprint(expr_struct.name);

  /* If it is not found, bail out. */
  if (!blueprint) {
    COMPILER_ERROR("struct '%s' is not defined.\n", expr_struct.name);
  }

  /* If the number of properties in the struct blueprint does
   * not match the number of provided initializers, bail out. */
  if (blueprint->property_indexes->count != expr_struct.initializers.count) {
    COMPILER_ERROR("struct '%s' requires %ld initializers.\n", blueprint->name,
                   blueprint->property_indexes->count);
  }

  /* Check if the initializer names match the property names. */
  for (size_t i = 0; i < expr_struct.initializers.count; i++) {
    ExprStructInitializer siexp =
        expr_struct.initializers.data[i].as.expr_struct_initializer;
    char *propname = siexp.property->as.expr_variable.name;

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
  for (size_t i = 0; i < expr_struct.initializers.count; i++) {
    COMPILE_EXPR(code, &expr_struct.initializers.data[i]);
  }

  return result;
}

static CompileResult compile_expr_struct_initializer(Bytecode *code,
                                                     const Expr *expr)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  ExprStructInitializer expr_struct_initializer =
      expr->as.expr_struct_initializer;

  /* First, we compile the value of the initializer,
   * since OP_SETATTR expects it to be on the stack. */
  COMPILE_EXPR(code, expr_struct_initializer.value);

  ExprVariable property = expr_struct_initializer.property->as.expr_variable;

  /* Finally, we emit OP_SETATTR with the property's
   * name index. */
  emit_byte(code, OP_SETATTR);
  emit_uint32(code, add_string(code, property.name));

  return result;
}

static CompileResult compile_expr_array(Bytecode *code, const Expr *expr)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  ExprArray expr_array = expr->as.expr_array;

  /* First, we compile the array elements in reverse. Why? If we had
   * done [1, 2, 3], when the vm popped these elements, it would ha-
   * ve got them in reversed order, that is, [3, 2, 1]. Compiling in
   * reverse avoids the overhead of sorting the elements at runtime. */
  for (int i = expr_array.elements.count - 1; i >= 0; i--) {
    COMPILE_EXPR(code, &expr_array.elements.data[i]);
  }

  /* Then, we emit OP_ARRAY and the number of elements. */
  emit_byte(code, OP_ARRAY);
  emit_uint32(code, expr_array.elements.count);

  return result;
}

static CompileResult compile_expr_subscript(Bytecode *code, const Expr *expr)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};
  ExprSubscript expr_subscript = expr->as.expr_subscript;

  /* First, we compile the expr. */
  COMPILE_EXPR(code, expr_subscript.expr);

  /* Then, we compile the index. */
  COMPILE_EXPR(code, expr_subscript.index);

  /* Then, we emit OP_SUBSCRIPT. */
  emit_byte(code, OP_SUBSCRIPT);

  return result;
}

typedef CompileResult (*CompileExprHandlerFn)(Bytecode *code, const Expr *expr);

typedef struct {
  CompileExprHandlerFn fn;
  char *name;
} CompileExprHandler;

static CompileExprHandler expression_handler[] = {
    [EXPR_LITERAL] = {.fn = compile_expr_lit, .name = "EXPR_LITERAL"},
    [EXPR_VARIABLE] = {.fn = compile_expr_var, .name = "EXPR_VARIABLE"},
    [EXPR_UNARY] = {.fn = compile_expr_una, .name = "EXPR_UNARY"},
    [EXPR_BINARY] = {.fn = compile_expr_bin, .name = "EXPR_BINARY"},
    [EXPR_CALL] = {.fn = compile_expr_call, .name = "EXPR_CALL"},
    [EXPR_GET] = {.fn = compile_expr_get, .name = "EXPR_GET"},
    [EXPR_ASSIGN] = {.fn = compile_expr_ass, .name = "EXPR_ASS"},
    [EXPR_STRUCT] = {.fn = compile_expr_struct, .name = "EXPR_STRUCT"},
    [EXPR_STRUCT_INITIALIZER] = {.fn = compile_expr_struct_initializer,
                                 .name = "EXPR_STRUCT_INITIALIZER"},
    [EXPR_ARRAY] = {.fn = compile_expr_array, .name = "EXPR_ARRAY"},
    [EXPR_SUBSCRIPT] = {.fn = compile_expr_subscript, .name = "EXPR_SUBSCRIPT"},
};

static CompileResult compile_expr(Bytecode *code, const Expr *expr)
{
  return expression_handler[expr->kind].fn(code, expr);
}

static CompileResult compile_stmt(Bytecode *code, const Stmt *stmt);

static CompileResult compile_stmt_print(Bytecode *code, const Stmt *stmt)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  StmtPrint s = stmt->as.stmt_print;
  COMPILE_EXPR(code, &s.expr);
  emit_byte(code, OP_PRINT);

  return result;
}

static CompileResult compile_stmt_let(Bytecode *code, const Stmt *stmt)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  if (current_compiler->locals_count >= 256) {
    COMPILER_ERROR("Maximum 256 locals.");
  }

  StmtLet s = stmt->as.stmt_let;

  /* Compile the initializer. */
  COMPILE_EXPR(code, &s.initializer);

  /* Add the variable name to the string pool. */
  uint32_t name_idx = add_string(code, s.name);

  /* If we're in global scope, emit OP_SET_GLOBAL,
   * otherwise, we want the value to remain on the
   * stack, so we will just make the compiler know
   * it is a local variable, and do some bookkeep-
   * ing regarding the number of variables we need
   * to pop off the stack when we do stack cleanup. */

  if (current_compiler->depth == 0) {
    current_compiler->globals[current_compiler->globals_count++] = (Local) {
        .name = code->sp.data[name_idx],
        .captured = false,
        .depth = current_compiler->depth,
    };
  } else {
    current_compiler->locals[current_compiler->locals_count++] = (Local) {
        .name = code->sp.data[name_idx],
        .captured = false,
        .depth = current_compiler->depth,
    };
  }

  if (current_compiler->depth == 0) {
    emit_byte(code, OP_SET_GLOBAL);
    emit_uint32(code, name_idx);
  }

  return result;
}

static CompileResult compile_stmt_expr(Bytecode *code, const Stmt *stmt)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  StmtExpr stmt_expr = stmt->as.stmt_expr;

  COMPILE_EXPR(code, &stmt_expr.expr);

  /* If the expression statement was just a call, like:
   *
   * ...
   * main(4);
   * ...
   *
   * Pop the return value off the stack, so it does not
   * interfere with later execution. */
  if (stmt_expr.expr.kind == EXPR_CALL) {
    emit_byte(code, OP_POP);
  }

  return result;
}

static CompileResult compile_stmt_block(Bytecode *code, const Stmt *stmt)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  begin_scope();
  StmtBlock stmt_block = stmt->as.stmt_block;

  /* Compile the body of the black. */
  for (size_t i = 0; i < stmt_block.stmts.count; i++) {
    COMPILE_STMT(code, &stmt_block.stmts.data[i]);
  }

  end_scope(code);

  return result;
}

static CompileResult compile_stmt_if(Bytecode *code, const Stmt *stmt)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  StmtIf stmt_if = stmt->as.stmt_if;

  /* We first compile the conditional expression because the VM
  .* expects a bool placed on the stack by the time it encount-
   * ers a conditional jump, that is, OP_JZ. */
  COMPILE_EXPR(code, &stmt_if.condition);

  /* Then, we emit OP_JZ, which jumps to the else clause if the
   * condition is falsey. Because we don't know the size of the
   * bytecode in the 'then' branch ahead of time, we do backpa-
   * tching: first, we emit 0xFFFF as the relative jump offset,
   * which serves as a stand-in for the real jump offset, which
   * will be known only after we compile the 'then' branch, and
   * find out its size. */
  int then_jump = emit_placeholder(code, OP_JZ);

  COMPILE_STMT(code, stmt_if.then_branch);

  /* Then, we emit OP_JMP, which jumps over the else branch, in
   * case the then branch was taken. */
  int else_jump = emit_placeholder(code, OP_JMP);

  /* Then, we patch the then jump because now we know its size. */
  patch_placeholder(code, then_jump);

  /* Then, we compile the else branch if it exists. */
  if (stmt_if.else_branch != NULL) {
    COMPILE_STMT(code, stmt_if.else_branch);
  }

  /* Finally, we patch the else jump. If the else branch wasn't
   * compiled, the offset should be zeroed out. */
  patch_placeholder(code, else_jump);

  return result;
}

static CompileResult compile_stmt_while(Bytecode *code, const Stmt *stmt)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  StmtWhile stmt_while = stmt->as.stmt_while;

  /* We need to mark the beginning of the loop before we compile
   * the conditional expression, so that we know where to return
   * after the body of the loop is executed. */

  int loop_start = code->code.count;

  Label label = {.location = loop_start, .patch_with = -1};
  table_insert(current_compiler->labels, stmt_while.label, label);

  /* We then compile the condition because the VM expects a bool
   * placed on the stack by the time it encounters a conditional
   * jump, that is, OP_JZ. */
  COMPILE_EXPR(code, &stmt_while.condition);

  /* We then emit OP_JZ which breaks out of the loop if the con-
   * dition is falsey. Because we don't know the size of the by-
   * tecode in the body of the 'while' loop ahead of time, we do
   * backpatching: first, we emit 0xFFFF as a relative jump off-
   * set which serves as a placeholder for the real jump offset. */
  int exit_jump = emit_placeholder(code, OP_JZ);

  /* Mark the loop depth (needed for break and continue). */
  dynarray_insert(&current_compiler->loop_depths, current_compiler->depth);

  /* Then, we compile the body of the loop. */
  COMPILE_STMT(code, stmt_while.body);

  /* Pop the loop depth as it's no longer needed. */
  dynarray_pop(&current_compiler->loop_depths);

  /* Then, we emit OP_JMP with a negative offset which jumps just
   * before the condition, so that we could evaluate it again and
   * see if we need to continue looping. */
  emit_loop(code, loop_start);

  int len = lblen(stmt_while.label, 0) + strlen("_exit");
  char *exit_label = malloc(len);
  snprintf(exit_label, len, "%s_exit", stmt_while.label);

  Label *loop_exit = table_get(current_compiler->labels, exit_label);
  if (!loop_exit) {
    Label le = {.location = code->code.count, .patch_with = -1};
    table_insert(current_compiler->labels, exit_label, le);
  } else {
    loop_exit->patch_with = code->code.count;
    table_insert(current_compiler->labels, exit_label, *loop_exit);
  }

  /* Finally, we patch the exit jump. */
  patch_placeholder(code, exit_jump);
  patch_jumps(code);

  free(exit_label);

  return result;
}

static CompileResult compile_stmt_for(Bytecode *code, const Stmt *stmt)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  StmtFor stmt_for = stmt->as.stmt_for;

  ExprAssign assignment = stmt_for.initializer.as.expr_assign;
  ExprVariable variable = assignment.lhs->as.expr_variable;

  /* Insert the initializer variable name into the current_compiler->locals
   * dynarray, since the condition that follows the initializer ex-
   * pects it to be there. */
  current_compiler->locals[current_compiler->locals_count++] = (Local) {
      .name = variable.name,
      .captured = false,
      .depth = current_compiler->depth,
  };

  /* Compile the right-hand side of the initializer first. */
  COMPILE_EXPR(code, assignment.rhs);

  /* Mark the beginning of the loop before compiling the condition,
   * so that we know where to jump after the loop body is executed. */
  int loop_start = code->code.count;

  Label label = {.location = loop_start, .patch_with = -1};
  table_insert(current_compiler->labels, stmt_for.label, label);

  /* Compile the conditional expression. */
  COMPILE_EXPR(code, &stmt_for.condition);

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
  COMPILE_EXPR(code, &stmt_for.advancement);

  /* After the loop body is executed, we jump to the advancement and
   * execute it. This means we need to evaluate the condition again,
   * so we're emitting a backward unconditional jump, which jumps to
   * just before the condition. */
  emit_loop(code, loop_start);

  /* Patch the jump now that we know the size of the advancement. */
  patch_placeholder(code, jump_over_advancement);

  /* Patch the loop_start we inserted to point to loop_continuation.
   * This is the place just before the advancement. */
  Label *ls = table_get(current_compiler->labels, stmt_for.label);
  ls->location = loop_continuation;
  table_insert(current_compiler->labels, stmt_for.label, *ls);

  /* Mark the loop depth (needed for break and continue). */
  dynarray_insert(&current_compiler->loop_depths, current_compiler->depth);

  /* Compile the loop body. */
  COMPILE_STMT(code, stmt_for.body);

  /* Pop the loop depth as it's no longer needed. */
  dynarray_pop(&current_compiler->loop_depths);

  /* Emit backward jump back to the advancement. */
  emit_loop(code, loop_continuation);

  current_compiler->locals_count--;

  int len = lblen(stmt_for.label, 0) + strlen("_exit");

  char *exit_label = malloc(len);
  snprintf(exit_label, len, "%s_exit", stmt_for.label);

  Label *loop_exit = table_get(current_compiler->labels, exit_label);
  if (!loop_exit) {
    Label l = {.location = code->code.count, .patch_with = -1};
    table_insert(current_compiler->labels, exit_label, l);
  } else {
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
  compiler.builtins = calloc(1, sizeof(Table_FunctionPtr));
  compiler.labels = calloc(1, sizeof(Table_Label));

  for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
    Function builtin = {0};
    builtin.name = builtins[i].name;
    builtin.paramcount = builtins[i].argcount;

    table_insert(compiler.builtins, builtin.name, ALLOC(builtin));
  }

  return ALLOC(compiler);
}

static CompileResult compile_stmt_fn(Bytecode *code, const Stmt *stmt)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  Compiler *old_compiler = current_compiler;
  current_compiler = new_compiler();
  current_compiler->next = old_compiler;
  current_compiler->depth = old_compiler->depth;

  StmtFn stmt_fn = stmt->as.stmt_fn;

  int funcname_idx = add_string(code, stmt_fn.name);

  Function func = {
      .name = code->sp.data[funcname_idx],
      .paramcount = stmt_fn.parameters.count,
      .location = code->code.count + 3,
  };

  table_insert(current_compiler->next->functions, func.name, func);

  current_compiler->current_fn = &func;

  Local *array = current_compiler->depth == 0 ? current_compiler->next->globals
                                              : current_compiler->next->locals;
  size_t *count = current_compiler->depth == 0
                      ? &current_compiler->next->globals_count
                      : &current_compiler->next->locals_count;
  array[(*count)++] = (Local) {
      .name = func.name,
      .depth = current_compiler->depth,
      .captured = false,
  };

  for (size_t i = 0; i < stmt_fn.parameters.count; i++) {
    current_compiler->locals[current_compiler->locals_count++] = (Local) {
        .name = stmt_fn.parameters.data[i],
        .depth = current_compiler->depth,
        .captured = false,
    };
  }

  /* Emit the jump because we don't want to execute the code
   * the first time we encounter it. */
  int jump = emit_placeholder(code, OP_JMP);

  /* Compile the function body. */
  COMPILE_STMT(code, stmt_fn.body);

  /* Finally, patch the jump. */
  patch_placeholder(code, jump);

  func.upvalue_count = current_compiler->upvalues.count;

  table_insert(current_compiler->functions, func.name, func);

  emit_byte(code, OP_CLOSURE);
  emit_uint32(code, add_string(code, func.name));
  emit_uint32(code, func.paramcount);
  emit_uint32(code, func.location);
  emit_uint32(code, func.upvalue_count);

  for (size_t i = 0; i < current_compiler->upvalues.count; i++) {
    emit_uint32(code, current_compiler->upvalues.data[i]);
  }

  if (current_compiler->depth == 0) {
    emit_byte(code, OP_SET_GLOBAL);
    emit_uint32(code, add_string(code, func.name));
  }

  free_compiler(current_compiler);
  free(current_compiler);

  current_compiler = old_compiler;

  return result;
}

static CompileResult compile_stmt_decorator(Bytecode *code, const Stmt *stmt)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  StmtDecorator stmt_decorator = stmt->as.stmt_decorator;

  COMPILE_STMT(code, stmt_decorator.fn);

  emit_byte(code, OP_GET_GLOBAL);
  emit_uint32(code, add_string(code, stmt_decorator.fn->as.stmt_fn.name));

  emit_byte(code, OP_GET_GLOBAL);
  emit_uint32(code, add_string(code, stmt_decorator.name));

  emit_byte(code, OP_CALL);

  uint32_t argcount;

  Function *f = resolve_func(stmt_decorator.name);

  if (f) {
    argcount = f->paramcount;
  } else {
    COMPILER_ERROR("...");
  }

  emit_byte(code, argcount);

  emit_byte(code, OP_SET_GLOBAL);
  emit_uint32(code, add_string(code, stmt_decorator.fn->as.stmt_fn.name));

  return result;
}

static CompileResult compile_stmt_struct(Bytecode *code, const Stmt *stmt)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  StmtStruct stmt_struct = stmt->as.stmt_struct;

  /* Emit some bytecode in the following format:
   *
   * OP_STRUCT_BLUEPRINT
   * 4-byte index of the struct name in the sp
   * 4-byte struct property count
   * for each property:
   *    4-byte index of the property name in the sp
   *    4-byte index of the property in the 'items' */
  emit_byte(code, OP_STRUCT_BLUEPRINT);
  emit_uint32(code, add_string(code, stmt_struct.name));
  emit_uint32(code, stmt_struct.properties.count);

  StructBlueprint blueprint = {.name = stmt_struct.name,
                               .property_indexes = calloc(1, sizeof(Table_int)),
                               .methods = calloc(1, sizeof(Table_Function))};

  for (size_t i = 0; i < stmt_struct.properties.count; i++) {
    emit_uint32(code, add_string(code, stmt_struct.properties.data[i]));
    table_insert(blueprint.property_indexes, stmt_struct.properties.data[i], i);
    emit_uint32(code, i);
  }

  /* Let the compiler know about the blueprint. */
  table_insert(current_compiler->struct_blueprints, blueprint.name, blueprint);

  return result;
}

static CompileResult compile_stmt_return(Bytecode *code, const Stmt *stmt)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  StmtReturn stmt_return = stmt->as.stmt_return;

  /* Compile the return value. */
  COMPILE_EXPR(code, &stmt_return.expr);

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

  for (int i = current_compiler->locals_count - 1; i >= 0; i--) {
    if (current_compiler->locals[i].captured) {
      emit_byte(code, OP_CLOSE_UPVALUE);
    } else {
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

static CompileResult compile_stmt_break(Bytecode *code, const Stmt *stmt)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  size_t len = lblen(stmt->as.stmt_break.label, 0) + strlen("_exit");

  char *exit_label = malloc(len);
  snprintf(exit_label, len, "%s_exit", stmt->as.stmt_break.label);

  emit_loop_cleanup(code);
  emit_named_jump(code, exit_label);
  free(exit_label);

  return result;
}

static CompileResult compile_stmt_continue(Bytecode *code, const Stmt *stmt)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  Label *loop_start =
      table_get(current_compiler->labels, stmt->as.stmt_continue.label);

  emit_loop_cleanup(code);
  emit_loop(code, loop_start->location);

  return result;
}

static CompileResult compile_stmt_impl(Bytecode *code, const Stmt *stmt)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};
  StmtImpl stmt_impl = stmt->as.stmt_impl;

  /* Look up the struct with that name in compiler->structs. */
  StructBlueprint *blueprint =
      table_get(current_compiler->struct_blueprints, stmt_impl.name);

  /* If it is not found, bail out. */
  if (!blueprint) {
    COMPILER_ERROR("struct '%s' is not defined.\n", stmt_impl.name);
  }

  for (size_t i = 0; i < stmt_impl.methods.count; i++) {
    StmtFn func = stmt_impl.methods.data[i].as.stmt_fn;
    Function f = {
        .name = func.name,
        .paramcount = func.parameters.count,
        .location = code->code.count + 3,
    };
    table_insert(blueprint->methods, func.name, ALLOC(f));
    COMPILE_STMT(code, &stmt_impl.methods.data[i]);
  }

  emit_byte(code, OP_IMPL);
  emit_uint32(code, add_string(code, blueprint->name));
  emit_uint32(code, stmt_impl.methods.count);

  for (size_t i = 0; i < stmt_impl.methods.count; i++) {
    StmtFn func = stmt_impl.methods.data[i].as.stmt_fn;
    Function **f = table_get(blueprint->methods, func.name);
    emit_uint32(code, add_string(code, (*f)->name));
    emit_uint32(code, (*f)->paramcount);
    emit_uint32(code, (*f)->location);
  }

  return result;
}

static CompileResult compile_stmt_yield(Bytecode *code, const Stmt *stmt)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  StmtYield stmt_yield = stmt->as.stmt_yield;
  COMPILE_EXPR(code, &stmt_yield.expr);
  emit_byte(code, OP_YIELD);

  Function *f = resolve_func(current_compiler->current_fn->name);

  f->is_gen = true;

  table_insert(current_compiler->functions, f->name, *f);

  return result;
}

static CompileResult compile_stmt_assert(Bytecode *code, const Stmt *stmt)
{
  CompileResult result = {.is_ok = true, .chunk = NULL, .msg = NULL};

  StmtAssert stmt_assert = stmt->as.stmt_assert;
  COMPILE_EXPR(code, &stmt_assert.expr);
  emit_byte(code, OP_ASSERT);

  return result;
}

typedef CompileResult (*CompileHandlerFn)(Bytecode *code, const Stmt *stmt);

typedef struct {
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
    [STMT_DECORATOR] = {.fn = compile_stmt_decorator, .name = "STMT_DECORATOR"},
    [STMT_STRUCT] = {.fn = compile_stmt_struct, .name = "STMT_STRUCT"},
    [STMT_RETURN] = {.fn = compile_stmt_return, .name = "STMT_RETURN"},
    [STMT_BREAK] = {.fn = compile_stmt_break, .name = "STMT_BREAK"},
    [STMT_CONTINUE] = {.fn = compile_stmt_continue, .name = "STMT_CONTINUE"},
    [STMT_YIELD] = {.fn = compile_stmt_yield, .name = "STMT_YIELD"},
    [STMT_ASSERT] = {.fn = compile_stmt_assert, .name = "STMT_ASSERT"},
};

static CompileResult compile_stmt(Bytecode *code, const Stmt *stmt)
{
  return stmt_handler[stmt->kind].fn(code, stmt);
}

CompileResult compile(const DynArray_Stmt *ast)
{
  CompileResult result = {
      .is_ok = true, .errcode = 0, .msg = NULL, .chunk = NULL};

  Bytecode *chunk = malloc(sizeof(Bytecode));
  init_chunk(chunk);

  for (size_t i = 0; i < ast->count; i++) {
    result = compile_stmt(chunk, &ast->data[i]);
    if (!result.is_ok) {
      free_chunk(chunk);
      free(chunk);
      return result;
    }
  }

  dynarray_insert(&chunk->code, OP_HLT);

  result.chunk = chunk;

  return result;
}
