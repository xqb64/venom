import subprocess

from tests.util import VALGRIND_CMD, CASES_PATH
from tests.util import assert_output, assert_error


def test_working_import():
    input_file = CASES_PATH / "import.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [3] * 5)


def test_import_cycle():
    input_file = CASES_PATH / "cycle" / "a.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    error = process.stderr.decode("utf-8")

    assert_error(error, ["compiler: Cycle.\n"])
    assert process.returncode == 1


def test_cached_import():
    input_file = CASES_PATH / "cached" / "a.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert "using cached import for: tests/cases/cached/c.vnm" in output
