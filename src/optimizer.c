#include "optimizer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "dynarray.h"
#include "table.h"
#include "util.h"

typedef Table(Expr) Table_Expr;

ExprLit clone_literal(const ExprLit *literal)
{
    ExprLit clone;

    clone.kind = literal->kind;

    switch (literal->kind)
    {
        case LIT_NUM: {
            clone.as._double = literal->as._double;
            break;
        }
        case LIT_BOOL: {
            clone.as._bool = literal->as._bool;
            break;
        }
        case LIT_STR: {
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

    switch (expr->kind)
    {
        case EXPR_BIN: {
            Expr lhs = clone_expr(expr->as.expr_bin.lhs);
            Expr rhs = clone_expr(expr->as.expr_bin.rhs);
            clone.as.expr_bin.lhs = ALLOC(lhs);
            clone.as.expr_bin.rhs = ALLOC(rhs);
            clone.as.expr_bin.op = own_string(expr->as.expr_bin.op);
            break;
        }
        case EXPR_ASS: {
            Expr lhs = clone_expr(expr->as.expr_ass.lhs);
            Expr rhs = clone_expr(expr->as.expr_ass.rhs);
            clone.as.expr_ass.lhs = ALLOC(lhs);
            clone.as.expr_ass.rhs = ALLOC(rhs);
            clone.as.expr_ass.op = own_string(expr->as.expr_ass.op);
            break;
        }
        case EXPR_UNA: {
            Expr exp = clone_expr(expr->as.expr_una.exp);
            clone.as.expr_una.exp = ALLOC(exp);
            break;
        }
        case EXPR_CALL: {
            DynArray_Expr args = {0};
            for (size_t i = 0; i < expr->as.expr_call.arguments.count; i++)
            {
                Expr cloned = clone_expr(&expr->as.expr_call.arguments.data[i]);
                dynarray_insert(&args, cloned);
            }
            clone.as.expr_call.arguments = args;
            Expr callee_expr = clone_expr(expr->as.expr_call.callee);
            clone.as.expr_call.callee = ALLOC(callee_expr);
            break;
        }
        case EXPR_LIT: {
            ExprLit cloned = clone_literal(&expr->as.expr_lit);
            clone.as.expr_lit = cloned;
            break;
        }
        case EXPR_VAR: {
            clone.as.expr_var.name = own_string(expr->as.expr_var.name);
            break;
        }
        default:
            assert(0);
    }

    return clone;
}

static Bucket *clone_bucket(Bucket *src, Expr *src_items, Expr *dst_items, size_t *dst_count)
{
    if (!src)
        return NULL;

    Bucket *new = malloc(sizeof(Bucket));
    new->key = own_string(src->key);

    dst_items[*dst_count] = clone_expr(&src_items[src->value]);
    new->value = (*dst_count)++;

    new->next = clone_bucket(src->next, src_items, dst_items, dst_count);
    return new;
}

static inline Table_Expr table_clone(Table_Expr *src)
{
    Table_Expr clone = {0};

    for (size_t i = 0; i < TABLE_MAX; i++)
    {
        if (src->indexes[i])
        {
            clone.indexes[i] = clone_bucket(src->indexes[i], src->items, clone.items, &clone.count);
        }
    }

    /* FIXME */
    for (size_t i = 0; i < src->count; i++)
    {
        clone.items[i] = clone_expr(&src->items[i]);
    }

    return clone;
}

Expr constant_fold_expr(Expr *target, bool *is_modified)
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
            free_expression(*(expr)->lhs);                                                     \
            free_expression(*(expr)->rhs);                                                     \
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
        ExprBin *binexpr = &target->as.expr_bin;

        Expr new_lhs = constant_fold_expr(binexpr->lhs, is_modified);
        Expr new_rhs = constant_fold_expr(binexpr->rhs, is_modified);

        ExprBin new_binexpr = {
            .lhs = ALLOC(new_lhs), .rhs = ALLOC(new_rhs), .op = own_string(binexpr->op)};

        APPLY(&new_binexpr);

        return AS_EXPR_BIN(new_binexpr);
    }
    else if (target->kind == EXPR_ASS)
    {
        ExprAssign *assignexpr = &target->as.expr_ass;

        Expr new_rhs = constant_fold_expr(assignexpr->rhs, is_modified);

        Expr lhs = clone_expr(assignexpr->lhs);
        ExprAssign new_assignexpr = {
            .op = own_string(assignexpr->op), .lhs = ALLOC(lhs), .rhs = ALLOC(new_rhs)};

        return AS_EXPR_ASS(new_assignexpr);
    }
    else if (target->kind == EXPR_CALL)
    {
        ExprCall *callexpr = &target->as.expr_call;

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
        ExprStruct *structexpr = &target->as.expr_struct;

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
    else if (target->kind == EXPR_S_INIT)
    {
        ExprStructInit *structinitexpr = &target->as.expr_s_init;
        Expr folded = constant_fold_expr(structinitexpr->value, is_modified);
        Expr structinit_expr = clone_expr(structinitexpr->property);
        ExprStructInit new_struct_init_expr = {.value = ALLOC(folded),
                                               .property = ALLOC(structinit_expr)};

        return AS_EXPR_S_INIT(new_struct_init_expr);
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

    return *target;
#undef HANDLE_OPER
#undef APPLY_NUMERIC
#undef APPLY_BOOLEAN
#undef APPLY
}

Stmt constant_fold_stmt(Stmt *stmt, bool *is_modified)
{
    switch (stmt->kind)
    {
        case STMT_PRINT: {
            Expr *expr = &stmt->as.stmt_print.exp;
            Expr folded = constant_fold_expr(expr, is_modified);
            StmtPrint print_stmt = {.exp = folded};
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
            Expr *expr = &stmt->as.stmt_assert.exp;
            Expr folded = constant_fold_expr(expr, is_modified);
            StmtAssert assert_stmt = {.exp = folded};
            return AS_STMT_ASSERT(assert_stmt);
        }
        case STMT_DECO: {
            Stmt fn = constant_fold_stmt(stmt->as.stmt_deco.fn, is_modified);
            StmtDeco deco_stmt = {.fn = ALLOC(fn), .name = own_string(stmt->as.stmt_deco.name)};
            return AS_STMT_DECO(deco_stmt);
        }
        case STMT_EXPR: {
            Expr *expr = &stmt->as.stmt_expr.exp;
            StmtExpr expr_stmt = {.exp = constant_fold_expr(expr, is_modified)};
            return AS_STMT_EXPR(expr_stmt);
        }
        case STMT_RETURN: {
            Expr *expr = &stmt->as.stmt_return.returnval;
            StmtRet ret_stmt = {.returnval = constant_fold_expr(expr, is_modified)};
            return AS_STMT_RETURN(ret_stmt);
        }
        case STMT_YIELD: {
            Expr *expr = &stmt->as.stmt_yield.exp;
            Expr folded = constant_fold_expr(expr, is_modified);
            StmtYield yield_stmt = {.exp = folded};
            return AS_STMT_YIELD(yield_stmt);
        }
        case STMT_WHILE: {
            Expr *expr = &stmt->as.stmt_while.condition;
            Expr folded = constant_fold_expr(expr, is_modified);
            char *label = own_string(stmt->as.stmt_while.label);
            Stmt body = constant_fold_stmt(stmt->as.stmt_while.body, is_modified);

            StmtWhile while_stmt = {.condition = folded, .body = ALLOC(body), .label = label};

            return AS_STMT_WHILE(while_stmt);
        }
        case STMT_FOR: {
            Expr *init = &stmt->as.stmt_for.initializer;
            Expr *cond = &stmt->as.stmt_for.condition;
            Expr *advancement = &stmt->as.stmt_for.advancement;

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

    return *stmt;
}

Expr propagate_copies_expr(Expr *expr, Table_Expr *copies, bool *is_modified)
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
            ExprBin *binexpr = &expr->as.expr_bin;

            Expr new_lhs = propagate_copies_expr(expr->as.expr_bin.lhs, copies, is_modified);
            Expr new_rhs = propagate_copies_expr(expr->as.expr_bin.rhs, copies, is_modified);

            ExprBin new_binexpr = {.lhs = ALLOC(new_lhs),
                                   .rhs = ALLOC(new_rhs),
                                   .op = own_string(expr->as.expr_bin.op)};

            return AS_EXPR_BIN(new_binexpr);
        }
        case EXPR_ASS: {
            Expr *lhs = expr->as.expr_ass.lhs;
            if (lhs->kind == EXPR_VAR)
            {
                table_remove(copies, lhs->as.expr_var.name);
            }
            Expr propagated_rhs = propagate_copies_expr(expr->as.expr_ass.rhs, copies, is_modified);

            Expr cloned_lhs = clone_expr(lhs);
            ExprAssign assign_expr = {.lhs = ALLOC(cloned_lhs),
                                      .rhs = ALLOC(propagated_rhs),
                                      .op = own_string(expr->as.expr_ass.op)};
            return AS_EXPR_ASS(assign_expr);
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
            Expr propagated = propagate_copies_expr(expr->as.expr_get.exp, copies, is_modified);
            ExprGet get_expr = {.op = own_string(expr->as.expr_get.op),
                                .property_name = own_string(expr->as.expr_get.property_name),
                                .exp = ALLOC(propagated)};
            return AS_EXPR_GET(get_expr);
        }
        case EXPR_UNA: {
            Expr propagated = propagate_copies_expr(expr->as.expr_una.exp, copies, is_modified);
            ExprUnary unary_expr = {.op = own_string(expr->as.expr_una.op),
                                    .exp = ALLOC(propagated)};
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
        case EXPR_S_INIT: {
            Expr propagated =
                propagate_copies_expr(expr->as.expr_s_init.value, copies, is_modified);
            Expr cloned_expr = clone_expr(expr->as.expr_s_init.property);
            ExprStructInit struct_init_expr = {.value = ALLOC(propagated),
                                               .property = ALLOC(cloned_expr)};
            return AS_EXPR_S_INIT(struct_init_expr);
        }
        default:
            assert(0);
    }

    return *expr;
}

static inline void print_table_expr(Table_Expr *table)
{
    for (size_t i = 0; i < table->count; i++)
    {
        print_expression(&table->items[i], 0);
        putchar('\n');
    }
}

static void free_table_expr(const Table_Expr *table);

Stmt propagate_copies_stmt(Stmt *stmt, Table_Expr *copies, bool *is_modified)
{
    switch (stmt->kind)
    {
        case STMT_LET: {
            Expr *init = &stmt->as.stmt_let.initializer;

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
            Expr expr = propagate_copies_expr(&stmt->as.stmt_print.exp, copies, is_modified);

            StmtPrint print_stmt = {.exp = expr};
            return AS_STMT_PRINT(print_stmt);
        }
        case STMT_FN: {
            Table_Expr cloned_copies = table_clone(copies);
            Stmt body = propagate_copies_stmt(stmt->as.stmt_fn.body, &cloned_copies, is_modified);
            StmtFn fn_stmt = {.body = ALLOC(body), .name = own_string(stmt->as.stmt_fn.name)};
            free_table_expr(&cloned_copies);
            return AS_STMT_FN(fn_stmt);
        }
        case STMT_BLOCK: {
            Table_Expr cloned_copies = table_clone(copies);
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
            Expr propagated = propagate_copies_expr(&stmt->as.stmt_assert.exp, copies, is_modified);
            StmtAssert assert_stmt = {.exp = propagated};
            return AS_STMT_ASSERT(assert_stmt);
        }
        case STMT_DECO: {
            Stmt propagated = propagate_copies_stmt(stmt->as.stmt_deco.fn, copies, is_modified);
            StmtDeco deco_stmt = {.name = own_string(stmt->as.stmt_deco.name),
                                  .fn = ALLOC(propagated)};
            return AS_STMT_DECO(deco_stmt);
        }
        case STMT_EXPR: {
            Expr propagated = propagate_copies_expr(&stmt->as.stmt_expr.exp, copies, is_modified);
            StmtExpr expr_stmt = {.exp = propagated};
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

            Table_Expr cloned_copies = table_clone(copies);

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

    return *stmt;
}

static DynArray_Stmt clone_ast(DynArray_Stmt *ast);

static Stmt clone_stmt(Stmt *stmt)
{
    Stmt copy;

    copy.kind = stmt->kind;

    switch (stmt->kind)
    {
        case STMT_LET: {
            copy.as.stmt_let.initializer = clone_expr(&stmt->as.stmt_let.initializer);
            copy.as.stmt_let.name = own_string(stmt->as.stmt_let.name);
            break;
        }
        case STMT_FN: {
            copy.as.stmt_fn.name = own_string(stmt->as.stmt_fn.name);
            Stmt body = clone_stmt(stmt->as.stmt_fn.body);
            copy.as.stmt_fn.body = ALLOC(body);
            break;
        }
        case STMT_BLOCK: {
            copy.as.stmt_block.depth = stmt->as.stmt_block.depth;
            copy.as.stmt_block.stmts = clone_ast(&stmt->as.stmt_block.stmts);
            break;
        }
        case STMT_PRINT: {
            copy.as.stmt_print.exp = clone_expr(&stmt->as.stmt_print.exp);
            break;
        }
        case STMT_WHILE: {
            copy.as.stmt_while.label = own_string(stmt->as.stmt_while.label);
            copy.as.stmt_while.condition = clone_expr(&stmt->as.stmt_while.condition);
            Stmt body = clone_stmt(stmt->as.stmt_while.body);
            copy.as.stmt_while.body = ALLOC(body);
            break;
        }
        case STMT_FOR: {
            copy.as.stmt_for.initializer = clone_expr(&stmt->as.stmt_for.initializer);
            copy.as.stmt_for.condition = clone_expr(&stmt->as.stmt_for.condition);
            copy.as.stmt_for.advancement = clone_expr(&stmt->as.stmt_for.advancement);
            Stmt body = clone_stmt(stmt->as.stmt_for.body);
            copy.as.stmt_for.body = ALLOC(body);
            copy.as.stmt_for.label = own_string(stmt->as.stmt_for.label);
            break;
        }
        case STMT_EXPR: {
            copy.as.stmt_expr.exp = clone_expr(&stmt->as.stmt_expr.exp);
            break;
        }
        case STMT_BREAK: {
            copy.as.stmt_break.label = own_string(stmt->as.stmt_break.label);
            break;
        }
        case STMT_CONTINUE: {
            copy.as.stmt_continue.label = own_string(stmt->as.stmt_continue.label);
            break;
        }
        case STMT_ASSERT: {
            copy.as.stmt_assert.exp = clone_expr(&stmt->as.stmt_assert.exp);
            break;
        }
        case STMT_DECO: {
            Stmt body = clone_stmt(stmt->as.stmt_deco.fn);
            copy.as.stmt_deco.fn = ALLOC(body);
            copy.as.stmt_deco.name = own_string(stmt->as.stmt_deco.name);
            break;
        }
        case STMT_IF: {
            copy.as.stmt_if.condition = clone_expr(&stmt->as.stmt_if.condition);
            Stmt then_branch = clone_stmt(stmt->as.stmt_if.then_branch);
            Stmt *else_branch = NULL;
            if (stmt->as.stmt_if.else_branch)
            {
                Stmt s = clone_stmt(stmt->as.stmt_if.else_branch);
                *else_branch = s;
            }
            copy.as.stmt_if.then_branch = ALLOC(then_branch);
            copy.as.stmt_if.else_branch = else_branch;
            break;
        }
        case STMT_IMPL: {
            copy.as.stmt_impl.name = own_string(stmt->as.stmt_impl.name);
            DynArray_Stmt methods = {0};
            for (size_t i = 0; i < stmt->as.stmt_impl.methods.count; i++)
            {
                Stmt s = clone_stmt(&stmt->as.stmt_impl.methods.data[i]);
                dynarray_insert(&methods, s);
            }
            copy.as.stmt_impl.methods = methods;
            break;
        }
        case STMT_RETURN: {
            copy.as.stmt_return.returnval = clone_expr(&stmt->as.stmt_return.returnval);
            break;
        }
        case STMT_YIELD: {
            copy.as.stmt_yield.exp = clone_expr(&stmt->as.stmt_yield.exp);
            break;
        }
        case STMT_USE: {
            copy.as.stmt_use.path = own_string(stmt->as.stmt_use.path);
            break;
        }
        case STMT_STRUCT: {
            copy.as.stmt_struct.name = own_string(stmt->as.stmt_struct.name);
            DynArray_char_ptr properties = {0};
            for (size_t i = 0; i < stmt->as.stmt_struct.properties.count; i++)
            {
                char *s = own_string(stmt->as.stmt_struct.properties.data[i]);
                dynarray_insert(&properties, s);
            }
            break;
        }
        default:
            assert(0);
    }

    return copy;
}

static void free_table_expr(const Table_Expr *table)
{
    for (size_t i = 0; i < TABLE_MAX; i++)
    {
        if (table->indexes[i])
        {
            Bucket *head = table->indexes[i];
            Bucket *tmp;
            while (head != NULL)
            {
                tmp = head;
                head = head->next;
                free(tmp->key);
                free(tmp);
            }
        }
    }

    for (size_t i = 0; i < table->count; i++)
    {
        free_expression(table->items[i]);
    }
}

static DynArray_Stmt clone_ast(DynArray_Stmt *ast)
{
    DynArray_Stmt copy = {0};

    for (size_t i = 0; i < ast->count; i++)
    {
        Stmt s = clone_stmt(&ast->data[i]);
        dynarray_insert(&copy, s);
    }

    return copy;
}

DynArray_Stmt optimize(DynArray_Stmt *ast)
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
            free_stmt(folded);
            dynarray_insert(&optimized_ast, propagated);
        }

        free_table_expr(&copies);

        for (size_t i = 0; i < original.count; i++)
            free_stmt(original.data[i]);
        dynarray_free(&original);

        original = clone_ast(&optimized_ast);

        for (size_t i = 0; i < optimized_ast.count; i++)
            free_stmt(optimized_ast.data[i]);
        dynarray_free(&optimized_ast);

    } while (is_modified);

    return original;
}
