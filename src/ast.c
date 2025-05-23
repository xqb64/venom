#include "ast.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>  // IWYU pragma: keep

#include "dynarray.h"
#include "util.h"

void free_expr(const Expr *expr)
{
  switch (expr->kind) {
    case EXPR_LITERAL: {
      ExprLiteral expr_lit = expr->as.expr_literal;
      if (expr_lit.kind == LIT_STRING) {
        free(expr_lit.as.str);
      }
      break;
    }
    case EXPR_VARIABLE: {
      ExprVariable expr_var = expr->as.expr_variable;
      free(expr_var.name);
      break;
    }
    case EXPR_UNARY: {
      ExprUnary expr_unary = expr->as.expr_unary;
      free_expr(expr_unary.expr);
      free(expr_unary.expr);
      free(expr_unary.op);
      break;
    }
    case EXPR_BINARY: {
      ExprBinary expr_bin = expr->as.expr_binary;
      free_expr(expr_bin.lhs);
      free_expr(expr_bin.rhs);
      free(expr_bin.lhs);
      free(expr_bin.rhs);
      free(expr_bin.op);
      break;
    }
    case EXPR_ASSIGN: {
      ExprAssign expr_assign = expr->as.expr_assign;
      free_expr(expr_assign.lhs);
      free_expr(expr_assign.rhs);
      free(expr_assign.lhs);
      free(expr_assign.rhs);
      free(expr_assign.op);
      break;
    }
    case EXPR_CALL: {
      ExprCall expr_call = expr->as.expr_call;
      free_expr(expr_call.callee);
      free(expr_call.callee);
      for (size_t i = 0; i < expr_call.arguments.count; i++) {
        free_expr(&expr_call.arguments.data[i]);
      }
      dynarray_free(&expr_call.arguments);
      break;
    }
    case EXPR_STRUCT: {
      ExprStruct expr_struct = expr->as.expr_struct;
      free(expr_struct.name);
      for (size_t i = 0; i < expr_struct.initializers.count; i++) {
        free_expr(&expr_struct.initializers.data[i]);
      }
      dynarray_free(&expr_struct.initializers);
      break;
    }
    case EXPR_STRUCT_INITIALIZER: {
      ExprStructInitializer expr_struct_initializer =
          expr->as.expr_struct_initializer;
      free_expr(expr_struct_initializer.property);
      free_expr(expr_struct_initializer.value);
      free(expr_struct_initializer.value);
      free(expr_struct_initializer.property);
      break;
    }
    case EXPR_GET: {
      ExprGet expr_get = expr->as.expr_get;
      free_expr(expr_get.expr);
      free(expr_get.property_name);
      free(expr_get.expr);
      free(expr_get.op);
      break;
    }
    case EXPR_ARRAY: {
      ExprArray expr_array = expr->as.expr_array;
      for (size_t i = 0; i < expr_array.elements.count; i++) {
        free_expr(&expr_array.elements.data[i]);
      }
      dynarray_free(&expr_array.elements);
      break;
    }
    case EXPR_SUBSCRIPT: {
      ExprSubscript expr_subscript = expr->as.expr_subscript;
      free_expr(expr_subscript.expr);
      free_expr(expr_subscript.index);
      free(expr_subscript.expr);
      free(expr_subscript.index);
      break;
    }
    case EXPR_CONDITIONAL: {
      ExprConditional expr_conditional = expr->as.expr_conditional;
      free_expr(expr_conditional.condition);
      free_expr(expr_conditional.then_branch);
      free_expr(expr_conditional.else_branch);
      free(expr_conditional.condition);
      free(expr_conditional.then_branch);
      free(expr_conditional.else_branch);
      break;
    }
    default:
      print_expr(expr, 0);
      assert(0);
  }
}

void free_stmt(const Stmt *stmt)
{
  switch (stmt->kind) {
    case STMT_PRINT: {
      free_expr(&stmt->as.stmt_print.expr);
      break;
    }
    case STMT_IMPL: {
      free(stmt->as.stmt_impl.name);
      for (size_t i = 0; i < stmt->as.stmt_impl.methods.count; i++) {
        free_stmt(&stmt->as.stmt_impl.methods.data[i]);
      }
      dynarray_free(&stmt->as.stmt_impl.methods);
      break;
    }
    case STMT_LET: {
      free_expr(&stmt->as.stmt_let.initializer);
      free(stmt->as.stmt_let.name);
      break;
    }
    case STMT_BLOCK: {
      for (size_t i = 0; i < stmt->as.stmt_block.stmts.count; i++) {
        free_stmt(&stmt->as.stmt_block.stmts.data[i]);
      }
      dynarray_free(&stmt->as.stmt_block.stmts);
      break;
    }
    case STMT_IF: {
      free_expr(&stmt->as.stmt_if.condition);

      free_stmt(stmt->as.stmt_if.then_branch);
      free(stmt->as.stmt_if.then_branch);

      if (stmt->as.stmt_if.else_branch != NULL) {
        free_stmt(stmt->as.stmt_if.else_branch);
        free(stmt->as.stmt_if.else_branch);
      }

      break;
    }
    case STMT_WHILE: {
      free(stmt->as.stmt_while.label);
      free_expr(&stmt->as.stmt_while.condition);
      for (size_t i = 0;
           i < stmt->as.stmt_while.body->as.stmt_block.stmts.count; i++) {
        free_stmt(&stmt->as.stmt_while.body->as.stmt_block.stmts.data[i]);
      }
      dynarray_free(&stmt->as.stmt_while.body->as.stmt_block.stmts);
      free(stmt->as.stmt_while.body);
      break;
    }
    case STMT_FOR: {
      free(stmt->as.stmt_for.label);
      free_expr(&stmt->as.stmt_for.initializer);
      free_expr(&stmt->as.stmt_for.condition);
      free_expr(&stmt->as.stmt_for.advancement);
      for (size_t i = 0; i < stmt->as.stmt_for.body->as.stmt_block.stmts.count;
           i++) {
        free_stmt(&stmt->as.stmt_for.body->as.stmt_block.stmts.data[i]);
      }
      dynarray_free(&stmt->as.stmt_for.body->as.stmt_block.stmts);
      free(stmt->as.stmt_for.body);
      break;
    }
    case STMT_RETURN: {
      free_expr(&stmt->as.stmt_return.expr);
      break;
    }
    case STMT_EXPR: {
      free_expr(&stmt->as.stmt_expr.expr);
      break;
    }
    case STMT_FN: {
      free(stmt->as.stmt_fn.name);
      for (size_t i = 0; i < stmt->as.stmt_fn.parameters.count; i++) {
        free(stmt->as.stmt_fn.parameters.data[i]);
      }
      dynarray_free(&stmt->as.stmt_fn.parameters);
      for (size_t i = 0; i < stmt->as.stmt_fn.body->as.stmt_block.stmts.count;
           i++) {
        free_stmt(&stmt->as.stmt_fn.body->as.stmt_block.stmts.data[i]);
      }
      dynarray_free(&stmt->as.stmt_fn.body->as.stmt_block.stmts);
      free(stmt->as.stmt_fn.body);
      break;
    }
    case STMT_DECORATOR: {
      free(stmt->as.stmt_decorator.name);
      free_stmt(stmt->as.stmt_decorator.fn);
      free(stmt->as.stmt_decorator.fn);
      break;
    }
    case STMT_STRUCT: {
      free(stmt->as.stmt_struct.name);
      for (size_t i = 0; i < stmt->as.stmt_struct.properties.count; i++) {
        free(stmt->as.stmt_struct.properties.data[i]);
      }
      dynarray_free(&stmt->as.stmt_struct.properties);
      break;
    }
    case STMT_USE: {
      free(stmt->as.stmt_use.path);
      break;
    }
    case STMT_YIELD: {
      free_expr(&stmt->as.stmt_yield.expr);
      break;
    }
    case STMT_ASSERT: {
      free_expr(&stmt->as.stmt_assert.expr);
      break;
    }
    case STMT_BREAK: {
      free(stmt->as.stmt_break.label);
      break;
    }
    case STMT_CONTINUE: {
      free(stmt->as.stmt_continue.label);
      break;
    }
    case STMT_GOTO: {
      free(stmt->as.stmt_goto.label);
      break;
    }
    case STMT_LABELED: {
      free(stmt->as.stmt_labeled.label);
      free_stmt(stmt->as.stmt_labeled.stmt);
      break;
    }
    default:
      print_stmt(stmt, 0, false);
      assert(0);
  }
}

void free_ast(const DynArray_Stmt *ast)
{
  for (size_t i = 0; i < ast->count; i++) {
    free_stmt(&ast->data[i]);
  }
  dynarray_free(ast);
}

void free_table_expr(const Table_Expr *table)
{
  for (size_t i = 0; i < TABLE_MAX; i++) {
    if (table->indexes[i]) {
      Bucket *head = table->indexes[i];
      Bucket *tmp;
      while (head != NULL) {
        tmp = head;
        head = head->next;
        free(tmp->key);
        free(tmp);
      }
    }
  }

  for (size_t i = 0; i < table->count; i++) {
    free_expr(&table->items[i]);
  }
}

static Bucket *clone_bucket(Bucket *src, const Expr *src_items, Expr *dst_items,
                            size_t *dst_count)
{
  if (!src) {
    return NULL;
  }

  Bucket *clone = malloc(sizeof(Bucket));
  clone->key = own_string(src->key);

  dst_items[*dst_count] = clone_expr(&src_items[src->value]);
  clone->value = (*dst_count)++;

  clone->next = clone_bucket(src->next, src_items, dst_items, dst_count);
  return clone;
}

Table_Expr clone_table_expr(const Table_Expr *src)
{
  Table_Expr clone = {0};

  for (size_t i = 0; i < TABLE_MAX; i++) {
    if (src->indexes[i]) {
      clone.indexes[i] =
          clone_bucket(src->indexes[i], src->items, clone.items, &clone.count);
    }
  }

  /* FIXME */
  for (size_t i = 0; i < src->count; i++) {
    clone.items[i] = clone_expr(&src->items[i]);
  }

  return clone;
}

ExprLiteral clone_literal(const ExprLiteral *literal)
{
  ExprLiteral clone;

  clone.kind = literal->kind;
  clone.span = literal->span;

  switch (literal->kind) {
    case LIT_NUMBER: {
      clone.as._double = literal->as._double;
      break;
    }
    case LIT_BOOLEAN: {
      clone.as._bool = literal->as._bool;
      break;
    }
    case LIT_STRING: {
      clone.as.str = own_string(literal->as.str);
      break;
    }
    default:
      break;
  }

  return clone;
}

Expr clone_expr(const Expr *expr)
{
  Expr clone;

  clone.kind = expr->kind;
  clone.span = expr->span;

  switch (expr->kind) {
    case EXPR_BINARY: {
      Expr lhs = clone_expr(expr->as.expr_binary.lhs);
      Expr rhs = clone_expr(expr->as.expr_binary.rhs);
      clone.as.expr_binary.lhs = ALLOC(lhs);
      clone.as.expr_binary.rhs = ALLOC(rhs);
      clone.as.expr_binary.op = own_string(expr->as.expr_binary.op);
      clone.as.expr_binary.span = expr->as.expr_binary.span;
      break;
    }
    case EXPR_ASSIGN: {
      Expr lhs = clone_expr(expr->as.expr_assign.lhs);
      Expr rhs = clone_expr(expr->as.expr_assign.rhs);
      clone.as.expr_assign.lhs = ALLOC(lhs);
      clone.as.expr_assign.rhs = ALLOC(rhs);
      clone.as.expr_assign.op = own_string(expr->as.expr_assign.op);
      clone.as.expr_assign.span = expr->as.expr_assign.span;
      break;
    }
    case EXPR_UNARY: {
      Expr exp = clone_expr(expr->as.expr_unary.expr);
      clone.as.expr_unary.expr = ALLOC(exp);
      clone.as.expr_unary.op = own_string(expr->as.expr_unary.op);
      clone.as.expr_unary.span = expr->as.expr_unary.span;
      break;
    }
    case EXPR_CALL: {
      DynArray_Expr args = {0};
      for (size_t i = 0; i < expr->as.expr_call.arguments.count; i++) {
        Expr cloned = clone_expr(&expr->as.expr_call.arguments.data[i]);
        dynarray_insert(&args, cloned);
      }
      clone.as.expr_call.arguments = args;
      Expr callee_expr = clone_expr(expr->as.expr_call.callee);
      clone.as.expr_call.callee = ALLOC(callee_expr);
      clone.as.expr_call.span = expr->as.expr_call.span;
      break;
    }
    case EXPR_LITERAL: {
      ExprLiteral cloned = clone_literal(&expr->as.expr_literal);
      clone.as.expr_literal = cloned;
      clone.as.expr_literal.span = expr->as.expr_literal.span;
      break;
    }
    case EXPR_VARIABLE: {
      clone.as.expr_variable.name = own_string(expr->as.expr_variable.name);
      clone.as.expr_variable.span = expr->as.expr_variable.span;
      break;
    }
    case EXPR_ARRAY: {
      DynArray_Expr elements = {0};
      for (size_t i = 0; i < expr->as.expr_array.elements.count; i++) {
        Expr cloned = clone_expr(&expr->as.expr_array.elements.data[i]);
        dynarray_insert(&elements, cloned);
      }
      clone.as.expr_array.elements = elements;
      clone.as.expr_array.span = expr->as.expr_array.span;
      break;
    }
    case EXPR_SUBSCRIPT: {
      Expr cloned_expr = clone_expr(expr->as.expr_subscript.expr);
      Expr cloned_index = clone_expr(expr->as.expr_subscript.index);
      clone.as.expr_subscript.expr = ALLOC(cloned_expr);
      clone.as.expr_subscript.index = ALLOC(cloned_index);
      clone.as.expr_subscript.span = expr->as.expr_subscript.span;
      break;
    }
    case EXPR_STRUCT: {
      DynArray_Expr initializers = {0};
      for (size_t i = 0; i < expr->as.expr_struct.initializers.count; i++) {
        Expr cloned = clone_expr(&expr->as.expr_struct.initializers.data[i]);
        dynarray_insert(&initializers, cloned);
      }
      clone.as.expr_struct.initializers = initializers;
      clone.as.expr_struct.name = own_string(expr->as.expr_struct.name);
      clone.as.expr_struct.span = expr->as.expr_struct.span;
      break;
    }
    case EXPR_STRUCT_INITIALIZER: {
      Expr value = clone_expr(expr->as.expr_struct_initializer.value);
      Expr property = clone_expr(expr->as.expr_struct_initializer.property);
      clone.as.expr_struct_initializer.value = ALLOC(value);
      clone.as.expr_struct_initializer.property = ALLOC(property);
      clone.as.expr_struct_initializer.span =
          expr->as.expr_struct_initializer.span;
      break;
    }
    case EXPR_GET: {
      Expr gettee = clone_expr(expr->as.expr_get.expr);
      clone.as.expr_get.expr = ALLOC(gettee);
      clone.as.expr_get.op = own_string(expr->as.expr_get.op);
      clone.as.expr_get.property_name =
          own_string(expr->as.expr_get.property_name);
      clone.as.expr_get.span = expr->as.expr_get.span;
      break;
    }
    case EXPR_CONDITIONAL: {
      Expr cloned_condition = clone_expr(expr->as.expr_conditional.condition);
      Expr cloned_then_branch =
          clone_expr(expr->as.expr_conditional.then_branch);
      Expr cloned_else_branch =
          clone_expr(expr->as.expr_conditional.else_branch);
      clone.as.expr_conditional.condition = ALLOC(cloned_condition);
      clone.as.expr_conditional.then_branch = ALLOC(cloned_then_branch);
      clone.as.expr_conditional.else_branch = ALLOC(cloned_else_branch);
      clone.as.expr_conditional.span = expr->as.expr_conditional.span;
      break;
    }
    default:
      print_expr(expr, 0);
      assert(0);
  }

  return clone;
}

Stmt clone_stmt(const Stmt *stmt)
{
  Stmt clone;

  clone.kind = stmt->kind;
  clone.span = stmt->span;

  switch (stmt->kind) {
    case STMT_LET: {
      clone.as.stmt_let.initializer =
          clone_expr(&stmt->as.stmt_let.initializer);
      clone.as.stmt_let.name = own_string(stmt->as.stmt_let.name);
      break;
    }
    case STMT_FN: {
      clone.as.stmt_fn.name = own_string(stmt->as.stmt_fn.name);
      DynArray_char_ptr parameters = {0};
      for (size_t i = 0; i < stmt->as.stmt_fn.parameters.count; i++) {
        dynarray_insert(&parameters,
                        own_string(stmt->as.stmt_fn.parameters.data[i]));
      }
      clone.as.stmt_fn.parameters = parameters;
      Stmt body = clone_stmt(stmt->as.stmt_fn.body);
      clone.as.stmt_fn.body = ALLOC(body);
      break;
    }
    case STMT_BLOCK: {
      clone.as.stmt_block.depth = stmt->as.stmt_block.depth;
      clone.as.stmt_block.stmts = clone_ast(&stmt->as.stmt_block.stmts);
      break;
    }
    case STMT_PRINT: {
      clone.as.stmt_print.expr = clone_expr(&stmt->as.stmt_print.expr);
      break;
    }
    case STMT_WHILE: {
      clone.as.stmt_while.label = own_string(stmt->as.stmt_while.label);
      clone.as.stmt_while.condition =
          clone_expr(&stmt->as.stmt_while.condition);
      Stmt body = clone_stmt(stmt->as.stmt_while.body);
      clone.as.stmt_while.body = ALLOC(body);
      break;
    }
    case STMT_FOR: {
      clone.as.stmt_for.initializer =
          clone_expr(&stmt->as.stmt_for.initializer);
      clone.as.stmt_for.condition = clone_expr(&stmt->as.stmt_for.condition);
      clone.as.stmt_for.advancement =
          clone_expr(&stmt->as.stmt_for.advancement);
      Stmt body = clone_stmt(stmt->as.stmt_for.body);
      clone.as.stmt_for.body = ALLOC(body);
      clone.as.stmt_for.label = own_string(stmt->as.stmt_for.label);
      break;
    }
    case STMT_EXPR: {
      clone.as.stmt_expr.expr = clone_expr(&stmt->as.stmt_expr.expr);
      break;
    }
    case STMT_BREAK: {
      clone.as.stmt_break.label = own_string(stmt->as.stmt_break.label);
      break;
    }
    case STMT_CONTINUE: {
      clone.as.stmt_continue.label = own_string(stmt->as.stmt_continue.label);
      break;
    }
    case STMT_ASSERT: {
      clone.as.stmt_assert.expr = clone_expr(&stmt->as.stmt_assert.expr);
      break;
    }
    case STMT_DECORATOR: {
      Stmt body = clone_stmt(stmt->as.stmt_decorator.fn);
      clone.as.stmt_decorator.fn = ALLOC(body);
      clone.as.stmt_decorator.name = own_string(stmt->as.stmt_decorator.name);
      break;
    }
    case STMT_IF: {
      clone.as.stmt_if.condition = clone_expr(&stmt->as.stmt_if.condition);
      Stmt then_branch = clone_stmt(stmt->as.stmt_if.then_branch);
      Stmt *else_branch = NULL;
      if (stmt->as.stmt_if.else_branch) {
        Stmt s = clone_stmt(stmt->as.stmt_if.else_branch);
        else_branch = ALLOC(s);
      }
      clone.as.stmt_if.then_branch = ALLOC(then_branch);
      clone.as.stmt_if.else_branch = else_branch;
      break;
    }
    case STMT_IMPL: {
      clone.as.stmt_impl.name = own_string(stmt->as.stmt_impl.name);
      DynArray_Stmt methods = {0};
      for (size_t i = 0; i < stmt->as.stmt_impl.methods.count; i++) {
        Stmt s = clone_stmt(&stmt->as.stmt_impl.methods.data[i]);
        dynarray_insert(&methods, s);
      }
      clone.as.stmt_impl.methods = methods;
      break;
    }
    case STMT_RETURN: {
      clone.as.stmt_return.expr = clone_expr(&stmt->as.stmt_return.expr);
      break;
    }
    case STMT_YIELD: {
      clone.as.stmt_yield.expr = clone_expr(&stmt->as.stmt_yield.expr);
      break;
    }
    case STMT_USE: {
      clone.as.stmt_use.path = own_string(stmt->as.stmt_use.path);
      break;
    }
    case STMT_STRUCT: {
      clone.as.stmt_struct.name = own_string(stmt->as.stmt_struct.name);
      DynArray_char_ptr properties = {0};
      for (size_t i = 0; i < stmt->as.stmt_struct.properties.count; i++) {
        char *s = own_string(stmt->as.stmt_struct.properties.data[i]);
        dynarray_insert(&properties, s);
      }
      clone.as.stmt_struct.properties = properties;
      break;
    }
    case STMT_GOTO: {
      clone.as.stmt_goto.label = own_string(stmt->as.stmt_goto.label);
      clone.as.stmt_goto.span = stmt->as.stmt_goto.span;
      break;
    }
    case STMT_LABELED: {
      clone.as.stmt_labeled.label = own_string(stmt->as.stmt_labeled.label);
      clone.as.stmt_labeled.span = stmt->as.stmt_labeled.span;
      Stmt cloned_stmt = clone_stmt(stmt->as.stmt_labeled.stmt);
      clone.as.stmt_labeled.stmt = ALLOC(cloned_stmt);
      break;
    }
    default:
      print_stmt(stmt, 0, false);
      assert(0);
  }

  return clone;
}

DynArray_Stmt clone_ast(const DynArray_Stmt *ast)
{
  DynArray_Stmt clone = {0};

  for (size_t i = 0; i < ast->count; i++) {
    Stmt s = clone_stmt(&ast->data[i]);
    dynarray_insert(&clone, s);
  }

  return clone;
}

static void print_literal(const ExprLiteral *literal)
{
  switch (literal->kind) {
    case LIT_BOOLEAN: {
      printf("%s", literal->as._bool ? "true" : "false");
      break;
    }
    case LIT_NULL: {
      printf("null");
      break;
    }
    case LIT_NUMBER: {
      printf("%.16g", literal->as._double);
      break;
    }
    case LIT_STRING: {
      printf("%s", literal->as.str);
      break;
    }
    default:
      assert(0);
  }
}

#define INDENT(n)                 \
  do {                            \
    for (int s = 0; s < (n); s++) \
      putchar(' ');               \
  } while (0)

void print_expr(const Expr *expr, int indent)
{
  switch (expr->kind) {
    case EXPR_LITERAL: {
      printf("Literal(\n");
      INDENT(indent + 4);
      print_literal(&expr->as.expr_literal);
      break;
    }
    case EXPR_ARRAY: {
      printf("Array(\n");
      INDENT(indent + 4);
      printf("members: [");
      for (size_t i = 0; i < expr->as.expr_array.elements.count; i++) {
        print_expr(&expr->as.expr_array.elements.data[i], indent + 4);
        if (i < expr->as.expr_array.elements.count - 1) {
          printf(", ");
        }
      }
      printf("]");
      break;
    }
    case EXPR_STRUCT: {
      printf("Struct(\n");
      INDENT(indent + 4);
      printf("name: %s,\n", expr->as.expr_struct.name);
      INDENT(indent + 4);
      printf("initializers: [\n");
      for (size_t i = 0; i < expr->as.expr_struct.initializers.count; i++) {
        INDENT(indent + 8);
        print_expr(&expr->as.expr_struct.initializers.data[i], indent + 8);
        if (i < expr->as.expr_struct.initializers.count - 1) {
          printf(",\n");
        }
      }
      break;
    }
    case EXPR_STRUCT_INITIALIZER: {
      printf("StructInit(\n");
      INDENT(indent + 4);
      printf("property: ");
      print_expr(expr->as.expr_struct_initializer.property, indent + 4);
      printf(",\n");
      INDENT(indent + 4);
      printf("value: ");
      print_expr(expr->as.expr_struct_initializer.value, indent + 4);
      break;
    }
    case EXPR_BINARY: {
      printf("Binary(\n");
      INDENT(indent + 4);
      print_expr(expr->as.expr_binary.lhs, indent + 4);
      printf(" %s ", expr->as.expr_binary.op);
      print_expr(expr->as.expr_binary.rhs, indent + 4);
      printf(" [%ld, %ld]", expr->span.start, expr->span.end);
      break;
    }
    case EXPR_GET: {
      printf("Get(\n");
      INDENT(indent + 4);
      printf("gettee: %s,\n", expr->as.expr_get.property_name);
      INDENT(indent + 4);
      printf("op: `%s`\n", expr->as.expr_get.op);
      INDENT(indent + 4);
      printf("exp: ");
      print_expr(expr->as.expr_get.expr, indent + 4);
      break;
    }
    case EXPR_SUBSCRIPT: {
      printf("Subscript(\n");
      INDENT(indent + 4);
      printf("subscriptee: ");
      print_expr(expr->as.expr_subscript.expr, indent + 4);
      printf(",\n");
      INDENT(indent + 4);
      printf("index: ");
      print_expr(expr->as.expr_subscript.index, indent + 4);
      break;
    }
    case EXPR_UNARY: {
      printf("Unary(\n");
      INDENT(indent + 4);
      printf("exp: ");
      print_expr(expr->as.expr_unary.expr, indent + 4);
      printf(",\n");
      INDENT(indent + 4);
      printf("op: %s", expr->as.expr_unary.op);
      break;
    }
    case EXPR_VARIABLE: {
      printf("Variable(\n");
      INDENT(indent + 4);
      printf("name: %s", expr->as.expr_variable.name);
      break;
    }
    case EXPR_ASSIGN: {
      printf("Assign(\n");
      INDENT(indent + 4);
      print_expr(expr->as.expr_assign.lhs, indent + 4);
      printf(" %s ", expr->as.expr_assign.op);
      print_expr(expr->as.expr_assign.rhs, indent + 4);
      break;
    }
    case EXPR_CALL: {
      printf("Call(\n");
      INDENT(indent + 4);
      printf("callee: ");
      print_expr(expr->as.expr_call.callee, indent + 4);
      printf(", \n");
      INDENT(indent + 4);
      printf("arguments: [");
      for (size_t i = 0; i < expr->as.expr_call.arguments.count; i++) {
        print_expr(&expr->as.expr_call.arguments.data[i], indent + 4);
        if (i < expr->as.expr_call.arguments.count - 1) {
          printf(", ");
        }
      }
      printf("]");
      break;
    }
    case EXPR_CONDITIONAL: {
      printf("Conditional(\n");
      INDENT(indent + 4);
      printf("condition: ");
      print_expr(expr->as.expr_conditional.condition, indent + 4);
      printf(", \n");
      INDENT(indent + 4);
      printf("then_branch: ");
      print_expr(expr->as.expr_conditional.then_branch, indent + 4);
      printf(", \n");
      INDENT(indent + 4);
      printf("else_branch: ");
      print_expr(expr->as.expr_conditional.else_branch, indent + 4);
      break;
    }
    default:
      assert(0);
  }
  printf("\n");
  INDENT(indent);
  printf(")");
}

void print_stmt(const Stmt *stmt, int indent, bool continuation)
{
  if (!continuation) {
    INDENT(indent);
  }

  switch (stmt->kind) {
    case STMT_LET: {
      printf("Let(\n");
      INDENT(indent + 4);
      printf("name: %s,\n", stmt->as.stmt_let.name);
      INDENT(indent + 4);
      printf("initializer: ");
      print_expr(&stmt->as.stmt_let.initializer, indent + 4);
      break;
    }
    case STMT_PRINT: {
      printf("Print(\n");
      INDENT(indent + 4);
      print_expr(&stmt->as.stmt_print.expr, indent + 4);
      break;
    }
    case STMT_FN: {
      printf("Function(\n");
      INDENT(indent + 4);
      print_stmt(stmt->as.stmt_fn.body, indent + 4, true);
      break;
    }
    case STMT_BLOCK: {
      printf("Block(\n");
      for (size_t i = 0; i < stmt->as.stmt_block.stmts.count; i++) {
        print_stmt(&stmt->as.stmt_block.stmts.data[i], indent + 4, false);
        if (i < stmt->as.stmt_block.stmts.count - 1) {
          printf(",\n");
        }
      }
      break;
    }
    case STMT_WHILE: {
      printf("While(\n");
      INDENT(indent + 4);
      printf("label: \"%s\",\n", stmt->as.stmt_while.label);
      INDENT(indent + 4);
      printf("condition: ");
      print_expr(&stmt->as.stmt_while.condition, indent + 4);
      printf(",\n");
      INDENT(indent + 4);
      printf("body: ");
      print_stmt(stmt->as.stmt_while.body, indent + 4, true);
      break;
    }
    case STMT_FOR: {
      printf("For(\n");
      INDENT(indent + 4);
      printf("label: \"%s\",\n", stmt->as.stmt_for.label);
      INDENT(indent + 4);
      printf("init: ");
      print_expr(&stmt->as.stmt_for.initializer, indent + 4);
      putchar('\n');
      INDENT(indent + 4);
      printf("condition: ");
      print_expr(&stmt->as.stmt_for.condition, indent + 4);
      putchar('\n');
      INDENT(indent + 4);
      printf("advancement: ");
      print_expr(&stmt->as.stmt_for.advancement, indent + 4);
      putchar('\n');
      INDENT(indent + 4);
      printf("body: ");
      print_stmt(stmt->as.stmt_for.body, indent + 4, true);
      break;
    }
    case STMT_IF: {
      printf("If(\n");
      INDENT(indent + 4);
      printf("condition: ");
      print_expr(&stmt->as.stmt_if.condition, indent + 4);
      printf(",\n");
      INDENT(indent + 4);
      printf("then: ");
      print_stmt(stmt->as.stmt_if.then_branch, indent + 4, true);
      printf(",\n");
      INDENT(indent + 4);
      printf("else: ");
      if (stmt->as.stmt_if.else_branch) {
        print_stmt(stmt->as.stmt_if.else_branch, indent + 4, true);
      } else {
        printf("null");
      }
      break;
    }
    case STMT_EXPR: {
      printf("Expr(\n");
      INDENT(indent + 4);
      print_expr(&stmt->as.stmt_expr.expr, indent + 4);
      break;
    }
    case STMT_RETURN: {
      printf("Return(\n");
      INDENT(indent + 4);
      print_expr(&stmt->as.stmt_return.expr, indent + 4);
      break;
    }
    case STMT_BREAK: {
      printf("Break(\n");
      INDENT(indent + 4);
      printf("label: \"%s\"", stmt->as.stmt_break.label);
      break;
    }
    case STMT_CONTINUE: {
      printf("Continue(\n");
      INDENT(indent + 4);
      printf("label: \"%s\"", stmt->as.stmt_continue.label);
      break;
    }
    case STMT_ASSERT: {
      printf("Assert(\n");
      INDENT(indent + 4);
      print_expr(&stmt->as.stmt_assert.expr, indent + 4);
      break;
    }
    case STMT_USE: {
      printf("Use(%s)\n", stmt->as.stmt_use.path);
      break;
    }
    case STMT_YIELD: {
      printf("Yield(\n");
      INDENT(indent + 4);
      print_expr(&stmt->as.stmt_yield.expr, indent + 4);
      break;
    }
    case STMT_DECORATOR: {
      printf("Decorator(\n");
      INDENT(indent + 4);
      printf("name: %s,\n", stmt->as.stmt_decorator.name);
      INDENT(indent + 4);
      printf("fn: ");
      print_stmt(stmt->as.stmt_decorator.fn, indent + 4, true);
      break;
    }
    case STMT_STRUCT: {
      printf("Struct(\n");
      INDENT(indent + 4);
      printf("name: %s\n", stmt->as.stmt_struct.name);
      INDENT(indent + 4);
      printf("properties: [");
      for (size_t i = 0; i < stmt->as.stmt_struct.properties.count; i++) {
        printf("%s", stmt->as.stmt_struct.properties.data[i]);
        if (i < stmt->as.stmt_struct.properties.count - 1) {
          printf(", ");
        }
      }
      printf("]");
      break;
    }
    case STMT_IMPL: {
      printf("Impl(\n");
      INDENT(indent + 4);
      printf("name: %s,\n", stmt->as.stmt_impl.name);
      INDENT(indent + 4);
      printf("methods: [");
      for (size_t i = 0; i < stmt->as.stmt_impl.methods.count; i++) {
        print_stmt(&stmt->as.stmt_impl.methods.data[i], indent + 4, true);
        if (i < stmt->as.stmt_impl.methods.count - 1) {
          printf(", ");
        }
      }
      printf("]");
      break;
    }
    case STMT_GOTO: {
      printf("Goto(\n");
      INDENT(indent + 4);
      printf("label: %s,\n", stmt->as.stmt_goto.label);
      break;
    }
    case STMT_LABELED: {
      printf("Labeled(\n");
      INDENT(indent + 4);
      printf("label: %s,\n", stmt->as.stmt_labeled.label);
      INDENT(indent + 4);
      print_stmt(stmt->as.stmt_labeled.stmt, indent + 4, true);
      break;
    }
    default:
      assert(0);
  }
  printf("\n");
  INDENT(indent);
  printf(")");
}

void print_ast(const DynArray_Stmt *ast)
{
  int indent = 0;

  printf("Program(\n");

  for (size_t i = 0; i < ast->count; i++) {
    print_stmt(&ast->data[i], indent + 4, false);
    if (i < ast->count - 1) {
      printf(",\n");
    }
  }

  printf("\n)\n");
}
