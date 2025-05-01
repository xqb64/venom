#include "semantics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define HANDLE_STMT(...)                       \
    do                                         \
    {                                          \
        result = loop_label_stmt(__VA_ARGS__); \
        if (!result.is_ok)                     \
        {                                      \
            return result;                     \
        }                                      \
    } while (0)

#define LOOP_LABEL_ERROR(...)                \
    alloc_err_str(&result.msg, __VA_ARGS__); \
    result.is_ok = false;                    \
    return result;

static size_t mktmp(void)
{
    static size_t tmp = 0;
    return tmp++;
}

LoopLabelResult loop_label_program(DynArray_Stmt *ast, char *current)
{
    LoopLabelResult result;

    for (size_t i = 0; i < ast->count; i++)
    {
        HANDLE_STMT(&ast->data[i], current);
    }

    result.is_ok = true;
    result.msg = NULL;
    result.ast = *ast;

    return result;
}

LoopLabelResult loop_label_stmt(Stmt *stmt, char *current)
{
    LoopLabelResult result;

    switch (stmt->kind)
    {
        case STMT_WHILE: {
            size_t tmp = mktmp();
            size_t label_len = lblen("while_", tmp);
            char *loop_label = malloc(label_len);
            snprintf(loop_label, label_len, "while_%zu", tmp);
            HANDLE_STMT(stmt->as.stmt_while.body, loop_label);
            stmt->as.stmt_while.label = loop_label;
            break;
        }
        case STMT_FOR: {
            size_t tmp = mktmp();
            size_t label_len = lblen("for_", tmp);
            char *loop_label = malloc(label_len);
            snprintf(loop_label, label_len, "for_%zu", tmp);
            HANDLE_STMT(stmt->as.stmt_for.body, loop_label);
            stmt->as.stmt_for.label = loop_label;
            break;
        }
        case STMT_BREAK: {
            if (!current)
            {
                LOOP_LABEL_ERROR("'break' statement outside the loop");
            }
            stmt->as.stmt_break.label = current;
            break;
        }
        case STMT_CONTINUE: {
            if (!current)
            {
                LOOP_LABEL_ERROR("compiler: 'continue' statement outside the loop");
            }
            stmt->as.stmt_continue.label = current;
            break;
        }
        case STMT_FN: {
            HANDLE_STMT(stmt->as.stmt_fn.body, current);
            break;
        }
        case STMT_BLOCK: {
            result = loop_label_program(&stmt->as.stmt_block.stmts, current);
            if (!result.is_ok)
                return result;
            break;
        }
        case STMT_IF: {
            HANDLE_STMT(stmt->as.stmt_if.then_branch, current);
            if (stmt->as.stmt_if.else_branch)
                HANDLE_STMT(stmt->as.stmt_if.else_branch, current);
            break;
        }
        default:
            break;
    }

    result.is_ok = true;
    result.msg = NULL;

    return result;
}
