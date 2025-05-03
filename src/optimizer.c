#include "optimizer.h"

#include <stdio.h>
#include <string.h>

#include "ast.h"

void optimize_expr(Expr *target)
{
#define HANDLE_OPER(type, expr, oper, literal_kind)                                     \
    do                                                                                  \
    {                                                                                   \
        if (strcmp((expr)->op, #oper) == 0)                                             \
        {                                                                               \
            type folded_const = lhs.as._##type oper rhs.as._##type;                     \
            ExprLit folded_expr = {.kind = (literal_kind), .as._##type = folded_const}; \
                                                                                        \
            target->as.expr_lit = folded_expr;                                          \
            target->kind = EXPR_LIT;                                                    \
        }                                                                               \
    } while (0)

#define APPLY_NUMERIC(expr)                  \
    HANDLE_OPER(double, (expr), +, LIT_NUM); \
    HANDLE_OPER(double, (expr), -, LIT_NUM); \
    HANDLE_OPER(double, (expr), *, LIT_NUM); \
    HANDLE_OPER(double, (expr), /, LIT_NUM); \
    HANDLE_OPER(bool, (expr), <, LIT_BOOL);  \
    HANDLE_OPER(bool, (expr), >, LIT_BOOL);  \
    HANDLE_OPER(bool, (expr), <=, LIT_BOOL); \
    HANDLE_OPER(bool, (expr), >=, LIT_BOOL); \
    HANDLE_OPER(bool, (expr), ==, LIT_BOOL); \
    HANDLE_OPER(bool, (expr), !=, LIT_BOOL);

#define APPLY_BOOLEAN(expr)                  \
    HANDLE_OPER(bool, (expr), ==, LIT_BOOL); \
    HANDLE_OPER(bool, (expr), !=, LIT_BOOL); \
    HANDLE_OPER(bool, (expr), &&, LIT_BOOL); \
    HANDLE_OPER(bool, (expr), ||, LIT_BOOL);

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
                                                                            \
            if (lhs.kind == LIT_BOOL && rhs.kind == LIT_BOOL)               \
            {                                                               \
                APPLY_BOOLEAN((expr));                                      \
            }                                                               \
        }                                                                   \
    } while (0)

    if (target->kind == EXPR_BIN)
    {
        ExprBin *binexpr = &target->as.expr_bin;

        optimize_expr(binexpr->lhs);
        optimize_expr(binexpr->rhs);

        APPLY(binexpr);
    }
    else if (target->kind == EXPR_ASS)
    {
        ExprAssign *assignexpr = &target->as.expr_ass;
        optimize_expr(assignexpr->rhs);
    }
    else if (target->kind == EXPR_CALL)
    {
        ExprCall *callexpr = &target->as.expr_call;

        for (size_t i = 0; i < callexpr->arguments.count; i++)
        {
            Expr *arg = &callexpr->arguments.data[i];
            optimize_expr(arg);
        }
    }
    else if (target->kind == EXPR_STRUCT)
    {
        ExprStruct *structexpr = &target->as.expr_struct;

        for (size_t i = 0; i < structexpr->initializers.count; i++)
        {
            Expr *initializer = &structexpr->initializers.data[i];
            optimize_expr(initializer);
        }
    }
    else if (target->kind == EXPR_S_INIT)
    {
        ExprStructInit *structinitexpr = &target->as.expr_s_init;

        optimize_expr(structinitexpr->value);
    }
#undef HANDLE_OPER
#undef APPLY_NUMERIC
#undef APPLY_BOOLEAN
#undef APPLY
}

void optimize_stmt(Stmt *stmt)
{
    switch (stmt->kind)
    {
        case STMT_PRINT: {
            Expr *expr = &stmt->as.stmt_print.exp;
            optimize_expr(expr);
            break;
        }
        case STMT_LET: {
            Expr *expr = &stmt->as.stmt_let.initializer;
            optimize_expr(expr);
            break;
        }
        case STMT_FN: {
            optimize_stmt(stmt->as.stmt_fn.body);
            break;
        }
        case STMT_IF: {
            optimize_expr(&stmt->as.stmt_if.condition);
            optimize_stmt(stmt->as.stmt_if.then_branch);
            if (stmt->as.stmt_if.else_branch)
                optimize_stmt(stmt->as.stmt_if.else_branch);
            break;
        }
        case STMT_BLOCK: {
            optimize(&stmt->as.stmt_block.stmts);
            break;
        }
        case STMT_ASSERT: {
            Expr *expr = &stmt->as.stmt_assert.exp;
            optimize_expr(expr);
            break;
        }
        case STMT_DECO: {
            optimize_stmt(stmt->as.stmt_deco.fn);
            break;
        }
        case STMT_EXPR: {
            Expr *expr = &stmt->as.stmt_expr.exp;
            optimize_expr(expr);
            break;
        }
        case STMT_RETURN: {
            Expr *expr = &stmt->as.stmt_return.returnval;
            optimize_expr(expr);
            break;
        }
        case STMT_YIELD: {
            Expr *expr = &stmt->as.stmt_yield.exp;
            optimize_expr(expr);
            break;
        }
        case STMT_WHILE: {
            Expr *expr = &stmt->as.stmt_while.condition;
            optimize_expr(expr);
            optimize_stmt(stmt->as.stmt_while.body);
            break;
        }
        case STMT_FOR: {
            Expr *init = &stmt->as.stmt_for.initializer;
            Expr *cond = &stmt->as.stmt_for.condition;
            Expr *advancement = &stmt->as.stmt_for.advancement;

            optimize_expr(init);
            optimize_expr(cond);
            optimize_expr(advancement);

            optimize_stmt(stmt->as.stmt_for.body);

            break;
        }
        case STMT_IMPL: {
            for (size_t i = 0; i < stmt->as.stmt_impl.methods.count; i++)
            {
                optimize_stmt(&stmt->as.stmt_impl.methods.data[i]);
            }
            break;
        }
        default:
            break;
    }
}

void optimize(DynArray_Stmt *ast)
{
    for (size_t i = 0; i < ast->count; i++)
    {
        optimize_stmt(&ast->data[i]);
    }
}
