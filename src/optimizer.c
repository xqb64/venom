#include "optimizer.h"

#include <stdio.h>
#include <string.h>

#include "ast.h"

void optimize_expr(Expr *expr)
{
#define HANDLE_OPER(oper)                                                            \
    do                                                                               \
    {                                                                                \
        if (strcmp(binexpr->op, #oper) == 0)                                         \
        {                                                                            \
            double optimized_double = lhs.as.dval oper rhs.as.dval;                  \
            ExprLit optimized_expr = {.kind = LIT_NUM, .as.dval = optimized_double}; \
                                                                                     \
            expr->as.expr_lit = optimized_expr;                                      \
            expr->kind = EXPR_LIT;                                                   \
        }                                                                            \
    } while (0)

    if (expr->kind == EXPR_BIN)
    {
        ExprBin *binexpr = &expr->as.expr_bin;

        optimize_expr(binexpr->lhs);
        optimize_expr(binexpr->rhs);

        if (binexpr->lhs->kind == EXPR_LIT && binexpr->rhs->kind == EXPR_LIT)
        {
            ExprLit lhs = binexpr->lhs->as.expr_lit;
            ExprLit rhs = binexpr->rhs->as.expr_lit;

            if (lhs.kind == LIT_NUM && rhs.kind == LIT_NUM)
            {
                HANDLE_OPER(+);
                HANDLE_OPER(-);
                HANDLE_OPER(*);
                HANDLE_OPER(/);
            }
        }
    }

#undef HANDLE_OP
}

void optimize_stmt(Stmt *stmt)
{
    if (stmt->kind == STMT_PRINT)
    {
        Expr *expr = &stmt->as.stmt_print.exp;
        optimize_expr(expr);
    }
    else if (stmt->kind == STMT_FN)
    {
        optimize_stmt(stmt->as.stmt_fn.body);
    }
    else if (stmt->kind == STMT_BLOCK)
    {
        optimize(&stmt->as.stmt_block.stmts);
    }
}

void optimize(DynArray_Stmt *ast)
{
    for (size_t i = 0; i < ast->count; i++)
    {
        optimize_stmt(&ast->data[i]);
    }
}
