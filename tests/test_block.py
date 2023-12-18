import subprocess

from tests.util import VALGRIND_CMD, CASES_PATH
from tests.util import assert_output, assert_error


def test_block_inherited_param():
    input_file = CASES_PATH / "block_inherited_param.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [0])


def test_block_inherited_local():
    input_file = CASES_PATH / "block_inherited_local.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [3])


def test_block_undefined_var():
    input_file = CASES_PATH / "block_undefined_var.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    error = process.stderr.decode("utf-8")

    assert_error(error, ["Compiler error: Variable 'z' is not defined.\n"])
    assert process.returncode == 1


def test_block_retval_remains_on_stack():
    input_file = CASES_PATH / "block_retval_remains_on_stack.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [7, None])


def test_block_retval_gets_popped():
    input_file = CASES_PATH / "block_retval_gets_popped.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [7, *(["Hello, world!"] * 5)])
