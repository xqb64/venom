#include "semantics.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>  // IWYU pragma: keep
#include <time.h>

#include "ast.h"
#include "dynarray.h"
#include "util.h"

static size_t mktmp(void)
{
  static size_t tmp = 0;
  return tmp++;
}

static LoopLabelResult loop_label_stmt(Stmt *stmt, const char *current)
{
  LoopLabelResult result = {
      .is_ok = true, .errcode = 0, .as.stmt = {0}, .msg = NULL};
  Stmt labeled_stmt;

  labeled_stmt.kind = stmt->kind;

  switch (stmt->kind) {
    case STMT_DO_WHILE: {
      size_t tmp = mktmp();
      size_t label_len = lblen("do_while_", tmp);

      char *loop_label = malloc(label_len);
      snprintf(loop_label, label_len, "do_while_%zu", tmp);

      LoopLabelResult body_result =
          loop_label_stmt(stmt->as.stmt_do_while.body, loop_label);

      if (!body_result.is_ok) {
        free(loop_label);
        return body_result;
      }

      labeled_stmt.as.stmt_do_while.body = ALLOC(body_result.as.stmt);
      labeled_stmt.as.stmt_do_while.label = loop_label;
      labeled_stmt.as.stmt_do_while.condition =
          clone_expr(&stmt->as.stmt_do_while.condition);

      break;
    }
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
        alloc_err_str(&result.msg, "'break' statement outside the loop");
        result.is_ok = false;
        result.errcode = -1;
        result.span = stmt->span;
        return result;
      }
      labeled_stmt.as.stmt_break.label = own_string(current);
      break;
    }
    case STMT_CONTINUE: {
      if (!current) {
        alloc_err_str(&result.msg, "'continue' statement outside the loop");
        result.is_ok = false;
        result.errcode = -1;
        result.span = stmt->span;
        return result;
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
    case STMT_LABELED: {
      LoopLabelResult stmt_result =
          loop_label_stmt(stmt->as.stmt_labeled.stmt, current);
      if (!stmt_result.is_ok) {
        return stmt_result;
      }

      labeled_stmt.as.stmt_labeled.stmt = ALLOC(stmt_result.as.stmt);
      labeled_stmt.as.stmt_labeled.span = stmt->as.stmt_labeled.span;
      labeled_stmt.as.stmt_labeled.label =
          own_string(stmt->as.stmt_labeled.label);
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

LoopLabelResult loop_label_program(DynArray_Stmt *ast, const char *current)
{
  LoopLabelResult result = {.is_ok = true,
                            .errcode = 0,
                            .as.ast = {0},
                            .msg = NULL,
                            .span = {0},
                            .time = 0.0};

  struct timespec start, end;

  clock_gettime(CLOCK_MONOTONIC, &start);

  DynArray_Stmt labeled_ast = {0};
  for (size_t i = 0; i < ast->count; i++) {
    LoopLabelResult stmt_result = loop_label_stmt(&ast->data[i], current);
    if (!stmt_result.is_ok) {
      free_ast(&labeled_ast);
      return stmt_result;
    }
    dynarray_insert(&labeled_ast, stmt_result.as.stmt);
  }

  clock_gettime(CLOCK_MONOTONIC, &end);

  result.as.ast = labeled_ast;
  result.time =
      (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

  return result;
}

static LabelCheckResult label_collect_stmt(const Stmt *stmt,
                                           DynArray_char_ptr *labels,
                                           char *funcname)
{
  switch (stmt->kind) {
    case STMT_FN: {
      for (size_t i = 0; i < stmt->as.stmt_fn.body->as.stmt_block.stmts.count;
           i++) {
        LabelCheckResult collect_result = label_collect_stmt(
            &stmt->as.stmt_fn.body->as.stmt_block.stmts.data[i], labels,
            funcname);
        if (!collect_result.is_ok) {
          return collect_result;
        }
      }
      break;
    }
    case STMT_BLOCK: {
      for (size_t i = 0; i < stmt->as.stmt_block.stmts.count; i++) {
        LabelCheckResult collect_result = label_collect_stmt(
            &stmt->as.stmt_block.stmts.data[i], labels, funcname);
        if (!collect_result.is_ok) {
          return collect_result;
        }
      }
      break;
    }
    case STMT_DO_WHILE: {
      for (size_t i = 0;
           i < stmt->as.stmt_do_while.body->as.stmt_block.stmts.count; i++) {
        LabelCheckResult collect_result = label_collect_stmt(
            &stmt->as.stmt_do_while.body->as.stmt_block.stmts.data[i], labels,
            funcname);
        if (!collect_result.is_ok) {
          return collect_result;
        }
      }
      break;
    }
    case STMT_WHILE: {
      for (size_t i = 0;
           i < stmt->as.stmt_while.body->as.stmt_block.stmts.count; i++) {
        LabelCheckResult collect_result = label_collect_stmt(
            &stmt->as.stmt_while.body->as.stmt_block.stmts.data[i], labels,
            funcname);
        if (!collect_result.is_ok) {
          return collect_result;
        }
      }
      break;
    }
    case STMT_LABELED: {
      for (size_t i = 0; i < labels->count; i++) {
        if (strcmp(labels->data[i], stmt->as.stmt_labeled.label) == 0) {
          return (LabelCheckResult) {.is_ok = false,
                                     .errcode = -1,
                                     .msg = strdup("Duplicate label"),
                                     .span = stmt->as.stmt_labeled.span};
        }
      }

      size_t len = strlen(stmt->as.stmt_labeled.label) + strlen(funcname) + 1;
      char *label = malloc(len);
      snprintf(label, len + 1, "%s_%s", stmt->as.stmt_labeled.label, funcname);

      dynarray_insert(labels, label);

      LabelCheckResult body_result =
          label_collect_stmt(stmt->as.stmt_labeled.stmt, labels, funcname);
      if (!body_result.is_ok) {
        return body_result;
      }

      break;
    }
    default:
      break;
  }
  return (LabelCheckResult) {
      .is_ok = true, .msg = NULL, .errcode = 0, .span = {0}};
}

static LabelCheckResult label_check_stmt(const Stmt *stmt,
                                         DynArray_char_ptr *labels,
                                         char *funcname)
{
  switch (stmt->kind) {
    case STMT_FN: {
      DynArray_char_ptr new_labels = {0};

      StmtBlock body = stmt->as.stmt_fn.body->as.stmt_block;

      for (size_t i = 0; i < body.stmts.count; i++) {
        LabelCheckResult collect_result = label_collect_stmt(
            &stmt->as.stmt_fn.body->as.stmt_block.stmts.data[i], &new_labels,
            stmt->as.stmt_fn.name);
        if (!collect_result.is_ok) {
          return collect_result;
        }
      }

      for (size_t i = 0; i < body.stmts.count; i++) {
        LabelCheckResult check_result = label_check_stmt(
            &body.stmts.data[i], &new_labels, stmt->as.stmt_fn.name);
        if (!check_result.is_ok) {
          return check_result;
        }
      }
      break;
    }
    case STMT_DO_WHILE: {
      StmtBlock body = stmt->as.stmt_do_while.body->as.stmt_block;

      for (size_t i = 0; i < body.stmts.count; i++) {
        LabelCheckResult check_result =
            label_check_stmt(&body.stmts.data[i], labels, funcname);
        if (!check_result.is_ok) {
          return check_result;
        }
      }
      break;
    }
    case STMT_WHILE: {
      StmtBlock body = stmt->as.stmt_while.body->as.stmt_block;

      for (size_t i = 0; i < body.stmts.count; i++) {
        LabelCheckResult check_result =
            label_check_stmt(&body.stmts.data[i], labels, funcname);
        if (!check_result.is_ok) {
          return check_result;
        }
      }
      break;
    }
    case STMT_FOR: {
      StmtBlock body = stmt->as.stmt_for.body->as.stmt_block;

      for (size_t i = 0; i < body.stmts.count; i++) {
        LabelCheckResult check_result =
            label_check_stmt(&body.stmts.data[i], labels, funcname);
        if (!check_result.is_ok) {
          return check_result;
        }
      }
      break;
    }
    case STMT_BLOCK: {
      for (size_t i = 0; i < stmt->as.stmt_block.stmts.count; i++) {
        LabelCheckResult check_result = label_check_stmt(
            &stmt->as.stmt_block.stmts.data[i], labels, funcname);
        if (!check_result.is_ok) {
          return check_result;
        }
      }
      break;
    }
    case STMT_IF: {
      LabelCheckResult then_result =
          label_check_stmt(stmt->as.stmt_if.then_branch, labels, funcname);
      if (!then_result.is_ok) {
        return then_result;
      }

      if (stmt->as.stmt_if.else_branch) {
        LabelCheckResult else_result =
            label_check_stmt(stmt->as.stmt_if.else_branch, labels, funcname);
        if (!else_result.is_ok) {
          return else_result;
        }
      }

      break;
    }
    case STMT_GOTO: {
      size_t len = strlen(stmt->as.stmt_goto.label) + strlen(funcname) + 1;
      char *label = malloc(len);
      snprintf(label, len + 1, "%s_%s", stmt->as.stmt_goto.label, funcname);

      bool seen = false;
      for (size_t i = 0; i < labels->count; i++) {
        if (strcmp(labels->data[i], label) == 0) {
          seen = true;
        }
      }

      if (!seen) {
        return (LabelCheckResult) {.is_ok = false,
                                   .msg = strdup("Non existent label."),
                                   .errcode = -1,
                                   .span = stmt->span};
      }
      break;
    }
    default:
      break;
  }
  return (LabelCheckResult) {
      .is_ok = true, .errcode = 0, .msg = NULL, .span = {0}};
}

LabelCheckResult label_check_program(DynArray_Stmt *ast)
{
  LabelCheckResult result = {
      .is_ok = true, .errcode = 0, .msg = NULL, .span = {0}, .time = 0.0};

  struct timespec start, end;

  clock_gettime(CLOCK_MONOTONIC, &start);

  DynArray_char_ptr labels = {0};
  for (size_t i = 0; i < ast->count; i++) {
    LabelCheckResult stmt_result =
        label_check_stmt(&ast->data[i], &labels, NULL);
    if (!stmt_result.is_ok) {
      return stmt_result;
    }
  }

  clock_gettime(CLOCK_MONOTONIC, &end);

  result.time =
      (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

  return result;
}
