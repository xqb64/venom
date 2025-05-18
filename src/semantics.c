#include "semantics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>  // IWYU pragma: keep

#include "ast.h"
#include "dynarray.h"
#include "util.h"

#define HANDLE_STMT(...)                   \
  do {                                     \
    result = loop_label_stmt(__VA_ARGS__); \
    if (!result.is_ok) {                   \
      return result;                       \
    }                                      \
  } while (0)

#define LOOP_LABEL_ERROR(...)              \
  alloc_err_str(&result.msg, __VA_ARGS__); \
  result.is_ok = false;                    \
  result.errcode = -1;                     \
  return result;

static size_t mktmp(void)
{
  static size_t tmp = 0;
  return tmp++;
}

LoopLabelResult loop_label_program(DynArray_Stmt *ast, const char *current)
{
  LoopLabelResult result = {
      .is_ok = true, .errcode = 0, .as.ast = {0}, .msg = NULL};

  DynArray_Stmt labeled_ast = {0};
  for (size_t i = 0; i < ast->count; i++) {
    LoopLabelResult stmt_result = loop_label_stmt(&ast->data[i], current);
    if (!stmt_result.is_ok) {
      free_ast(&labeled_ast);
      return stmt_result;
    }
    dynarray_insert(&labeled_ast, stmt_result.as.stmt);
  }

  result.as.ast = labeled_ast;

  return result;
}

LoopLabelResult loop_label_stmt(Stmt *stmt, const char *current)
{
  LoopLabelResult result = {
      .is_ok = true, .errcode = 0, .as.stmt = {0}, .msg = NULL};
  Stmt labeled_stmt;

  labeled_stmt.kind = stmt->kind;

  switch (stmt->kind) {
    case STMT_WHILE: {
      size_t tmp = mktmp();
      size_t label_len = lblen("while_", tmp);

      char *loop_label = malloc(label_len);
      snprintf(loop_label, label_len, "while_%zu", tmp);

      LoopLabelResult body_result =
          loop_label_stmt(stmt->as.stmt_while.body, loop_label);

      if (!body_result.is_ok) {
        free(loop_label);
        return body_result;
      }

      labeled_stmt.as.stmt_while.body = ALLOC(body_result.as.stmt);
      labeled_stmt.as.stmt_while.label = loop_label;
      labeled_stmt.as.stmt_while.condition =
          clone_expr(&stmt->as.stmt_while.condition);

      break;
    }
    case STMT_FOR: {
      size_t tmp = mktmp();
      size_t label_len = lblen("for_", tmp);

      char *loop_label = malloc(label_len);
      snprintf(loop_label, label_len, "for_%zu", tmp);

      LoopLabelResult body_result =
          loop_label_stmt(stmt->as.stmt_for.body, loop_label);

      if (!body_result.is_ok) {
        free(loop_label);
        return body_result;
      }

      labeled_stmt.as.stmt_for.body = ALLOC(body_result.as.stmt);
      labeled_stmt.as.stmt_for.label = loop_label;
      labeled_stmt.as.stmt_for.initializer =
          clone_expr(&stmt->as.stmt_for.initializer);
      labeled_stmt.as.stmt_for.condition =
          clone_expr(&stmt->as.stmt_for.condition);
      labeled_stmt.as.stmt_for.advancement =
          clone_expr(&stmt->as.stmt_for.advancement);

      break;
    }
    case STMT_BREAK: {
      if (!current) {
        LOOP_LABEL_ERROR("'break' statement outside the loop");
      }
      labeled_stmt.as.stmt_break.label = own_string(current);
      break;
    }
    case STMT_CONTINUE: {
      if (!current) {
        LOOP_LABEL_ERROR("'continue' statement outside the loop");
      }
      labeled_stmt.as.stmt_continue.label = own_string(current);
      break;
    }
    case STMT_FN: {
      DynArray_char_ptr parameters = {0};
      for (size_t i = 0; i < stmt->as.stmt_fn.parameters.count; i++) {
        dynarray_insert(&parameters,
                        own_string(stmt->as.stmt_fn.parameters.data[i]));
      }

      labeled_stmt.as.stmt_fn.parameters = parameters;
      labeled_stmt.as.stmt_fn.name = own_string(stmt->as.stmt_fn.name);

      LoopLabelResult body_result =
          loop_label_stmt(stmt->as.stmt_fn.body, current);

      if (!body_result.is_ok) {
        free(labeled_stmt.as.stmt_fn.name);

        for (size_t i = 0; i < parameters.count; i++) {
          free(parameters.data[i]);
        }
        dynarray_free(&parameters);

        return body_result;
      }

      labeled_stmt.as.stmt_fn.body = ALLOC(body_result.as.stmt);

      break;
    }
    case STMT_BLOCK: {
      LoopLabelResult block_result =
          loop_label_program(&stmt->as.stmt_block.stmts, current);

      if (!block_result.is_ok) {
        return block_result;
      }

      labeled_stmt.as.stmt_block.stmts = block_result.as.ast;
      labeled_stmt.as.stmt_block.depth = stmt->as.stmt_block.depth;

      break;
    }
    case STMT_IF: {
      LoopLabelResult then_result =
          loop_label_stmt(stmt->as.stmt_if.then_branch, current);

      if (!then_result.is_ok) {
        return then_result;
      }

      labeled_stmt.as.stmt_if.then_branch = ALLOC(then_result.as.stmt);

      if (stmt->as.stmt_if.else_branch) {
        LoopLabelResult else_result =
            loop_label_stmt(stmt->as.stmt_if.else_branch, current);

        if (!else_result.is_ok) {
          return else_result;
        }

        labeled_stmt.as.stmt_if.else_branch = ALLOC(else_result.as.stmt);
      } else {
        labeled_stmt.as.stmt_if.else_branch = NULL;
      }

      labeled_stmt.as.stmt_if.condition =
          clone_expr(&stmt->as.stmt_if.condition);

      break;
    }
    default: {
      labeled_stmt = clone_stmt(stmt);
      break;
    }
  }

  result.as.stmt = labeled_stmt;

  return result;
}
