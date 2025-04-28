#include "ast.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static void free_expression(Expr e)
{
    switch (e.kind)
    {
        case EXPR_LIT: {
            ExprLit litexpr = TO_EXPR_LIT(e);
            if (litexpr.kind == LIT_STR)
            {
                free(litexpr.as.sval);
            }
            break;
        }
        case EXPR_VAR: {
            ExprVar varexpr = TO_EXPR_VAR(e);
            free(varexpr.name);
            break;
        }
        case EXPR_UNA: {
            ExprUnary unaryexpr = TO_EXPR_UNA(e);
            free_expression(*unaryexpr.exp);
            free(unaryexpr.exp);
            free(unaryexpr.op);
            break;
        }
        case EXPR_BIN: {
            ExprBin binexpr = TO_EXPR_BIN(e);
            free_expression(*binexpr.lhs);
            free_expression(*binexpr.rhs);
            free(binexpr.lhs);
            free(binexpr.rhs);
            break;
        }
        case EXPR_ASS: {
            ExprAssign assignexpr = TO_EXPR_ASS(e);
            free_expression(*assignexpr.lhs);
            free_expression(*assignexpr.rhs);
            free(assignexpr.lhs);
            free(assignexpr.rhs);
            break;
        }
        case EXPR_LOG: {
            ExprLogic logicexpr = TO_EXPR_LOG(e);
            free_expression(*logicexpr.lhs);
            free_expression(*logicexpr.rhs);
            free(logicexpr.lhs);
            free(logicexpr.rhs);
            free(logicexpr.op);
            break;
        }
        case EXPR_CALL: {
            ExprCall callexpr = TO_EXPR_CALL(e);
            free_expression(*callexpr.callee);
            free(callexpr.callee);
            for (size_t i = 0; i < callexpr.arguments.count; i++)
            {
                free_expression(callexpr.arguments.data[i]);
            }
            dynarray_free(&callexpr.arguments);
            break;
        }
        case EXPR_STRUCT: {
            ExprStruct structexpr = TO_EXPR_STRUCT(e);
            free(structexpr.name);
            for (size_t i = 0; i < structexpr.initializers.count; i++)
            {
                free_expression(structexpr.initializers.data[i]);
            }
            dynarray_free(&structexpr.initializers);
            break;
        }
        case EXPR_S_INIT: {
            ExprStructInit structinitexpr = TO_EXPR_S_INIT(e);
            free_expression(*structinitexpr.property);
            free_expression(*structinitexpr.value);
            free(structinitexpr.value);
            free(structinitexpr.property);
            break;
        }
        case EXPR_GET: {
            ExprGet getexpr = TO_EXPR_GET(e);
            free_expression(*getexpr.exp);
            free(getexpr.property_name);
            free(getexpr.exp);
            free(getexpr.op);
            break;
        }
        case EXPR_ARRAY: {
            ExprArray arrayexpr = TO_EXPR_ARRAY(e);
            for (size_t i = 0; i < arrayexpr.elements.count; i++)
            {
                free_expression(arrayexpr.elements.data[i]);
            }
            dynarray_free(&arrayexpr.elements);
            break;
        }
        case EXPR_SUBSCRIPT: {
            ExprSubscript subscriptexpr = TO_EXPR_SUBSCRIPT(e);
            free_expression(*subscriptexpr.expr);
            free_expression(*subscriptexpr.index);
            free(subscriptexpr.expr);
            free(subscriptexpr.index);
            break;
        }
        default:
            break;
    }
}

void free_stmt(Stmt stmt)
{
    switch (stmt.kind)
    {
        case STMT_PRINT: {
            free_expression(TO_STMT_PRINT(stmt).exp);
            break;
        }
        case STMT_IMPL: {
            free(TO_STMT_IMPL(stmt).name);
            for (size_t i = 0; i < TO_STMT_IMPL(stmt).methods.count; i++)
            {
                free_stmt(TO_STMT_IMPL(stmt).methods.data[i]);
            }
            dynarray_free(&TO_STMT_IMPL(stmt).methods);
            break;
        }
        case STMT_LET: {
            free_expression(TO_STMT_LET(stmt).initializer);
            free(stmt.as.stmt_let.name);
            break;
        }
        case STMT_BLOCK: {
            for (size_t i = 0; i < TO_STMT_BLOCK(&stmt).stmts.count; i++)
            {
                free_stmt(TO_STMT_BLOCK(&stmt).stmts.data[i]);
            }
            dynarray_free(&TO_STMT_BLOCK(&stmt).stmts);
            break;
        }
        case STMT_IF: {
            free_expression(TO_STMT_IF(stmt).condition);

            free_stmt(*TO_STMT_IF(stmt).then_branch);
            free(TO_STMT_IF(stmt).then_branch);

            if (TO_STMT_IF(stmt).else_branch != NULL)
            {
                free_stmt(*TO_STMT_IF(stmt).else_branch);
                free(TO_STMT_IF(stmt).else_branch);
            }

            break;
        }
        case STMT_WHILE: {
            free(TO_STMT_WHILE(stmt).label);
            free_expression(TO_STMT_WHILE(stmt).condition);
            for (size_t i = 0; i < TO_STMT_BLOCK(TO_STMT_WHILE(stmt).body).stmts.count; i++)
            {
                free_stmt(TO_STMT_BLOCK(TO_STMT_WHILE(stmt).body).stmts.data[i]);
            }
            dynarray_free(&TO_STMT_BLOCK(TO_STMT_WHILE(stmt).body).stmts);
            free(TO_STMT_WHILE(stmt).body);
            break;
        }
        case STMT_FOR: {
            free(TO_STMT_FOR(stmt).label);
            free_expression(TO_STMT_FOR(stmt).initializer);
            free_expression(TO_STMT_FOR(stmt).condition);
            free_expression(TO_STMT_FOR(stmt).advancement);
            for (size_t i = 0; i < TO_STMT_BLOCK(TO_STMT_FOR(stmt).body).stmts.count; i++)
            {
                free_stmt(TO_STMT_BLOCK(TO_STMT_FOR(stmt).body).stmts.data[i]);
            }
            dynarray_free(&TO_STMT_BLOCK(TO_STMT_FOR(stmt).body).stmts);
            free(TO_STMT_FOR(stmt).body);
            break;
        }
        case STMT_RETURN: {
            free_expression(TO_STMT_RETURN(stmt).returnval);
            break;
        }
        case STMT_EXPR: {
            free_expression(TO_STMT_EXPR(stmt).exp);
            break;
        }
        case STMT_FN: {
            free(TO_STMT_FN(stmt).name);
            for (size_t i = 0; i < TO_STMT_FN(stmt).parameters.count; i++)
            {
                free(TO_STMT_FN(stmt).parameters.data[i]);
            }
            dynarray_free(&TO_STMT_FN(stmt).parameters);
            for (size_t i = 0; i < TO_STMT_BLOCK(TO_STMT_FN(stmt).body).stmts.count; i++)
            {
                free_stmt(TO_STMT_BLOCK(TO_STMT_FN(stmt).body).stmts.data[i]);
            }
            dynarray_free(&TO_STMT_BLOCK(TO_STMT_FN(stmt).body).stmts);
            free(TO_STMT_FN(stmt).body);
            break;
        }
        case STMT_DECO: {
            free(stmt.as.stmt_deco.name);
            free_stmt(*stmt.as.stmt_deco.fn);
            free(stmt.as.stmt_deco.fn);
            break;
        }
        case STMT_STRUCT: {
            free(TO_STMT_STRUCT(stmt).name);
            for (size_t i = 0; i < TO_STMT_STRUCT(stmt).properties.count; i++)
            {
                free(TO_STMT_STRUCT(stmt).properties.data[i]);
            }
            dynarray_free(&TO_STMT_STRUCT(stmt).properties);
            break;
        }
        case STMT_USE: {
            free(TO_STMT_USE(stmt).path);
            break;
        }
        case STMT_YIELD: {
            free_expression(TO_STMT_YIELD(stmt).exp);
            break;
        }
        default:
            break;
    }
}

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

#define INDENT(n)                     \
    do                                \
    {                                 \
        for (int s = 0; s < (n); s++) \
            putchar(' ');             \
    } while (0)

void print_expression(Expr *expr, int indent)
{
    switch (expr->kind)
    {
        case EXPR_LIT: {
            printf("Literal(\n");
            INDENT(indent + 4);
            print_literal(&expr->as.expr_lit);
            break;
        }
        case EXPR_LOG: {
            printf("Logical(\n");
            INDENT(indent + 4);
            print_expression(expr->as.expr_log.lhs, indent + 4);
            printf(" %s ", expr->as.expr_bin.op);
            print_expression(expr->as.expr_log.rhs, indent + 4);
            break;
        }
        case EXPR_ARRAY: {
            printf("Array(\n");
            INDENT(indent + 4);
            printf("members: [");
            for (size_t i = 0; i < expr->as.expr_array.elements.count; i++)
            {
                print_expression(&expr->as.expr_array.elements.data[i], indent + 4);
                if (i < expr->as.expr_array.elements.count - 1)
                    printf(", ");
            }
            printf("]");
            break;
        }
        case EXPR_STRUCT: {
            printf("Struct(\n");
            INDENT(indent + 4);
            printf("name: %s,\n", expr->as.expr_struct.name);
            INDENT(indent + 4);
            printf("initializers: [\n");
            for (size_t i = 0; i < expr->as.expr_struct.initializers.count; i++)
            {
                INDENT(indent + 8);
                print_expression(&expr->as.expr_struct.initializers.data[i], indent + 8);
                if (i < expr->as.expr_struct.initializers.count - 1)
                    printf(",\n");
            }
            break;
        }
        case EXPR_S_INIT: {
            printf("StructInit(\n");
            INDENT(indent + 4);
            printf("property: ");
            print_expression(expr->as.expr_s_init.property, indent + 4);
            printf(",\n");
            INDENT(indent + 4);
            printf("value: ");
            print_expression(expr->as.expr_s_init.value, indent + 4);
            break;
        }
        case EXPR_BIN: {
            printf("Binary(\n");
            INDENT(indent + 4);
            print_expression(expr->as.expr_bin.lhs, indent + 4);
            printf(" %s ", expr->as.expr_bin.op);
            print_expression(expr->as.expr_bin.rhs, indent + 4);
            break;
        }
        case EXPR_GET: {
            printf("Get(\n");
            INDENT(indent + 4);
            printf("gettee: %s,\n", expr->as.expr_get.property_name);
            INDENT(indent + 4);
            printf("op: `%s`\n", expr->as.expr_get.op);
            INDENT(indent + 4);
            printf("exp: ");
            print_expression(expr->as.expr_get.exp, indent + 4);
            break;
        }
        case EXPR_SUBSCRIPT: {
            printf("Subscript(\n");
            INDENT(indent + 4);
            printf("subscriptee: ");
            print_expression(expr->as.expr_subscript.expr, indent + 4);
            printf(",\n");
            INDENT(indent + 4);
            printf("index: ");
            print_expression(expr->as.expr_subscript.index, indent + 4);
            break;
        }
        case EXPR_UNA: {
            printf("Unary(\n");
            INDENT(indent + 4);
            printf("exp: ");
            print_expression(expr->as.expr_una.exp, indent + 4);
            printf(",\n");
            INDENT(indent + 4);
            printf("op: %s", expr->as.expr_una.op);
            break;
        }
        case EXPR_VAR: {
            printf("Variable(\n");
            INDENT(indent + 4);
            printf("name: %s", expr->as.expr_var.name);
            break;
        }
        case EXPR_ASS: {
            printf("Assign(\n");
            INDENT(indent + 4);
            print_expression(expr->as.expr_ass.lhs, indent + 4);
            printf(" %s ", expr->as.expr_ass.op);
            print_expression(expr->as.expr_ass.rhs, indent + 4);
            break;
        }
        case EXPR_CALL: {
            printf("Call(\n");
            INDENT(indent + 4);
            printf("callee: ");
            print_expression(expr->as.expr_call.callee, indent + 4);
            printf(", \n");
            INDENT(indent + 4);
            printf("arguments: [");
            for (size_t i = 0; i < expr->as.expr_call.arguments.count; i++)
            {
                print_expression(&expr->as.expr_call.arguments.data[i], indent + 4);
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
            INDENT(indent + 4);
            printf("name: %s,\n", stmt->as.stmt_let.name);
            INDENT(indent + 4);
            printf("initializer: ");
            print_expression(&stmt->as.stmt_let.initializer, indent + 4);
            break;
        }
        case STMT_PRINT: {
            printf("Print(\n");
            INDENT(indent + 4);
            print_expression(&stmt->as.stmt_print.exp, indent + 4);
            break;
        }
        case STMT_FN: {
            printf("Function(\n");
            INDENT(indent + 4);
            print_stmt(stmt->as.stmt_fn.body, indent + 4, true);
            break;
        }
        case STMT_BLOCK: {
            printf("Block(\n");
            for (size_t i = 0; i < stmt->as.stmt_block.stmts.count; i++)
            {
                print_stmt(&stmt->as.stmt_block.stmts.data[i], indent + 4, false);
                if (i < stmt->as.stmt_block.stmts.count - 1)
                    printf(",\n");
            }
            break;
        }
        case STMT_WHILE: {
            printf("While(\n");
            INDENT(indent + 4);
            printf("condition: ");
            print_expression(&stmt->as.stmt_while.condition, indent + 4);
            printf(",\n");
            INDENT(indent + 4);
            printf("body: ");
            print_stmt(stmt->as.stmt_while.body, indent + 4, true);
            break;
        }
        case STMT_FOR: {
            printf("For(\n");
            INDENT(indent + 4);
            printf("init: ");
            print_expression(&stmt->as.stmt_for.initializer, indent + 4);
            putchar('\n');
            INDENT(indent + 4);
            printf("condition: ");
            print_expression(&stmt->as.stmt_for.condition, indent + 4);
            putchar('\n');
            INDENT(indent + 4);
            printf("advancement: ");
            print_expression(&stmt->as.stmt_for.advancement, indent + 4);
            putchar('\n');
            INDENT(indent + 4);
            printf("label: \"%s\",\n", stmt->as.stmt_for.label);
            INDENT(indent + 4);
            printf("body: ");
            print_stmt(stmt->as.stmt_for.body, indent + 4, true);
            break;
        }
        case STMT_IF: {
            printf("If(\n");
            INDENT(indent + 4);
            printf("condition: ");
            print_expression(&stmt->as.stmt_if.condition, indent + 4);
            printf(",\n");
            INDENT(indent + 4);
            printf("then: ");
            print_stmt(stmt->as.stmt_if.then_branch, indent + 4, true);
            printf(",\n");
            INDENT(indent + 4);
            printf("else: ");
            if (stmt->as.stmt_if.else_branch)
                print_stmt(stmt->as.stmt_if.else_branch, indent + 4, true);
            else
                printf("null");
            break;
        }
        case STMT_EXPR: {
            printf("Expr(\n");
            INDENT(indent + 4);
            print_expression(&stmt->as.stmt_expr.exp, indent + 4);
            break;
        }
        case STMT_RETURN: {
            printf("Return(\n");
            INDENT(indent + 4);
            print_expression(&stmt->as.stmt_return.returnval, indent + 4);
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
            INDENT(indent + 4);
            print_expression(&stmt->as.stmt_assert.exp, indent + 4);
            break;
        }
        case STMT_USE: {
            printf("Use(%s)\n", stmt->as.stmt_use.path);
            break;
        }
        case STMT_YIELD: {
            printf("Yield(\n");
            INDENT(indent + 4);
            print_expression(&stmt->as.stmt_yield.exp, indent + 4);
            break;
        }
        case STMT_DECO: {
            printf("Decorator(\n");
            INDENT(indent + 4);
            printf("name: %s,\n", stmt->as.stmt_deco.name);
            INDENT(indent + 4);
            printf("fn: ");
            print_stmt(stmt->as.stmt_deco.fn, indent + 4, true);
            break;
        }
        case STMT_STRUCT: {
            printf("Struct(\n");
            INDENT(indent + 4);
            printf("name: %s\n", stmt->as.stmt_struct.name);
            INDENT(indent + 4);
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
            INDENT(indent + 4);
            printf("name: %s,\n", stmt->as.stmt_impl.name);
            INDENT(indent + 4);
            printf("methods: [");
            for (size_t i = 0; i < stmt->as.stmt_impl.methods.count; i++)
            {
                print_stmt(&stmt->as.stmt_impl.methods.data[i], indent + 4, true);
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
        print_stmt(&stmts->data[i], indent + 4, false);
        if (i < stmts->count - 1)
            printf(",\n");
    }

    printf("\n)\n");
}
