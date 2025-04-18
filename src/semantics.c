#include "semantics.h"

#include <stdio.h>
#include <stdlib.h>

#include "parser.h"

static size_t make_temp(void)
{
    static size_t tmp = 0;
    return tmp++;
}

DynArray_Stmt loop_label_program(DynArray_Stmt stmts, char *current)
{
    for (size_t i = 0; i < stmts.count; i++)
    {
        loop_label_stmt(&stmts.data[i], current);
    }
    return stmts;
}

void loop_label_stmt(Stmt *stmt, char *current)
{
    switch (stmt->kind)
    {
        case STMT_WHILE: {
            size_t tmp = make_temp();
            char *loop_label = malloc(256);
            snprintf(loop_label, 256, "while_%zu", tmp);
            loop_label_stmt(stmt->as.stmt_while.body, loop_label);
            stmt->as.stmt_while.label = loop_label;
            break;
        }
        case STMT_FOR: {
            size_t tmp = make_temp();
            char *loop_label = malloc(256);
            snprintf(loop_label, 256, "for_%zu", tmp);
            loop_label_stmt(stmt->as.stmt_for.body, loop_label);
            stmt->as.stmt_for.label = loop_label;
            break;
        }
        case STMT_BREAK: {
            if (!current)
            {
                fprintf(stderr, "compiler: 'break' statement outside the loop\n");
                exit(1);
            }
            stmt->as.stmt_break.label = current;
            break;
        }
        case STMT_CONTINUE: {
            if (!current)
            {
                fprintf(stderr, "compiler: 'continue' statement outside the loop\n");
                exit(1);
            }
            stmt->as.stmt_continue.label = current;
            break;
        }
        case STMT_FN: {
            loop_label_stmt(stmt->as.stmt_fn.body, current);
            break;
        }
        case STMT_BLOCK: {
            stmt->as.stmt_block.stmts = loop_label_program(stmt->as.stmt_block.stmts, current);
            break;
        }
        case STMT_IF: {
            loop_label_stmt(stmt->as.stmt_if.then_branch, current);
            if (stmt->as.stmt_if.else_branch)
                loop_label_stmt(stmt->as.stmt_if.else_branch, current);
            break;
        }
        default:
            break;
    }
}