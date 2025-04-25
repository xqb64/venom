#include "ast.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

void print_literal(ExprLit *literal)
{
    switch (literal->kind)
    {
        case LIT_BOOL: {
            printf("%s", literal->as.bval ? "true" : "false");
            break;
        }
        case LIT_NULL: {
            printf("null");
            break;
        }
        case LIT_NUM: {
            printf("%.16g", literal->as.dval);
            break;
        }
        case LIT_STR: {
            printf("%s", literal->as.sval);
            break;
        }
        default:
            assert(0);
    }
}

#define INDENT(n)                       \
    do {                                \
        for (int i = 0; i < (n); i++)   \
            putchar(' ');               \
    } while (0)

void print_expression(Expr *expr, int indent)
{
    switch (expr->kind)
    {
        case EXPR_LIT: {
            printf("Literal(\n");
            INDENT(indent+4);
            print_literal(&expr->as.expr_lit);
            break;
        }
        case EXPR_LOG: {
            printf("Logical(\n");
            INDENT(indent+4);
            print_expression(expr->as.expr_log.lhs, indent+4);
            printf(" %s ", expr->as.expr_bin.op);
            print_expression(expr->as.expr_log.rhs, indent+4);
            break;
        }
        case EXPR_ARRAY: {
            printf("Array(\n");
            INDENT(indent+4);
            printf("members: [");
            for (size_t i = 0; i < expr->as.expr_array.elements.count; i++)
            {
                print_expression(&expr->as.expr_array.elements.data[i], indent+4);
                if (i < expr->as.expr_array.elements.count - 1)
                    printf(", ");
            }
            printf("]");
            break;
        }
        case EXPR_STRUCT: {
            printf("Struct(\n");
            INDENT(indent+4);
            printf("name: %s,\n", expr->as.expr_struct.name);
            INDENT(indent+4);
            printf("initializers: [\n");
            for (size_t i = 0; i < expr->as.expr_struct.initializers.count; i++)
            {
                for (int j = 0; j < indent+8; j++)
                    printf(" ");
                print_expression(&expr->as.expr_struct.initializers.data[i], indent+8);
                if (i < expr->as.expr_struct.initializers.count - 1)
                    printf(",\n");
            } 
            break;
        }
        case EXPR_S_INIT: {
            printf("StructInit(\n");
            INDENT(indent+4);
            printf("property: ");
            print_expression(expr->as.expr_s_init.property, indent+4);
            printf(",\n");
            INDENT(indent+4);
            printf("value: ");
            print_expression(expr->as.expr_s_init.value, indent+4);
            break;
        }
        case EXPR_BIN: {
            printf("Binary(\n");
            INDENT(indent+4);
            print_expression(expr->as.expr_bin.lhs, indent+4);
            printf(" %s ", expr->as.expr_bin.op);
            print_expression(expr->as.expr_bin.rhs, indent+4);
            break;
        }
        case EXPR_GET: {
            printf("Get(\n");
            INDENT(indent+4);
            printf("gettee: %s,\n", expr->as.expr_get.property_name);
            INDENT(indent+4);
            printf("op: `%s`\n", expr->as.expr_get.op);
            INDENT(indent+4);
            printf("exp: ");
            print_expression(expr->as.expr_get.exp, indent+4);
            break;
        }
        case EXPR_SUBSCRIPT: {
            printf("Subscript(\n");
            INDENT(indent+4);
            printf("subscriptee: ");
            print_expression(expr->as.expr_subscript.expr, indent+4);
            printf(",\n");
            INDENT(indent+4);
            printf("index: ");
            print_expression(expr->as.expr_subscript.index, indent+4);
            break;
        }
        case EXPR_UNA: {
            printf("Unary(\n");
            INDENT(indent+4);
            printf("exp: ");
            print_expression(expr->as.expr_una.exp, indent+4);
            printf(",\n");
            INDENT(indent+4);
            printf("op: %s", expr->as.expr_una.op);
            break;
        }
        case EXPR_VAR: {
            printf("Variable(\n");
            INDENT(indent+4);
            printf("name: %s", expr->as.expr_var.name);
            break;
        }
        case EXPR_ASS: {
            printf("Assign(\n");
            INDENT(indent+4);
            print_expression(expr->as.expr_ass.lhs, indent+4);
            printf(" %s ", expr->as.expr_ass.op);
            print_expression(expr->as.expr_ass.rhs, indent+4);
            break;
        }
        case EXPR_CALL: {
            printf("Call(\n");           
            INDENT(indent+4);
            printf("callee: ");
            print_expression(expr->as.expr_call.callee, indent+4);
            printf(", \n");
            INDENT(indent+4);
            printf("arguments: [");
            for (size_t i = 0; i < expr->as.expr_call.arguments.count; i++)
            {
                print_expression(&expr->as.expr_call.arguments.data[i], indent+4);
                if (i < expr->as.expr_call.arguments.count - 1)
                    printf(", ");
            }
            printf("]");
            break;
        }
        default:
            break;
    }
    printf("\n");
    INDENT(indent);
    printf(")");
}

void print_stmt(Stmt *stmt, int indent, bool continuation)
{
    if (!continuation)
        INDENT(indent);

    switch (stmt->kind)
    {
        case STMT_LET: {
            printf("Let(\n");
            INDENT(indent+4);
            printf("name: %s,\n", stmt->as.stmt_let.name);
            INDENT(indent+4);
            printf("initializer: ");
            print_expression(&stmt->as.stmt_let.initializer, indent+4);
           break;
        }
        case STMT_PRINT: {
            printf("Print(\n");
            INDENT(indent+4);
            print_expression(&stmt->as.stmt_print.exp, indent+4);
            break;
        }
        case STMT_FN: {
            printf("Function(\n");
            INDENT(indent+4);
            print_stmt(stmt->as.stmt_fn.body, indent+4, true);
            break;
        }
        case STMT_BLOCK: {
            printf("Block(\n");
            for (size_t i = 0; i < stmt->as.stmt_block.stmts.count; i++)
            {
                print_stmt(&stmt->as.stmt_block.stmts.data[i], indent+4, false);
                if (i < stmt->as.stmt_block.stmts.count - 1)
                    printf(",\n");
            }
            break;
        }
        case STMT_WHILE: {
            printf("While(\n");
            INDENT(indent+4);
            printf("condition: ");
            print_expression(&stmt->as.stmt_while.condition, indent+4);
            printf(",\n");
            INDENT(indent+4);
            printf("body: ");
            print_stmt(stmt->as.stmt_while.body, indent+4, true);
            break;
        }
        case STMT_FOR: {
            printf("For(\n");
            INDENT(indent+4);
            printf("init: ");
            print_expression(&stmt->as.stmt_for.initializer, indent+4);
            putchar('\n');
            INDENT(indent+4);
            printf("condition: ");
            print_expression(&stmt->as.stmt_for.condition, indent+4);
            putchar('\n');
            INDENT(indent+4);
            printf("advancement: ");
            print_expression(&stmt->as.stmt_for.advancement, indent+4);
            putchar('\n');
            INDENT(indent+4);
            printf("label: \"%s\",\n", stmt->as.stmt_for.label);
            INDENT(indent+4);
            printf("body: ");
            print_stmt(stmt->as.stmt_for.body, indent+4, true);
            break;
        }
        case STMT_IF: {
            printf("If(\n");
            INDENT(indent+4);
            printf("condition: ");
            print_expression(&stmt->as.stmt_if.condition, indent+4);
            printf(",\n");
            INDENT(indent+4);  
            printf("then: ");
            print_stmt(stmt->as.stmt_if.then_branch, indent+4, true);
            printf(",\n");
            INDENT(indent+4);
            printf("else: ");
            if (stmt->as.stmt_if.else_branch)
                print_stmt(stmt->as.stmt_if.else_branch, indent+4, true);
            else
                printf("null");
            break;
        }
        case STMT_EXPR: {
            printf("Expr(\n");
            INDENT(indent+4);
            print_expression(&stmt->as.stmt_expr.exp, indent+4);
            break;
        }
        case STMT_RETURN: {
            printf("Return(\n");
            INDENT(indent+4);
            print_expression(&stmt->as.stmt_return.returnval, indent+4);
            break;
        }
        case STMT_BREAK: {
            printf("Break");
            break;
        }
        case STMT_CONTINUE: {
            printf("Continue");
            break;
        }
        case STMT_ASSERT: {
            printf("Assert(\n");
            INDENT(indent+4);
            print_expression(&stmt->as.stmt_assert.exp, indent+4);
            break;
        }
        case STMT_USE: {
           printf("Use(%s)\n", stmt->as.stmt_use.path);
           break;
        }
        case STMT_YIELD: {
            printf("Yield(\n");
            INDENT(indent+4);
            print_expression(&stmt->as.stmt_yield.exp, indent+4); 
            break;
        }
        case STMT_DECO: {
            printf("Decorator(\n");
            INDENT(indent+4);
            printf("name: %s,\n", stmt->as.stmt_deco.name);
            INDENT(indent+4);
            printf("fn: ");
            print_stmt(stmt->as.stmt_deco.fn, indent+4, true);
            break;
        }
        case STMT_STRUCT: {
            printf("Struct(\n");
            INDENT(indent+4);
            printf("name: %s\n", stmt->as.stmt_struct.name);
            INDENT(indent+4);
            printf("properties: [");
            for (size_t i = 0; i < stmt->as.stmt_struct.properties.count; i++)
            {
                printf("%s", stmt->as.stmt_struct.properties.data[i]);
                if (i < stmt->as.stmt_struct.properties.count - 1)
                    printf(", ");
            }
            printf("]");
            break;
        }
        case STMT_IMPL: {
            printf("Impl(\n");
            INDENT(indent+4);
            printf("name: %s,\n", stmt->as.stmt_impl.name);
            INDENT(indent+4);
            printf("methods: [");
            for (size_t i = 0; i < stmt->as.stmt_impl.methods.count; i++)
            {
                print_stmt(&stmt->as.stmt_impl.methods.data[i], indent+4, true);
                if (i < stmt->as.stmt_impl.methods.count - 1)
                    printf(", ");
            }
            printf("]");
            break;
        }
        default:
            break;
    }
    printf("\n");
    INDENT(indent);
    printf(")");
}

void pretty_print(DynArray_Stmt *stmts)
{
    int indent = 0;

    printf("Program(\n");
    
    for (size_t i = 0; i < stmts->count; i++)
    {
       print_stmt(&stmts->data[i], indent+4, false);
       if (i < stmts->count - 1)
            printf(",\n");
     }

    printf("\n)\n");
}