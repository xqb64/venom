#include "optimizer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "dynarray.h"
#include "table.h"
#include "util.h"

static Expr constant_fold_expr(const Expr *target, bool *is_modified)
{
#define HANDLE_OPER(result_type, as_type, expr, oper, literal_kind)        \
  do {                                                                     \
    if (strcmp((expr)->op, #oper) == 0) {                                  \
      result_type folded_const = lhs.as._##as_type oper rhs.as._##as_type; \
      ExprLiteral folded_expr = {.kind = (literal_kind),                   \
                                 .as._##result_type = folded_const};       \
                                                                           \
      *is_modified = true;                                                 \
                                                                           \
      free_expr((expr)->lhs);                                              \
      free_expr((expr)->rhs);                                              \
                                                                           \
      free((expr)->lhs);                                                   \
      free((expr)->rhs);                                                   \
      free((expr)->op);                                                    \
                                                                           \
      return AS_EXPR_LITERAL(folded_expr);                                 \
    }                                                                      \
  } while (0)

#define APPLY_NUMERIC(expr)                           \
  HANDLE_OPER(double, double, (expr), +, LIT_NUMBER); \
  HANDLE_OPER(double, double, (expr), -, LIT_NUMBER); \
  HANDLE_OPER(double, double, (expr), *, LIT_NUMBER); \
  HANDLE_OPER(double, double, (expr), /, LIT_NUMBER); \
  HANDLE_OPER(bool, double, (expr), <, LIT_BOOLEAN);  \
  HANDLE_OPER(bool, double, (expr), >, LIT_BOOLEAN);  \
  HANDLE_OPER(bool, double, (expr), <=, LIT_BOOLEAN); \
  HANDLE_OPER(bool, double, (expr), >=, LIT_BOOLEAN); \
  HANDLE_OPER(bool, double, (expr), ==, LIT_BOOLEAN); \
  HANDLE_OPER(bool, double, (expr), !=, LIT_BOOLEAN);

#define APPLY_BOOLEAN(expr)                         \
  HANDLE_OPER(bool, bool, (expr), ==, LIT_BOOLEAN); \
  HANDLE_OPER(bool, bool, (expr), !=, LIT_BOOLEAN); \
  HANDLE_OPER(bool, bool, (expr), &&, LIT_BOOLEAN); \
  HANDLE_OPER(bool, bool, (expr), ||, LIT_BOOLEAN);

#define APPLY(expr)                                                    \
  do {                                                                 \
    if ((expr)->lhs->kind == EXPR_LITERAL &&                           \
        (expr)->rhs->kind == EXPR_LITERAL) {                           \
      ExprLiteral lhs = (expr)->lhs->as.expr_literal;                  \
      ExprLiteral rhs = (expr)->rhs->as.expr_literal;                  \
                                                                       \
      if (lhs.kind == LIT_NUMBER && rhs.kind == LIT_NUMBER) {          \
        APPLY_NUMERIC((expr));                                         \
      } else if (lhs.kind == LIT_BOOLEAN && rhs.kind == LIT_BOOLEAN) { \
        APPLY_BOOLEAN((expr));                                         \
      }                                                                \
    }                                                                  \
  } while (0)

  if (target->kind == EXPR_BINARY) {
    const ExprBinary *binexpr = &target->as.expr_binary;

    Expr new_lhs = constant_fold_expr(binexpr->lhs, is_modified);
    Expr new_rhs = constant_fold_expr(binexpr->rhs, is_modified);

    ExprBinary new_binexpr = {.lhs = ALLOC(new_lhs),
                              .rhs = ALLOC(new_rhs),
                              .op = own_string(binexpr->op)};

    APPLY(&new_binexpr);

    return AS_EXPR_BINARY(new_binexpr);
  } else if (target->kind == EXPR_ASSIGN) {
    const ExprAssign *assignexpr = &target->as.expr_assign;

    Expr new_rhs = constant_fold_expr(assignexpr->rhs, is_modified);

    Expr lhs = clone_expr(assignexpr->lhs);
    ExprAssign new_assignexpr = {.op = own_string(assignexpr->op),
                                 .lhs = ALLOC(lhs),
                                 .rhs = ALLOC(new_rhs)};

    return AS_EXPR_ASSIGN(new_assignexpr);
  } else if (target->kind == EXPR_CALL) {
    const ExprCall *callexpr = &target->as.expr_call;

    DynArray_Expr arguments = {0};
    for (size_t i = 0; i < callexpr->arguments.count; i++) {
      Expr *arg = &callexpr->arguments.data[i];
      Expr folded = constant_fold_expr(arg, is_modified);
      dynarray_insert(&arguments, folded);
    }

    Expr callee_expr = clone_expr(callexpr->callee);
    ExprCall new_call_expr = {.callee = ALLOC(callee_expr),
                              .arguments = arguments};

    return AS_EXPR_CALL(new_call_expr);
  } else if (target->kind == EXPR_STRUCT) {
    const ExprStruct *structexpr = &target->as.expr_struct;

    DynArray_Expr initializers = {0};
    for (size_t i = 0; i < structexpr->initializers.count; i++) {
      Expr *initializer = &structexpr->initializers.data[i];
      Expr folded = constant_fold_expr(initializer, is_modified);
      dynarray_insert(&initializers, folded);
    }

    ExprStruct new_struct_expr = {.name = own_string(structexpr->name),
                                  .initializers = initializers};

    return AS_EXPR_STRUCT(new_struct_expr);
  } else if (target->kind == EXPR_STRUCT_INITIALIZER) {
    const ExprStructInitializer *expr_struct_initializer =
        &target->as.expr_struct_initializer;

    Expr folded =
        constant_fold_expr(expr_struct_initializer->value, is_modified);
    Expr cloned_siexpr = clone_expr(expr_struct_initializer->property);
    ExprStructInitializer new_struct_init_expr = {
        .value = ALLOC(folded), .property = ALLOC(cloned_siexpr)};

    return AS_EXPR_STRUCT_INITIALIZER(new_struct_init_expr);
  } else if (target->kind == EXPR_LITERAL) {
    ExprLiteral lit_expr = clone_literal(&target->as.expr_literal);
    return AS_EXPR_LITERAL(lit_expr);
  } else if (target->kind == EXPR_VARIABLE) {
    Expr cloned_target = clone_expr(target);
    return cloned_target;
  } else {
    print_expr(target, 0);
    assert(0);
  }
#undef HANDLE_OPER
#undef APPLY_NUMERIC
#undef APPLY_BOOLEAN
#undef APPLY
}

static Stmt constant_fold_stmt(const Stmt *stmt, bool *is_modified)
{
  switch (stmt->kind) {
    case STMT_PRINT: {
      const Expr *expr = &stmt->as.stmt_print.expr;
      Expr folded = constant_fold_expr(expr, is_modified);
      StmtPrint print_stmt = {.expr = folded};
      return AS_STMT_PRINT(print_stmt);
    }
    case STMT_LET: {
      const Expr *expr = &stmt->as.stmt_let.initializer;
      Expr folded = constant_fold_expr(expr, is_modified);
      char *name = own_string(stmt->as.stmt_let.name);
      StmtLet let_stmt = {.initializer = folded, .name = name};
      return AS_STMT_LET(let_stmt);
    }
    case STMT_FN: {
      Stmt body = constant_fold_stmt(stmt->as.stmt_fn.body, is_modified);
      DynArray_char_ptr cloned_params = {0};
      for (size_t i = 0; i < stmt->as.stmt_fn.parameters.count; i++) {
        dynarray_insert(&cloned_params,
                        own_string(stmt->as.stmt_fn.parameters.data[i]));
      }
      StmtFn fn_stmt = {.body = ALLOC(body),
                        .parameters = cloned_params,
                        .name = own_string(stmt->as.stmt_fn.name)};
      return AS_STMT_FN(fn_stmt);
    }
    case STMT_IF: {
      Expr condition =
          constant_fold_expr(&stmt->as.stmt_if.condition, is_modified);
      Stmt then_branch =
          constant_fold_stmt(stmt->as.stmt_if.then_branch, is_modified);

      Stmt *else_branch = NULL;
      if (stmt->as.stmt_if.else_branch) {
        Stmt s = constant_fold_stmt(stmt->as.stmt_if.else_branch, is_modified);
        else_branch = ALLOC(s);
      }

      StmtIf if_stmt = {.condition = condition,
                        .then_branch = ALLOC(then_branch),
                        .else_branch = else_branch};

      return AS_STMT_IF(if_stmt);
    }
    case STMT_BLOCK: {
      DynArray_Stmt stmts = {0};

      for (size_t i = 0; i < stmt->as.stmt_block.stmts.count; i++) {
        Stmt folded =
            constant_fold_stmt(&stmt->as.stmt_block.stmts.data[i], is_modified);
        dynarray_insert(&stmts, folded);
      }

      StmtBlock block_stmt = {.depth = stmt->as.stmt_block.depth,
                              .stmts = stmts};

      return AS_STMT_BLOCK(block_stmt);
    }
    case STMT_ASSERT: {
      const Expr *expr = &stmt->as.stmt_assert.expr;
      Expr folded = constant_fold_expr(expr, is_modified);
      StmtAssert assert_stmt = {.expr = folded};
      return AS_STMT_ASSERT(assert_stmt);
    }
    case STMT_DECORATOR: {
      Stmt fn = constant_fold_stmt(stmt->as.stmt_decorator.fn, is_modified);
      StmtDecorator deco_stmt = {
          .fn = ALLOC(fn), .name = own_string(stmt->as.stmt_decorator.name)};
      return AS_STMT_DECORATOR(deco_stmt);
    }
    case STMT_EXPR: {
      const Expr *expr = &stmt->as.stmt_expr.expr;
      StmtExpr expr_stmt = {.expr = constant_fold_expr(expr, is_modified)};
      return AS_STMT_EXPR(expr_stmt);
    }
    case STMT_RETURN: {
      const Expr *expr = &stmt->as.stmt_return.expr;
      StmtReturn ret_stmt = {.expr = constant_fold_expr(expr, is_modified)};
      return AS_STMT_RETURN(ret_stmt);
    }
    case STMT_YIELD: {
      const Expr *expr = &stmt->as.stmt_yield.expr;
      Expr folded = constant_fold_expr(expr, is_modified);
      StmtYield yield_stmt = {.expr = folded};
      return AS_STMT_YIELD(yield_stmt);
    }
    case STMT_WHILE: {
      const Expr *expr = &stmt->as.stmt_while.condition;
      Expr folded = constant_fold_expr(expr, is_modified);
      char *label = own_string(stmt->as.stmt_while.label);
      Stmt body = constant_fold_stmt(stmt->as.stmt_while.body, is_modified);

      StmtWhile while_stmt = {
          .condition = folded, .body = ALLOC(body), .label = label};

      return AS_STMT_WHILE(while_stmt);
    }
    case STMT_FOR: {
      const Expr *init = &stmt->as.stmt_for.initializer;
      const Expr *cond = &stmt->as.stmt_for.condition;
      const Expr *advancement = &stmt->as.stmt_for.advancement;

      Expr folded_init = constant_fold_expr(init, is_modified);
      Expr folded_cond = constant_fold_expr(cond, is_modified);
      Expr folded_advancement = constant_fold_expr(advancement, is_modified);

      Stmt folded_body =
          constant_fold_stmt(stmt->as.stmt_for.body, is_modified);
      StmtFor for_stmt = {.initializer = folded_init,
                          .condition = folded_cond,
                          .advancement = folded_advancement,
                          .label = own_string(stmt->as.stmt_for.label),
                          .body = ALLOC(folded_body)};

      return AS_STMT_FOR(for_stmt);
    }
    case STMT_IMPL: {
      DynArray_Stmt folded_methods = {0};
      for (size_t i = 0; i < stmt->as.stmt_impl.methods.count; i++) {
        Stmt folded = constant_fold_stmt(&stmt->as.stmt_impl.methods.data[i],
                                         is_modified);
        dynarray_insert(&folded_methods, folded);
      }
      StmtImpl impl_stmt = {.methods = folded_methods,
                            .name = own_string(stmt->as.stmt_impl.name)};
      return AS_STMT_IMPL(impl_stmt);
    }
    case STMT_BREAK: {
      StmtBreak break_stmt = {.label = own_string(stmt->as.stmt_break.label)};
      return AS_STMT_BREAK(break_stmt);
    }
    case STMT_CONTINUE: {
      StmtContinue continue_stmt = {
          .label = own_string(stmt->as.stmt_continue.label)};
      return AS_STMT_CONTINUE(continue_stmt);
    }
    case STMT_USE: {
      StmtUse use_stmt = {.path = own_string(stmt->as.stmt_use.path)};
      return AS_STMT_USE(use_stmt);
    }
    case STMT_STRUCT: {
      DynArray_char_ptr properties = {0};
      for (size_t i = 0; i < stmt->as.stmt_struct.properties.count; i++) {
        dynarray_insert(&properties, stmt->as.stmt_struct.properties.data[i]);
      }
      StmtStruct struct_stmt = {.name = own_string(stmt->as.stmt_struct.name),
                                .properties = properties};
      return AS_STMT_STRUCT(struct_stmt);
    }
    default:
      print_stmt(stmt, 0, false);
      assert(0);
  }
}

static Expr propagate_copies_expr(const Expr *expr, Table_Expr *copies,
                                  bool *is_modified)
{
  switch (expr->kind) {
    case EXPR_LITERAL: {
      return clone_expr(expr);
    }
    case EXPR_VARIABLE: {
      char *name = expr->as.expr_variable.name;
      Expr *resolved = table_get(copies, name);
      if (resolved) {
        *is_modified = true;
        return clone_expr(resolved);
      }
      ExprVariable var_expr = {.name = own_string(name)};
      return AS_EXPR_VARIABLE(var_expr);
    }
    case EXPR_BINARY: {
      const ExprBinary *binexpr = &expr->as.expr_binary;

      Expr new_lhs =
          propagate_copies_expr(expr->as.expr_binary.lhs, copies, is_modified);
      Expr new_rhs =
          propagate_copies_expr(expr->as.expr_binary.rhs, copies, is_modified);

      ExprBinary new_binexpr = {.lhs = ALLOC(new_lhs),
                                .rhs = ALLOC(new_rhs),
                                .op = own_string(expr->as.expr_binary.op)};

      return AS_EXPR_BINARY(new_binexpr);
    }
    case EXPR_ASSIGN: {
      Expr *lhs = expr->as.expr_assign.lhs;
      if (lhs->kind == EXPR_VARIABLE) {
        table_remove(copies, lhs->as.expr_variable.name);
      }
      Expr propagated_rhs =
          propagate_copies_expr(expr->as.expr_assign.rhs, copies, is_modified);

      Expr cloned_lhs = clone_expr(lhs);
      ExprAssign assign_expr = {.lhs = ALLOC(cloned_lhs),
                                .rhs = ALLOC(propagated_rhs),
                                .op = own_string(expr->as.expr_assign.op)};
      return AS_EXPR_ASSIGN(assign_expr);
    }
    case EXPR_ARRAY: {
      DynArray_Expr elements = {0};
      for (size_t i = 0; i < expr->as.expr_array.elements.count; i++) {
        Expr propagated = propagate_copies_expr(
            &expr->as.expr_array.elements.data[i], copies, is_modified);
        dynarray_insert(&elements, propagated);
      }
      ExprArray array_expr = {.elements = elements};
      return AS_EXPR_ARRAY(array_expr);
    }
    case EXPR_GET: {
      Expr propagated =
          propagate_copies_expr(expr->as.expr_get.expr, copies, is_modified);
      ExprGet get_expr = {
          .op = own_string(expr->as.expr_get.op),
          .property_name = own_string(expr->as.expr_get.property_name),
          .expr = ALLOC(propagated)};
      return AS_EXPR_GET(get_expr);
    }
    case EXPR_UNARY: {
      Expr propagated =
          propagate_copies_expr(expr->as.expr_unary.expr, copies, is_modified);
      ExprUnary unary_expr = {.op = own_string(expr->as.expr_unary.op),
                              .expr = ALLOC(propagated)};
      return AS_EXPR_UNARY(unary_expr);
    }
    case EXPR_SUBSCRIPT: {
      Expr propagated = propagate_copies_expr(expr->as.expr_subscript.index,
                                              copies, is_modified);

      Expr cloned_expr = clone_expr(expr->as.expr_subscript.expr);
      ExprSubscript subscript_expr = {.index = ALLOC(propagated),
                                      .expr = ALLOC(cloned_expr)};
      return AS_EXPR_SUBSCRIPT(subscript_expr);
    }
    case EXPR_STRUCT: {
      DynArray_Expr initializers = {0};
      for (size_t i = 0; i < expr->as.expr_struct.initializers.count; i++) {
        Expr propagated = propagate_copies_expr(
            &expr->as.expr_struct.initializers.data[i], copies, is_modified);
        dynarray_insert(&initializers, propagated);
      }
      ExprStruct struct_expr = {.name = own_string(expr->as.expr_struct.name),
                                .initializers = initializers};
      return AS_EXPR_STRUCT(struct_expr);
    }
    case EXPR_STRUCT_INITIALIZER: {
      Expr propagated = propagate_copies_expr(
          expr->as.expr_struct_initializer.value, copies, is_modified);
      Expr cloned_expr = clone_expr(expr->as.expr_struct_initializer.property);
      ExprStructInitializer struct_init_expr = {.value = ALLOC(propagated),
                                                .property = ALLOC(cloned_expr)};
      return AS_EXPR_STRUCT_INITIALIZER(struct_init_expr);
    }
    case EXPR_CALL: {
      DynArray_Expr propagated_args = {0};
      for (size_t i = 0; i < expr->as.expr_call.arguments.count; i++) {
        Expr propagated_arg = propagate_copies_expr(
            &expr->as.expr_call.arguments.data[i], copies, is_modified);
        dynarray_insert(&propagated_args, propagated_arg);
      }
      Expr cloned_callee = clone_expr(expr->as.expr_call.callee);
      ExprCall call_expr = {.callee = ALLOC(cloned_callee),
                            .arguments = propagated_args};
      return AS_EXPR_CALL(call_expr);
    }
    default:
      print_expr(expr, 0);
      assert(0);
  }
}

static Stmt propagate_copies_stmt(const Stmt *stmt, Table_Expr *copies,
                                  bool *is_modified)
{
  switch (stmt->kind) {
    case STMT_LET: {
      const Expr *init = &stmt->as.stmt_let.initializer;

      table_remove(copies, stmt->as.stmt_let.name);

      if (init->kind == EXPR_VARIABLE || init->kind == EXPR_LITERAL) {
        table_insert(copies, stmt->as.stmt_let.name, clone_expr(init));
      }

      Expr optimized = propagate_copies_expr(init, copies, is_modified);

      StmtLet let_stmt = {.name = own_string(stmt->as.stmt_let.name),
                          .initializer = optimized};

      return AS_STMT_LET(let_stmt);
    }
    case STMT_PRINT: {
      Expr expr =
          propagate_copies_expr(&stmt->as.stmt_print.expr, copies, is_modified);

      StmtPrint print_stmt = {.expr = expr};
      return AS_STMT_PRINT(print_stmt);
    }
    case STMT_FN: {
      Table_Expr cloned_copies = clone_table_expr(copies);
      Stmt body = propagate_copies_stmt(stmt->as.stmt_fn.body, &cloned_copies,
                                        is_modified);
      DynArray_char_ptr cloned_params = {0};
      for (size_t i = 0; i < stmt->as.stmt_fn.parameters.count; i++) {
        dynarray_insert(&cloned_params,
                        own_string(stmt->as.stmt_fn.parameters.data[i]));
      }
      StmtFn fn_stmt = {.body = ALLOC(body),
                        .parameters = cloned_params,
                        .name = own_string(stmt->as.stmt_fn.name)};
      free_table_expr(&cloned_copies);
      return AS_STMT_FN(fn_stmt);
    }
    case STMT_BLOCK: {
      Table_Expr cloned_copies = clone_table_expr(copies);
      DynArray_Stmt optimized = {0};
      for (size_t i = 0; i < stmt->as.stmt_block.stmts.count; i++) {
        Stmt s = propagate_copies_stmt(&stmt->as.stmt_block.stmts.data[i],
                                       &cloned_copies, is_modified);

        dynarray_insert(&optimized, s);
      }
      StmtBlock block_stmt = {.stmts = optimized,
                              .depth = stmt->as.stmt_block.depth};
      free_table_expr(&cloned_copies);

      return AS_STMT_BLOCK(block_stmt);
    }
    case STMT_ASSERT: {
      Expr propagated = propagate_copies_expr(&stmt->as.stmt_assert.expr,
                                              copies, is_modified);
      StmtAssert assert_stmt = {.expr = propagated};
      return AS_STMT_ASSERT(assert_stmt);
    }
    case STMT_DECORATOR: {
      Stmt propagated = propagate_copies_stmt(stmt->as.stmt_decorator.fn,
                                              copies, is_modified);
      StmtDecorator deco_stmt = {
          .name = own_string(stmt->as.stmt_decorator.name),
          .fn = ALLOC(propagated)};
      return AS_STMT_DECORATOR(deco_stmt);
    }
    case STMT_EXPR: {
      Expr propagated =
          propagate_copies_expr(&stmt->as.stmt_expr.expr, copies, is_modified);
      StmtExpr expr_stmt = {.expr = propagated};
      return AS_STMT_EXPR(expr_stmt);
      break;
    }
    case STMT_FOR: {
      Expr propagated_init = propagate_copies_expr(
          &stmt->as.stmt_for.initializer, copies, is_modified);
      Expr propagated_condition = propagate_copies_expr(
          &stmt->as.stmt_for.condition, copies, is_modified);
      Expr propagated_advancement = propagate_copies_expr(
          &stmt->as.stmt_for.advancement, copies, is_modified);

      Table_Expr cloned_copies = clone_table_expr(copies);
      
      Stmt propagated_body =
          propagate_copies_stmt(stmt->as.stmt_for.body, &cloned_copies, is_modified);

      StmtFor for_stmt = {.initializer = propagated_init,
                          .condition = propagated_condition,
                          .advancement = propagated_advancement,
                          .body = ALLOC(propagated_body),
                          .label = own_string(stmt->as.stmt_for.label)};

      free_table_expr(&cloned_copies);

      return AS_STMT_FOR(for_stmt);
    }
    case STMT_WHILE: {
      Expr expr = propagate_copies_expr(&stmt->as.stmt_while.condition, copies,
                                        is_modified);

      Table_Expr cloned_copies = clone_table_expr(copies);

      Stmt body = propagate_copies_stmt(stmt->as.stmt_while.body,
                                        &cloned_copies, is_modified);

      StmtWhile while_stmt = {.body = ALLOC(body),
                              .condition = expr,
                              .label = own_string(stmt->as.stmt_while.label)};

      free_table_expr(&cloned_copies);
      return AS_STMT_WHILE(while_stmt);
    }
    case STMT_IF: {
      Expr propagated_condition = propagate_copies_expr(
          &stmt->as.stmt_if.condition, copies, is_modified);
      Stmt propagated_then = propagate_copies_stmt(stmt->as.stmt_if.then_branch,
                                                   copies, is_modified);

      Stmt *propagated_else = NULL;
      if (stmt->as.stmt_if.else_branch) {
        Stmt s = propagate_copies_stmt(stmt->as.stmt_if.else_branch, copies,
                                       is_modified);
        propagated_else = ALLOC(s);
      }

      StmtIf if_stmt = {.condition = propagated_condition,
                        .then_branch = ALLOC(propagated_then),
                        .else_branch = propagated_else};

      return AS_STMT_IF(if_stmt);
    }
    case STMT_IMPL: {
      DynArray_Stmt methods = {0};
      for (size_t i = 0; i < stmt->as.stmt_impl.methods.count; i++) {
        Stmt propagated = propagate_copies_stmt(
            &stmt->as.stmt_impl.methods.data[i], copies, is_modified);
        dynarray_insert(&methods, propagated);
      }

      StmtImpl impl_stmt = {.name = own_string(stmt->as.stmt_impl.name),
                            .methods = methods};
      return AS_STMT_IMPL(impl_stmt);
    }
    case STMT_RETURN: {
      Expr propagated = propagate_copies_expr(&stmt->as.stmt_return.expr,
                                              copies, is_modified);
      StmtReturn ret_stmt = {.expr = propagated};
      return AS_STMT_RETURN(ret_stmt);
    }
    case STMT_BREAK: {
      StmtBreak break_stmt = {.label = own_string(stmt->as.stmt_break.label)};
      return AS_STMT_BREAK(break_stmt);
    }
    case STMT_CONTINUE: {
      StmtContinue continue_stmt = {
          .label = own_string(stmt->as.stmt_continue.label)};
      return AS_STMT_CONTINUE(continue_stmt);
    }
    default:
      print_stmt(stmt, 0, false);
      assert(0);
  }
}

bool stmt_may_continue(Stmt *stmt)
{
  switch (stmt->kind) {
    case STMT_RETURN:
    case STMT_CONTINUE:
    case STMT_BREAK: {
      return false;
    }
    case STMT_IF: {
      bool then_condition = stmt_may_continue(stmt->as.stmt_if.then_branch);
      bool else_condition =
          stmt->as.stmt_if.else_branch
              ? stmt_may_continue(stmt->as.stmt_if.else_branch)
              : true;
      return then_condition || else_condition;
    }
    case STMT_BLOCK: {
      for (size_t i = 0; i < stmt->as.stmt_block.stmts.count; i++) {
        if (!stmt_may_continue(&stmt->as.stmt_block.stmts.data[i])) {
          return false;
        }
      }
      return true;
    }
    default:
      return true;
  }
}

Stmt eliminate_unreachable_stmt(Stmt *stmt, bool *is_modified)
{
  switch (stmt->kind) {
    case STMT_FN: {
      Stmt body =
          eliminate_unreachable_stmt(stmt->as.stmt_fn.body, is_modified);
      char *name = own_string(stmt->as.stmt_fn.name);
      DynArray_char_ptr params = {0};
      for (size_t i = 0; i < stmt->as.stmt_fn.parameters.count; i++) {
        dynarray_insert(&params,
                        own_string(stmt->as.stmt_fn.parameters.data[i]));
      }
      StmtFn stmt_fn = {
          .name = name, .parameters = params, .body = ALLOC(body)};
      return AS_STMT_FN(stmt_fn);
    }
    case STMT_BLOCK: {
      DynArray_Stmt stmts = {0};
      bool reachable = true;
      for (size_t i = 0; i < stmt->as.stmt_block.stmts.count; i++) {
        if (!reachable) {
          *is_modified = true;
          continue;
        }
        Stmt optimized = eliminate_unreachable_stmt(
            &stmt->as.stmt_block.stmts.data[i], is_modified);
        if (optimized.kind == STMT_BLOCK &&
            optimized.as.stmt_block.stmts.count == 0) {
          continue;
        }
        if (!stmt_may_continue(&optimized)) {
          reachable = false;
        }
        dynarray_insert(&stmts, optimized);
      }
      StmtBlock stmt_block = {.stmts = stmts,
                              .depth = stmt->as.stmt_block.depth};
      return AS_STMT_BLOCK(stmt_block);
    }
    case STMT_IF: {
      Expr *condition = &stmt->as.stmt_if.condition;
      if (condition->kind == EXPR_LITERAL &&
          condition->as.expr_literal.kind == LIT_BOOLEAN &&
          !condition->as.expr_literal.as._bool) {
        DynArray_Stmt stmts = {0};
        StmtBlock empty = {.stmts = stmts, .depth = 0};
        return AS_STMT_BLOCK(empty);
      }
      Stmt then_branch =
          eliminate_unreachable_stmt(stmt->as.stmt_if.then_branch, is_modified);
      Stmt *else_branch = NULL;
      if (stmt->as.stmt_if.else_branch) {
        Stmt eliminated = eliminate_unreachable_stmt(
            stmt->as.stmt_if.else_branch, is_modified);
        else_branch = ALLOC(eliminated);
      }
      StmtIf stmt_if = {.condition = clone_expr(&stmt->as.stmt_if.condition),
                        .then_branch = ALLOC(then_branch),
                        .else_branch = else_branch};
      return AS_STMT_IF(stmt_if);
    }
    case STMT_WHILE: {
      Expr *condition = &stmt->as.stmt_while.condition;
      if (condition->kind == EXPR_LITERAL &&
          condition->as.expr_literal.kind == LIT_BOOLEAN &&
          !condition->as.expr_literal.as._bool) {
        DynArray_Stmt stmts = {0};
        StmtBlock empty = {.stmts = stmts, .depth = 0};
        return AS_STMT_BLOCK(empty);
      }
      Stmt body =
          eliminate_unreachable_stmt(stmt->as.stmt_while.body, is_modified);
      StmtWhile stmt_while = {
          .label = own_string(stmt->as.stmt_while.label),
          .condition = clone_expr(&stmt->as.stmt_while.condition),
          .body = ALLOC(body)};
      return AS_STMT_WHILE(stmt_while);
    }
    case STMT_FOR: {
      Expr *condition = &stmt->as.stmt_for.condition;
      if (condition->kind == EXPR_LITERAL &&
          condition->as.expr_literal.kind == LIT_BOOLEAN &&
          !condition->as.expr_literal.as._bool) {
        DynArray_Stmt stmts = {0};
        StmtBlock empty = {.stmts = stmts, .depth = 0};
        return AS_STMT_BLOCK(empty);
      }
      Stmt body = eliminate_unreachable_stmt(stmt->as.stmt_for.body, is_modified);
      StmtFor stmt_for = {
        .label = own_string(stmt->as.stmt_for.label),
        .initializer = clone_expr(&stmt->as.stmt_for.initializer),
        .condition = clone_expr(&stmt->as.stmt_for.condition),
        .advancement = clone_expr(&stmt->as.stmt_for.advancement),
        .body = ALLOC(body),
      };
      return AS_STMT_FOR(stmt_for);
    }
    default:
      return clone_stmt(stmt);
  }
}

static bool is_equal_expr(const Expr *a, const Expr *b);

static bool is_equal_expr_list(const DynArray_Expr *a, const DynArray_Expr *b) {
  if (a->count != b->count) return false;
  for (size_t i = 0; i < a->count; ++i) {
    if (!is_equal_expr(&a->data[i], &b->data[i])) return false;
  }
  return true;
}

static bool is_equal_expr(const Expr *a, const Expr *b) {
  if (a == b) return true;
  if (!a || !b) return false;
  if (a->kind != b->kind) return false;

  switch (a->kind) {

    case EXPR_LITERAL:
      if (a->as.expr_literal.kind != b->as.expr_literal.kind) return false;
      switch (a->as.expr_literal.kind) {
        case LIT_BOOLEAN:
          return a->as.expr_literal.as._bool == b->as.expr_literal.as._bool;
        case LIT_NUMBER:
          return a->as.expr_literal.as._double == b->as.expr_literal.as._double;
        case LIT_STRING:
          return strcmp(a->as.expr_literal.as.str, b->as.expr_literal.as.str) == 0;
        case LIT_NULL:
          return true;
        default:
          assert(0);
      }
      break;

    case EXPR_VARIABLE:
      return strcmp(a->as.expr_variable.name, b->as.expr_variable.name) == 0;

    case EXPR_UNARY:
      return strcmp(a->as.expr_unary.op, b->as.expr_unary.op) == 0 &&
             is_equal_expr(a->as.expr_unary.expr, b->as.expr_unary.expr);

    case EXPR_BINARY:
      return strcmp(a->as.expr_binary.op, b->as.expr_binary.op) == 0 &&
             is_equal_expr(a->as.expr_binary.lhs, b->as.expr_binary.lhs) &&
             is_equal_expr(a->as.expr_binary.rhs, b->as.expr_binary.rhs);

    case EXPR_CALL:
      return is_equal_expr(a->as.expr_call.callee, b->as.expr_call.callee) &&
             is_equal_expr_list(&a->as.expr_call.arguments, &b->as.expr_call.arguments);

    case EXPR_GET:
      return strcmp(a->as.expr_get.property_name, b->as.expr_get.property_name) == 0 &&
             strcmp(a->as.expr_get.op, b->as.expr_get.op) == 0 &&
             is_equal_expr(a->as.expr_get.expr, b->as.expr_get.expr);

    case EXPR_ASSIGN:
      return strcmp(a->as.expr_assign.op, b->as.expr_assign.op) == 0 &&
             is_equal_expr(a->as.expr_assign.lhs, b->as.expr_assign.lhs) &&
             is_equal_expr(a->as.expr_assign.rhs, b->as.expr_assign.rhs);

    case EXPR_STRUCT:
      return strcmp(a->as.expr_struct.name, b->as.expr_struct.name) == 0 &&
             is_equal_expr_list(&a->as.expr_struct.initializers, &b->as.expr_struct.initializers);

    case EXPR_STRUCT_INITIALIZER:
      return is_equal_expr(a->as.expr_struct_initializer.property, b->as.expr_struct_initializer.property) &&
             is_equal_expr(a->as.expr_struct_initializer.value, b->as.expr_struct_initializer.value);

    case EXPR_ARRAY:
      return is_equal_expr_list(&a->as.expr_array.elements, &b->as.expr_array.elements);

    case EXPR_SUBSCRIPT:
      return is_equal_expr(a->as.expr_subscript.expr, b->as.expr_subscript.expr) &&
             is_equal_expr(a->as.expr_subscript.index, b->as.expr_subscript.index);

    default:
      assert(0);
  }

  return false;
}
static bool liveset_contains(DynArray_Expr *live, Expr *item) {
  for (size_t i = 0; i < live->count; i++) {
   if (is_equal_expr(&live->data[i], item)) {
      return true;
    }
  }
  return false;
}

static void collect_live_expr(DynArray_Expr *live, Expr *expr)
{
  switch (expr->kind) {
    case EXPR_VARIABLE: {
      dynarray_insert(live, clone_expr(expr));
      break;
    }
    case EXPR_BINARY: {
      collect_live_expr(live, expr->as.expr_binary.lhs);
      collect_live_expr(live, expr->as.expr_binary.rhs);
      break;
    }
    case EXPR_UNARY: {
      collect_live_expr(live, expr->as.expr_unary.expr);
      break;
    }
    case EXPR_ARRAY: {
      for (size_t i = 0; i < expr->as.expr_array.elements.count; i++) {
        collect_live_expr(live, &expr->as.expr_array.elements.data[i]);
      }
      break;
    }
    case EXPR_ASSIGN: {
      collect_live_expr(live, expr->as.expr_assign.rhs);
      break;
    }
    case EXPR_GET: {
      collect_live_expr(live, expr->as.expr_get.expr);
      break;
    }
    case EXPR_STRUCT: {
      for (size_t i = 0; i < expr->as.expr_struct.initializers.count; i++) {
        collect_live_expr(live, &expr->as.expr_struct.initializers.data[i]);
      }
      break;
    }
    case EXPR_STRUCT_INITIALIZER: {
      collect_live_expr(live, expr->as.expr_struct_initializer.property);
      collect_live_expr(live, expr->as.expr_struct_initializer.value);
      break;
    }
    case EXPR_SUBSCRIPT: {
      collect_live_expr(live, expr->as.expr_subscript.expr);
      collect_live_expr(live, expr->as.expr_subscript.index);
      break;
    }
    case EXPR_CALL: {
      for (size_t i = 0; i < expr->as.expr_call.arguments.count; i++) {
        collect_live_expr(live, &expr->as.expr_call.arguments.data[i]);
      }
      collect_live_expr(live, expr->as.expr_call.callee);
      break;
    }
    default:
      break;
  }
}

static void collect_live_stmt(DynArray_Expr *live, Stmt *stmt)
{
  switch (stmt->kind) {
    case STMT_FN: {
      collect_live_stmt(live, stmt->as.stmt_fn.body);
      break;
    }
    case STMT_BLOCK: {
      for (size_t i = 0; i < stmt->as.stmt_block.stmts.count; i++) {
        collect_live_stmt(live, &stmt->as.stmt_block.stmts.data[i]);
      }
      break;
    }
    case STMT_LET: {
      collect_live_expr(live, &stmt->as.stmt_let.initializer);
      break;
    }
    case STMT_PRINT: {
      collect_live_expr(live, &stmt->as.stmt_print.expr);
      break;
    }
    case STMT_ASSERT: {
      collect_live_expr(live, &stmt->as.stmt_assert.expr);
      break;
    }
    case STMT_EXPR: {
      collect_live_expr(live, &stmt->as.stmt_expr.expr);
      break;
    }
    case STMT_RETURN: {
      collect_live_expr(live, &stmt->as.stmt_return.expr);
      break;
    }
    case STMT_IMPL: {
      for (size_t i = 0; i < stmt->as.stmt_impl.methods.count; i++) {
        collect_live_stmt(live, &stmt->as.stmt_impl.methods.data[i]);
      }
      break;
    }
    case STMT_DECORATOR: {
      collect_live_stmt(live, stmt->as.stmt_decorator.fn);
      break;
    }
    case STMT_WHILE: {
      collect_live_expr(live, &stmt->as.stmt_while.condition);
      collect_live_stmt(live, stmt->as.stmt_while.body);
      break;
    }
    case STMT_FOR: {
      collect_live_expr(live, &stmt->as.stmt_for.initializer);
      collect_live_expr(live, &stmt->as.stmt_for.condition);
      collect_live_expr(live, &stmt->as.stmt_for.advancement);
      collect_live_stmt(live, stmt->as.stmt_for.body);
      break;
    }
    case STMT_IF: {
      collect_live_expr(live, &stmt->as.stmt_if.condition);
      collect_live_stmt(live, stmt->as.stmt_if.then_branch);
      if (stmt->as.stmt_if.else_branch) {
        collect_live_stmt(live, stmt->as.stmt_if.else_branch);
      }
      break;
    }
    case STMT_YIELD: {
      collect_live_expr(live, &stmt->as.stmt_yield.expr);
      break;
    }
    default:
      break;
  }
}

void liveset_remove(DynArray_Expr *live, Expr *expr) {
  for (size_t i = 0; i < live->count; ++i) {
    if (is_equal_expr(&live->data[i], expr)) {
      free_expr(&live->data[i]);
      live->data[i] = live->data[--live->count];
      break;
    }
  }
}

DynArray_Stmt eliminate_dead_store_block(DynArray_Stmt *stmts, bool *is_modified) {
  DynArray_Stmt result = {0};
  DynArray_Expr live = {0};
  for (int i = (int)stmts->count - 1; i >= 0; i--) {
    Stmt *stmt = &stmts->data[i];
    bool keep = true;
    switch (stmt->kind) {
      case STMT_EXPR: {
        Expr *expr = &stmt->as.stmt_expr.expr;
        if (expr->kind == EXPR_ASSIGN) {
          Expr *lhs = expr->as.expr_assign.lhs;
          Expr *rhs = expr->as.expr_assign.rhs;
          if (!liveset_contains(&live, lhs)) {
            keep = false;
            *is_modified = true;
          } else {
            collect_live_expr(&live, rhs);
            liveset_remove(&live, lhs);
          }
        } else {
          collect_live_expr(&live, expr);
        }
        break;
      }
      case STMT_BLOCK: {
        DynArray_Stmt cleaned_block = eliminate_dead_store_block(&stmt->as.stmt_block.stmts, is_modified);
        if (cleaned_block.count == 0) {
          keep = false;
        } else {
          stmt->as.stmt_block.stmts = cleaned_block;
        }
        break;
      }
      default: {
        collect_live_stmt(&live, stmt);
        break;
      }
    }
    if (keep) {
      dynarray_insert(&result, clone_stmt(stmt));
    }
  }
  for (size_t i = 0, j = result.count - 1; i < j; i++, j--) {
    Stmt temp = result.data[i];
    result.data[i] = result.data[j];
    result.data[j] = temp;
  }
  dynarray_free(&live);
  return result;
}

Stmt eliminate_dead_store_stmt(Stmt *stmt, bool *is_modified)
{
  switch (stmt->kind) {
    case STMT_BLOCK: {
      DynArray_Stmt eliminated = eliminate_dead_store_block(&stmt->as.stmt_block.stmts, is_modified);
      StmtBlock stmt_block = {.stmts = eliminated, .depth = stmt->as.stmt_block.depth};
      return AS_STMT_BLOCK(stmt_block);
    }
    case STMT_FN: {
      DynArray_char_ptr parameters = {0};
      for (size_t i = 0; i < stmt->as.stmt_fn.parameters.count; i++) {
        dynarray_insert(&parameters, stmt->as.stmt_fn.parameters.data[i]);
      }
      Stmt body = eliminate_dead_store_stmt(stmt->as.stmt_fn.body, is_modified);
      StmtFn stmt_fn = {.body = ALLOC(body), .name = own_string(stmt->as.stmt_fn.name), .parameters = parameters};
      return AS_STMT_FN(stmt_fn);
    }
    case STMT_WHILE: {
      Stmt body = eliminate_dead_store_stmt(stmt->as.stmt_while.body, is_modified);
      Expr condition = clone_expr(&stmt->as.stmt_while.condition);
      char *label = own_string(stmt->as.stmt_while.label);
      StmtWhile stmt_while = {.label = label, .condition = condition, .body = ALLOC(body)};
      return AS_STMT_WHILE(stmt_while);
    }
    case STMT_FOR: {
      Expr initializer = clone_expr(&stmt->as.stmt_for.initializer);
      Expr condition = clone_expr(&stmt->as.stmt_for.condition);
      Expr advancement = clone_expr(&stmt->as.stmt_for.advancement);
      char *label = own_string(stmt->as.stmt_for.label);
      Stmt body = eliminate_dead_store_stmt(stmt->as.stmt_for.body, is_modified);
      StmtFor stmt_for = {.initializer = initializer, .condition = condition, .advancement = advancement, .label = label, .body = ALLOC(body)};
      return AS_STMT_FOR(stmt_for);
    }
    case STMT_DECORATOR: {
      Stmt fn = eliminate_dead_store_stmt(stmt->as.stmt_decorator.fn, is_modified);
      char *name = own_string(stmt->as.stmt_decorator.name);
      StmtDecorator stmt_decorator = {.name = name, .fn = ALLOC(fn)};
      return AS_STMT_DECORATOR(stmt_decorator);
    }
    case STMT_IMPL: {
      DynArray_Stmt methods = {0};
      for (size_t i = 0; i < stmt->as.stmt_impl.methods.count; i++) {
        Stmt eliminated = eliminate_dead_store_stmt(&stmt->as.stmt_impl.methods.data[i], is_modified);
      }
      char *name = own_string(stmt->as.stmt_impl.name);
      StmtImpl stmt_impl = {.name = name, .methods = methods};
      return AS_STMT_IMPL(stmt_impl);
    }
    case STMT_IF: {
      Expr condition = clone_expr(&stmt->as.stmt_if.condition);
      Stmt then_branch = eliminate_dead_store_stmt(stmt->as.stmt_if.then_branch, is_modified);
      Stmt *else_branch = NULL;
      if (stmt->as.stmt_if.else_branch) {
        Stmt eliminated_else = eliminate_dead_store_stmt(stmt->as.stmt_if.else_branch, is_modified);
        else_branch = ALLOC(eliminated_else);
      }
      StmtIf stmt_if = {.condition = condition, .then_branch = ALLOC(then_branch), .else_branch = else_branch};
      return AS_STMT_IF(stmt_if);
    }
    default:
      return clone_stmt(stmt);
  }
}

DynArray_Stmt optimize(const DynArray_Stmt *ast)
{
  bool is_modified;
  DynArray_Stmt original = clone_ast(ast);

  do {
    is_modified = false;
    Table_Expr copies = {0};

    DynArray_Stmt optimized_ast = {0};
    for (size_t i = 0; i < original.count; i++) {
      Stmt folded = constant_fold_stmt(&original.data[i], &is_modified);
      Stmt propagated = propagate_copies_stmt(&folded, &copies, &is_modified);
      Stmt unreachable_eliminated = eliminate_unreachable_stmt(&propagated, &is_modified);
      Stmt dead_store_eliminated = eliminate_dead_store_stmt(&unreachable_eliminated, &is_modified);
      dynarray_insert(&optimized_ast, dead_store_eliminated);
      free_stmt(&folded);
      free_stmt(&propagated);
      free_stmt(&unreachable_eliminated);
    }

    free_table_expr(&copies);
    free_ast(&original);

    original = clone_ast(&optimized_ast);

    free_ast(&optimized_ast);
  } while (is_modified);

  return original;
}
