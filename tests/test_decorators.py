import subprocess

from tests.util import VALGRIND_CMD, CASES_PATH
from tests.util import assert_output


def test_first_class_citizen():
    input_file = CASES_PATH / "deco1.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [16, 1024, 36])


def test_first_class_citizen2():
    input_file = CASES_PATH / "deco2.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [16, 1024, 7])


def test_first_class_citizen3():
    input_file = CASES_PATH / "deco3.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [16, 1024, 37])


def test_first_class_citizen4():
    input_file = CASES_PATH / "deco4.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [16, 1024, 35])


def test_first_class_citizen5():
    input_file = CASES_PATH / "deco5.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [16, 1024, 130])


def test_proper_decorator():
    input_file = CASES_PATH / "proper_deco.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [2])
