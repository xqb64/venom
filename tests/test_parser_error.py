import subprocess

from tests.util import VALGRIND_CMD
from tests.util import CASES_PATH

import pytest


@pytest.mark.parametrize(
    "path, errmsg",
    [
        ["stmt_print.vnm", "Expected ';' after 'print' statement."],
        ["stmt_let1.vnm", "Expected ';' after 'let' statement."],
        ["stmt_let2.vnm", "Expected '=' after variable name in 'let' statement."],
        ["stmt_let3.vnm", "Expected identifier after 'let'."],
        ["stmt_assert.vnm", "Expected ';' after 'assert' statement."],
        ["stmt_yield.vnm", "Expected ';' after 'yield' statement."],
        ["stmt_expr.vnm", "Expected ';' after expression statement."],
        ["stmt_return.vnm", "Expected ';' after 'return' statement."],
        ["stmt_break.vnm", "Expected ';' after 'break' statement."],
        ["stmt_continue.vnm", "Expected ';' after 'continue' statement."],
        ["stmt_while1.vnm", "Expected '(' after 'while'."],
        ["stmt_while2.vnm", "Expected ')' after 'while' condition."],
        ["stmt_while3.vnm", "Expected '{' token."],
        ["stmt_for1.vnm", "Expected '(' after 'for'."],
        ["stmt_for2.vnm", "Expected 'let' after '(' in 'for' initializer."],
        ["stmt_for3.vnm", "Expected ';' after 'for' initializer."],
        ["stmt_for4.vnm", "Expected ';' after 'for' condition."],
        ["stmt_for5.vnm", "Expected ')' after 'for' advancement."],
        ["stmt_for6.vnm", "Expected '{' token."],
        ["stmt_for7.vnm", "Expected ':' after property name."],
        ["stmt_if1.vnm", "Expected '(' after 'if'."],
        ["stmt_if2.vnm", "Expected ')' after 'if' condition."],
        ["stmt_impl1.vnm", "Expected identifier after 'impl'."],
        ["stmt_impl2.vnm", "Expected '{' after identifier in 'impl' statement."],
        ["stmt_impl3.vnm", "Expected ';' after 'yield' statement."],
        ["stmt_fn1.vnm", "Expected identifier after 'fn'."],
        ["stmt_fn2.vnm", "Expected '(' after identifier in 'fn' statement."],
        ["stmt_fn3.vnm", "Expected parameter name after '(' in 'fn' statement."],
        ["stmt_fn4.vnm", "Expected '{' token."],
        ["stmt_fn5.vnm", "Expected ')' after the parameter list in 'fn' statement."],
        ["stmt_decorator1.vnm", "Expected 'fn' token."],
        ["stmt_decorator2.vnm", "Expected ';' after 'return' statement."],
        ["stmt_struct1.vnm", "Expected identifier after 'struct'."],
        ["stmt_struct2.vnm", "Expected '{' after identifier in 'struct' stmt."],
        ["stmt_struct3.vnm", "Expected property name."],
        ["stmt_struct4.vnm", "Expected semicolon after property name."],
        ["grouping.vnm", "Unmatched closing parentheses."],
        ["array_init.vnm", "Expected ']' after array members."],
        ["struct_init2.vnm", "Expected ':' after property name."],
        ["struct_init3.vnm", "Expected comma after `key: value` pair"] 
    ],
)
def test_parser_error_stmt(path, errmsg):
    process = subprocess.run(
        VALGRIND_CMD + [CASES_PATH / "errors" / "parser" / path],
        capture_output=True,
    )
    
    print(path)

    decoded = process.stderr.decode('utf-8')

    print(decoded)

    assert f"parser: {errmsg}" in decoded
    assert process.returncode == 255

