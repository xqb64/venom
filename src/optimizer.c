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
#define HANDLE_OPER(result_type, as_type, expr, oper, literal_kind)                            \
    do                                                                                         \
    {                                                                                          \
        if (strcmp((expr)->op, #oper) == 0)                                                    \
        {                                                                                      \
            result_type folded_const = lhs.as._##as_type oper rhs.as._##as_type;               \
            ExprLit folded_expr = {.kind = (literal_kind), .as._##result_type = folded_const}; \
                                                                                               \
            *is_modified = true;                                                               \
                                                                                               \
            free_expression((expr)->lhs);                                                      \
            free_expression((expr)->rhs);                                                      \
                                                                                               \
            free((expr)->lhs);                                                                 \
            free((expr)->rhs);                                                                 \
            free((expr)->op);                                                                  \
                                                                                               \
            return AS_EXPR_LIT(folded_expr);                                                   \
        }                                                                                      \
    } while (0)

#define APPLY_NUMERIC(expr)                          \
    HANDLE_OPER(double, double, (expr), +, LIT_NUM); \
    HANDLE_OPER(double, double, (expr), -, LIT_NUM); \
    HANDLE_OPER(double, double, (expr), *, LIT_NUM); \
    HANDLE_OPER(double, double, (expr), /, LIT_NUM); \
    HANDLE_OPER(bool, double, (expr), <, LIT_BOOL);  \
    HANDLE_OPER(bool, double, (expr), >, LIT_BOOL);  \
    HANDLE_OPER(bool, double, (expr), <=, LIT_BOOL); \
    HANDLE_OPER(bool, double, (expr), >=, LIT_BOOL); \
    HANDLE_OPER(bool, double, (expr), ==, LIT_BOOL); \
    HANDLE_OPER(bool, double, (expr), !=, LIT_BOOL);

#define APPLY_BOOLEAN(expr)                        \
    HANDLE_OPER(bool, bool, (expr), ==, LIT_BOOL); \
    HANDLE_OPER(bool, bool, (expr), !=, LIT_BOOL); \
    HANDLE_OPER(bool, bool, (expr), &&, LIT_BOOL); \
    HANDLE_OPER(bool, bool, (expr), ||, LIT_BOOL);

#define APPLY(expr)                                                         \
    do                                                                      \
    {                                                                       \
        if ((expr)->lhs->kind == EXPR_LIT && (expr)->rhs->kind == EXPR_LIT) \
        {                                                                   \
            ExprLit lhs = (expr)->lhs->as.expr_lit;                         \
            ExprLit rhs = (expr)->rhs->as.expr_lit;                         \
                                                                            \
            if (lhs.kind == LIT_NUM && rhs.kind == LIT_NUM)                 \
            {                                                               \
                APPLY_NUMERIC((expr));                                      \
            }                                                               \
            else if (lhs.kind == LIT_BOOL && rhs.kind == LIT_BOOL)          \
            {                                                               \
                APPLY_BOOLEAN((expr));                                      \
            }                                                               \
        }                                                                   \
    } while (0)

    if (target->kind == EXPR_BIN)
    {
        const ExprBin *binexpr = &target->as.expr_bin;

        Expr new_lhs = constant_fold_expr(binexpr->lhs, is_modified);
        Expr new_rhs = constant_fold_expr(binexpr->rhs, is_modified);

        ExprBin new_binexpr = {
            .lhs = ALLOC(new_lhs), .rhs = ALLOC(new_rhs), .op = own_string(binexpr->op)};

        APPLY(&new_binexpr);

        return AS_EXPR_BIN(new_binexpr);
    }
    else if (target->kind == EXPR_ASSIGN)
    {
        const ExprAssign *assignexpr = &target->as.expr_assign;

        Expr new_rhs = constant_fold_expr(assignexpr->rhs, is_modified);

        Expr lhs = clone_expr(assignexpr->lhs);
        ExprAssign new_assignexpr = {
            .op = own_string(assignexpr->op), .lhs = ALLOC(lhs), .rhs = ALLOC(new_rhs)};

        return AS_EXPR_ASSIGN(new_assignexpr);
    }
    else if (target->kind == EXPR_CALL)
    {
        const ExprCall *callexpr = &target->as.expr_call;

        DynArray_Expr arguments = {0};
        for (size_t i = 0; i < callexpr->arguments.count; i++)
        {
            Expr *arg = &callexpr->arguments.data[i];
            Expr folded = constant_fold_expr(arg, is_modified);
            dynarray_insert(&arguments, folded);
        }

        Expr callee_expr = clone_expr(callexpr->callee);
        ExprCall new_call_expr = {.callee = ALLOC(callee_expr), .arguments = arguments};

        return AS_EXPR_CALL(new_call_expr);
    }
    else if (target->kind == EXPR_STRUCT)
    {
        const ExprStruct *structexpr = &target->as.expr_struct;

        DynArray_Expr initializers = {0};
        for (size_t i = 0; i < structexpr->initializers.count; i++)
        {
            Expr *initializer = &structexpr->initializers.data[i];
            Expr folded = constant_fold_expr(initializer, is_modified);
            dynarray_insert(&initializers, folded);
        }

        ExprStruct new_struct_expr = {.name = own_string(structexpr->name),
                                      .initializers = initializers};

        return AS_EXPR_STRUCT(new_struct_expr);
    }
    else if (target->kind == EXPR_STRUCT_INIT)
    {
        const ExprStructInit *structinitexpr = &target->as.expr_struct_init;

        Expr folded = constant_fold_expr(structinitexpr->value, is_modified);
        Expr structinit_expr = clone_expr(structinitexpr->property);
        ExprStructInit new_struct_init_expr = {.value = ALLOC(folded),
                                               .property = ALLOC(structinit_expr)};

        return AS_EXPR_STRUCT_INIT(new_struct_init_expr);
    }
    else if (target->kind == EXPR_LIT)
    {
        ExprLit lit_expr = clone_literal(&target->as.expr_lit);
        return AS_EXPR_LIT(lit_expr);
    }
    else if (target->kind == EXPR_VAR)
    {
        Expr cloned_target = clone_expr(target);
        return cloned_target;
    }
    else
    {
        print_expression(target, 0);
        assert(0);
    }
#undef HANDLE_OPER
#undef APPLY_NUMERIC
#undef APPLY_BOOLEAN
#undef APPLY
}

static Stmt constant_fold_stmt(const Stmt *stmt, bool *is_modified)
{
    switch (stmt->kind)
    {
        case STMT_PRINT: {
            const Expr *expr = &stmt->as.stmt_print.expr;
            Expr folded = constant_fold_expr(expr, is_modified);
            StmtPrint print_stmt = {.expr = folded};
            return AS_STMT_PRINT(print_stmt);
        }
        case STMT_LET: {
            Expr expr = clone_expr(&stmt->as.stmt_let.initializer);
            Expr folded = constant_fold_expr(&expr, is_modified);
            char *name = own_string(stmt->as.stmt_let.name);
            StmtLet let_stmt = {.initializer = folded, .name = name};
            return AS_STMT_LET(let_stmt);
        }
        case STMT_FN: {
            Stmt body = constant_fold_stmt(stmt->as.stmt_fn.body, is_modified);
            StmtFn fn_stmt = {.body = ALLOC(body), .name = own_string(stmt->as.stmt_fn.name)};
            return AS_STMT_FN(fn_stmt);
        }
        case STMT_IF: {
            Expr condition = constant_fold_expr(&stmt->as.stmt_if.condition, is_modified);
            Stmt then_branch = constant_fold_stmt(stmt->as.stmt_if.then_branch, is_modified);

            Stmt *else_branch = NULL;
            if (stmt->as.stmt_if.else_branch)
            {
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

            for (size_t i = 0; i < stmt->as.stmt_block.stmts.count; i++)
            {
                Stmt folded = constant_fold_stmt(&stmt->as.stmt_block.stmts.data[i], is_modified);
                dynarray_insert(&stmts, folded);
            }

            StmtBlock block_stmt = {.depth = stmt->as.stmt_block.depth, .stmts = stmts};

            return AS_STMT_BLOCK(block_stmt);
        }
        case STMT_ASSERT: {
            const Expr *expr = &stmt->as.stmt_assert.expr;
            Expr folded = constant_fold_expr(expr, is_modified);
            StmtAssert assert_stmt = {.expr = folded};
            return AS_STMT_ASSERT(assert_stmt);
        }
        case STMT_DECO: {
            Stmt fn = constant_fold_stmt(stmt->as.stmt_deco.fn, is_modified);
            StmtDeco deco_stmt = {.fn = ALLOC(fn), .name = own_string(stmt->as.stmt_deco.name)};
            return AS_STMT_DECO(deco_stmt);
        }
        case STMT_EXPR: {
            const Expr *expr = &stmt->as.stmt_expr.expr;
            StmtExpr expr_stmt = {.expr = constant_fold_expr(expr, is_modified)};
            return AS_STMT_EXPR(expr_stmt);
        }
        case STMT_RETURN: {
            const Expr *expr = &stmt->as.stmt_return.returnval;
            StmtRet ret_stmt = {.returnval = constant_fold_expr(expr, is_modified)};
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

            StmtWhile while_stmt = {.condition = folded, .body = ALLOC(body), .label = label};

            return AS_STMT_WHILE(while_stmt);
        }
        case STMT_FOR: {
            const Expr *init = &stmt->as.stmt_for.initializer;
            const Expr *cond = &stmt->as.stmt_for.condition;
            const Expr *advancement = &stmt->as.stmt_for.advancement;

            Expr folded_init = constant_fold_expr(init, is_modified);
            Expr folded_cond = constant_fold_expr(cond, is_modified);
            Expr folded_advancement = constant_fold_expr(advancement, is_modified);

            Stmt folded_body = constant_fold_stmt(stmt->as.stmt_for.body, is_modified);
            StmtFor for_stmt = {.initializer = folded_init,
                                .condition = folded_cond,
                                .advancement = folded_advancement,
                                .label = own_string(stmt->as.stmt_for.label),
                                .body = ALLOC(folded_body)};

            return AS_STMT_FOR(for_stmt);
        }
        case STMT_IMPL: {
            DynArray_Stmt folded_methods = {0};
            for (size_t i = 0; i < stmt->as.stmt_impl.methods.count; i++)
            {
                Stmt folded = constant_fold_stmt(&stmt->as.stmt_impl.methods.data[i], is_modified);
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
            StmtContinue continue_stmt = {.label = own_string(stmt->as.stmt_continue.label)};
            return AS_STMT_CONTINUE(continue_stmt);
        }
        case STMT_USE: {
            StmtUse use_stmt = {.path = own_string(stmt->as.stmt_use.path)};
            return AS_STMT_USE(use_stmt);
        }
        case STMT_STRUCT: {
            DynArray_char_ptr properties = {0};
            for (size_t i = 0; i < stmt->as.stmt_struct.properties.count; i++)
            {
                dynarray_insert(&properties, stmt->as.stmt_struct.properties.data[i]);
            }
            StmtStruct struct_stmt = {.name = own_string(stmt->as.stmt_struct.name),
                                      .properties = properties};
            return AS_STMT_STRUCT(struct_stmt);
        }
        default:
            assert(0);
    }
}

static Expr propagate_copies_expr(const Expr *expr, Table_Expr *copies, bool *is_modified)
{
    switch (expr->kind)
    {
        case EXPR_LIT: {
            return *expr;
        }
        case EXPR_VAR: {
            char *name = expr->as.expr_var.name;
            Expr *resolved = table_get(copies, name);
            if (resolved)
            {
                *is_modified = true;
                return clone_expr(resolved);
            }
            ExprVar var_expr = {.name = own_string(name)};
            return AS_EXPR_VAR(var_expr);
        }
        case EXPR_BIN: {
            const ExprBin *binexpr = &expr->as.expr_bin;

            Expr new_lhs = propagate_copies_expr(expr->as.expr_bin.lhs, copies, is_modified);
            Expr new_rhs = propagate_copies_expr(expr->as.expr_bin.rhs, copies, is_modified);

            ExprBin new_binexpr = {.lhs = ALLOC(new_lhs),
                                   .rhs = ALLOC(new_rhs),
                                   .op = own_string(expr->as.expr_bin.op)};

            return AS_EXPR_BIN(new_binexpr);
        }
        case EXPR_ASSIGN: {
            Expr *lhs = expr->as.expr_assign.lhs;
            if (lhs->kind == EXPR_VAR)
            {
                table_remove(copies, lhs->as.expr_var.name);
            }
            Expr propagated_rhs = propagate_copies_expr(expr->as.expr_assign.rhs, copies, is_modified);

            Expr cloned_lhs = clone_expr(lhs);
            ExprAssign assign_expr = {.lhs = ALLOC(cloned_lhs),
                                      .rhs = ALLOC(propagated_rhs),
                                      .op = own_string(expr->as.expr_assign.op)};
            return AS_EXPR_ASSIGN(assign_expr);
        }
        case EXPR_ARRAY: {
            DynArray_Expr elements = {0};
            for (size_t i = 0; i < expr->as.expr_array.elements.count; i++)
            {
                Expr propagated = propagate_copies_expr(&expr->as.expr_array.elements.data[i],
                                                        copies, is_modified);
                dynarray_insert(&elements, propagated);
            }
            ExprArray array_expr = {.elements = elements};
            return AS_EXPR_ARRAY(array_expr);
        }
        case EXPR_GET: {
            Expr propagated = propagate_copies_expr(expr->as.expr_get.expr, copies, is_modified);
            ExprGet get_expr = {.op = own_string(expr->as.expr_get.op),
                                .property_name = own_string(expr->as.expr_get.property_name),
                                .expr = ALLOC(propagated)};
            return AS_EXPR_GET(get_expr);
        }
        case EXPR_UNA: {
            Expr propagated = propagate_copies_expr(expr->as.expr_una.expr, copies, is_modified);
            ExprUnary unary_expr = {.op = own_string(expr->as.expr_una.op),
                                    .expr = ALLOC(propagated)};
            return AS_EXPR_UNA(unary_expr);
        }
        case EXPR_SUBSCRIPT: {
            Expr propagated =
                propagate_copies_expr(expr->as.expr_subscript.index, copies, is_modified);

            Expr cloned_expr = clone_expr(expr->as.expr_subscript.expr);
            ExprSubscript subscript_expr = {.index = ALLOC(propagated), .expr = ALLOC(cloned_expr)};
            return AS_EXPR_SUBSCRIPT(subscript_expr);
        }
        case EXPR_STRUCT: {
            DynArray_Expr initializers = {0};
            for (size_t i = 0; i < expr->as.expr_struct.initializers.count; i++)
            {
                Expr propagated = propagate_copies_expr(&expr->as.expr_struct.initializers.data[i],
                                                        copies, is_modified);
                dynarray_insert(&initializers, propagated);
            }
            ExprStruct struct_expr = {.name = own_string(expr->as.expr_struct.name),
                                      .initializers = initializers};
            return AS_EXPR_STRUCT(struct_expr);
        }
        case EXPR_STRUCT_INIT: {
            Expr propagated =
                propagate_copies_expr(expr->as.expr_struct_init.value, copies, is_modified);
            Expr cloned_expr = clone_expr(expr->as.expr_struct_init.property);
            ExprStructInit struct_init_expr = {.value = ALLOC(propagated),
                                               .property = ALLOC(cloned_expr)};
            return AS_EXPR_STRUCT_INIT(struct_init_expr);
        }
        default:
            assert(0);
    }
}

static Stmt propagate_copies_stmt(const Stmt *stmt, Table_Expr *copies, bool *is_modified)
{
    switch (stmt->kind)
    {
        case STMT_LET: {
            const Expr *init = &stmt->as.stmt_let.initializer;

            table_remove(copies, stmt->as.stmt_let.name);

            if (init->kind == EXPR_VAR || init->kind == EXPR_LIT)
            {
                table_insert(copies, stmt->as.stmt_let.name, clone_expr(init));
            }

            Expr optimized = propagate_copies_expr(init, copies, is_modified);

            StmtLet let_stmt = {.name = own_string(stmt->as.stmt_let.name),
                                .initializer = optimized};

            return AS_STMT_LET(let_stmt);
        }
        case STMT_PRINT: {
            Expr expr = propagate_copies_expr(&stmt->as.stmt_print.expr, copies, is_modified);

            StmtPrint print_stmt = {.expr = expr};
            return AS_STMT_PRINT(print_stmt);
        }
        case STMT_FN: {
            Table_Expr cloned_copies = clone_table_expr(copies);
            Stmt body = propagate_copies_stmt(stmt->as.stmt_fn.body, &cloned_copies, is_modified);
            StmtFn fn_stmt = {.body = ALLOC(body), .name = own_string(stmt->as.stmt_fn.name)};
            free_table_expr(&cloned_copies);
            return AS_STMT_FN(fn_stmt);
        }
        case STMT_BLOCK: {
            Table_Expr cloned_copies = clone_table_expr(copies);
            DynArray_Stmt optimized = {0};
            for (size_t i = 0; i < stmt->as.stmt_block.stmts.count; i++)
            {
                Stmt s = propagate_copies_stmt(&stmt->as.stmt_block.stmts.data[i], &cloned_copies,
                                               is_modified);

                dynarray_insert(&optimized, s);
            }
            StmtBlock block_stmt = {.stmts = optimized, .depth = stmt->as.stmt_block.depth};
            free_table_expr(&cloned_copies);

            return AS_STMT_BLOCK(block_stmt);
        }
        case STMT_ASSERT: {
            Expr propagated =
                propagate_copies_expr(&stmt->as.stmt_assert.expr, copies, is_modified);
            StmtAssert assert_stmt = {.expr = propagated};
            return AS_STMT_ASSERT(assert_stmt);
        }
        case STMT_DECO: {
            Stmt propagated = propagate_copies_stmt(stmt->as.stmt_deco.fn, copies, is_modified);
            StmtDeco deco_stmt = {.name = own_string(stmt->as.stmt_deco.name),
                                  .fn = ALLOC(propagated)};
            return AS_STMT_DECO(deco_stmt);
        }
        case STMT_EXPR: {
            Expr propagated = propagate_copies_expr(&stmt->as.stmt_expr.expr, copies, is_modified);
            StmtExpr expr_stmt = {.expr = propagated};
            return AS_STMT_EXPR(expr_stmt);
            break;
        }
        case STMT_FOR: {
            Expr propagated_init =
                propagate_copies_expr(&stmt->as.stmt_for.initializer, copies, is_modified);
            Expr propagated_condition =
                propagate_copies_expr(&stmt->as.stmt_for.condition, copies, is_modified);
            Expr propagated_advancement =
                propagate_copies_expr(&stmt->as.stmt_for.advancement, copies, is_modified);

            Stmt propagated_body =
                propagate_copies_stmt(stmt->as.stmt_for.body, copies, is_modified);

            StmtFor for_stmt = {.initializer = propagated_init,
                                .condition = propagated_condition,
                                .advancement = propagated_advancement,
                                .body = ALLOC(propagated_body),
                                .label = own_string(stmt->as.stmt_for.label)};

            return AS_STMT_FOR(for_stmt);
        }
        case STMT_WHILE: {
            Expr expr = propagate_copies_expr(&stmt->as.stmt_while.condition, copies, is_modified);

            Table_Expr cloned_copies = clone_table_expr(copies);

            Stmt body =
                propagate_copies_stmt(stmt->as.stmt_while.body, &cloned_copies, is_modified);

            StmtWhile while_stmt = {.body = ALLOC(body),
                                    .condition = expr,
                                    .label = own_string(stmt->as.stmt_while.label)};

            free_table_expr(&cloned_copies);
            return AS_STMT_WHILE(while_stmt);
        }
        case STMT_IF: {
            Expr propagated_condition =
                propagate_copies_expr(&stmt->as.stmt_if.condition, copies, is_modified);
            Stmt propagated_then =
                propagate_copies_stmt(stmt->as.stmt_if.then_branch, copies, is_modified);

            Stmt *propagated_else = NULL;
            if (stmt->as.stmt_if.else_branch)
            {
                Stmt s = propagate_copies_stmt(stmt->as.stmt_if.else_branch, copies, is_modified);
                propagated_else = ALLOC(s);
            }

            StmtIf if_stmt = {.condition = propagated_condition,
                              .then_branch = ALLOC(propagated_then),
                              .else_branch = propagated_else};

            return AS_STMT_IF(if_stmt);
        }
        case STMT_IMPL: {
            DynArray_Stmt methods = {0};
            for (size_t i = 0; i < stmt->as.stmt_impl.methods.count; i++)
            {
                Stmt propagated =
                    propagate_copies_stmt(&stmt->as.stmt_impl.methods.data[i], copies, is_modified);
                dynarray_insert(&methods, propagated);
            }

            StmtImpl impl_stmt = {.name = own_string(stmt->as.stmt_impl.name), .methods = methods};
            return AS_STMT_IMPL(impl_stmt);
        }
        case STMT_RETURN: {
            Expr propagated =
                propagate_copies_expr(&stmt->as.stmt_return.returnval, copies, is_modified);
            StmtRet ret_stmt = {.returnval = propagated};
            return AS_STMT_RETURN(ret_stmt);
        }
        case STMT_BREAK: {
            StmtBreak break_stmt = {.label = own_string(stmt->as.stmt_break.label)};
            return AS_STMT_BREAK(break_stmt);
        }
        case STMT_CONTINUE: {
            StmtContinue continue_stmt = {.label = own_string(stmt->as.stmt_continue.label)};
            return AS_STMT_CONTINUE(continue_stmt);
        }
        default:
            print_stmt(stmt, 0, false);
            assert(0);
    }
}

DynArray_Stmt optimize(const DynArray_Stmt *ast)
{
    bool is_modified;
    DynArray_Stmt original = clone_ast(ast);

    do
    {
        is_modified = false;
        Table_Expr copies = {0};

        DynArray_Stmt optimized_ast = {0};
        for (size_t i = 0; i < original.count; i++)
        {
            Stmt folded = constant_fold_stmt(&original.data[i], &is_modified);
            Stmt propagated = propagate_copies_stmt(&folded, &copies, &is_modified);
            free_stmt(&folded);
            dynarray_insert(&optimized_ast, propagated);
        }

        free_table_expr(&copies);

        for (size_t i = 0; i < original.count; i++)
            free_stmt(&original.data[i]);
        dynarray_free(&original);

        original = clone_ast(&optimized_ast);

        for (size_t i = 0; i < optimized_ast.count; i++)
            free_stmt(&optimized_ast.data[i]);
        dynarray_free(&optimized_ast);

    } while (is_modified);

    return original;
}
