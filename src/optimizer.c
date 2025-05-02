#include "optimizer.h"

#include <stdio.h>
#include <string.h>

#include "ast.h"

void optimize_expr(Expr *expr)
{
#define HANDLE_OPER(type, oper, literal)                                                            \
    do                                                                               \
    {                                                                                \
        if (strcmp(binexpr->op, #oper) == 0)                                         \
        {                                                                            \
            type folded_const = lhs.as._##type oper rhs.as._##type;                  \
            ExprLit folded_expr = {.kind = LIT_##literal, .as._##type = folded_const}; \
                                                                                     \
            expr->as.expr_lit = folded_expr;                                      \
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
                HANDLE_OPER(double, +, NUM);
                HANDLE_OPER(double, -, NUM);
                HANDLE_OPER(double, *, NUM);
                HANDLE_OPER(double, /, NUM);
                HANDLE_OPER(bool, <, BOOL);
                HANDLE_OPER(bool, >, BOOL);
                HANDLE_OPER(bool, <=, BOOL);
                HANDLE_OPER(bool, >=, BOOL);
                HANDLE_OPER(bool, ==, BOOL);
                HANDLE_OPER(bool, !=, BOOL);
                HANDLE_OPER(bool, &&, BOOL);
                HANDLE_OPER(bool, ||, BOOL);
           }
        }
    }

#undef HANDLE_OPER
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
