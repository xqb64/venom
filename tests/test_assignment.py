import subprocess

from tests.util import VALGRIND_CMD
from tests.util import assert_output, assert_error
from tests.util import CASES_PATH


def test_assignment_global():
    input_file = CASES_PATH / "assign_global.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [16, 32])


def test_assignment_local():
    input_file = CASES_PATH / "assign_local.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [16, 32])


def test_assignment_invalid():
    input_file = CASES_PATH / "assign_invalid.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    error = process.stderr.decode("utf-8")

    assert_error(error, ["compiler: Invalid assignment.\n"])
    assert process.returncode == 1


def test_assignment_property():
    input_file = CASES_PATH / "assign_property.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [0, 3])


def test_assignment_property_nested():
    input_file = CASES_PATH / "assign_property_nested.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [0, "Hello, world!"])
