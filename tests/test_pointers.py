import subprocess

from tests.util import VALGRIND_CMD, CASES_PATH
from tests.util import assert_output


def test_pointers_01():
    input_file = CASES_PATH / "ptr01.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [14])


def test_pointers_02():
    input_file = CASES_PATH / "ptr02.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, ["Hello, world!"])


def test_pointers_03():
    input_file = CASES_PATH / "ptr03.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, ["Hello, world!"])


def test_pointers_04():
    input_file = CASES_PATH / "ptr04.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    dbg_prints = [
        "w is:",
        "PTR",
        "*w is:",
        "false",
        "thing is:",
        "PTR",
        "*thing is:",
        "PTR",
        "**thing is:",
        "true",
        "true",
    ]

    assert_output(output, dbg_prints)


def test_pointers_05():
    input_file = CASES_PATH / "ptr05.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [3])


def test_pointers_06():
    input_file = CASES_PATH / "ptr06.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [4096])


def test_pointers_07():
    input_file = CASES_PATH / "ptr07.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [2048])


def test_pointers_08():
    input_file = CASES_PATH / "ptr08.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [2048])


def test_pointers_09():
    input_file = CASES_PATH / "ptr09.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [3.14])


def test_pointers_10():
    input_file = CASES_PATH / "ptr10.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [1.23])


def test_pointers_linked_list():
    input_file = CASES_PATH / "linked_list.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [3.14, False, "Hello, world!"])
