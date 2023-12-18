import subprocess

from tests.util import VALGRIND_CMD, CASES_PATH
from tests.util import assert_error


def test_func_undefined():
    input_file = CASES_PATH / "func_undefined.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    error = process.stderr.decode("utf-8")

    assert_error(error, ["Compiler error: Function 'amain' is not defined.\n"])
    assert process.returncode == 1


def test_func_wrong_argcount():
    input_file = CASES_PATH / "func_wrong_argcount.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    error = process.stderr.decode("utf-8")

    assert_error(error, ["Compiler error: Function 'main' requires 2 arguments.\n"])
    assert process.returncode == 1
