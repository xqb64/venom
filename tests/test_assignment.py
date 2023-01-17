import subprocess
import pytest
import textwrap

from tests.util import VALGRIND_CMD
from tests.util import TWO_OPERANDS_GROUP


@pytest.mark.parametrize(
    "a, b",
    TWO_OPERANDS_GROUP,
)
def test_assignment_global(tmp_path, a, b):
    source = textwrap.dedent(
        f"""\
        let x = {a};
        print x;
        x = {b};
        print x;"""
    )

    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)
    
    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )
    for value in [a, b]:
        assert f"dbg print :: {value:.2f}\n".encode('utf-8') in process.stdout
        assert process.returncode == 0

    # the stack must end up empty
    assert process.stdout.endswith(b"stack: []\n")


@pytest.mark.parametrize(
    "a, b",
    TWO_OPERANDS_GROUP,
)
def test_assignment_func(tmp_path, a, b):
    source = textwrap.dedent(
        """
        fn main() {
            let x = %d;
            print x;
            x = %d;
            print x;
        }
        main();""" % (a, b)
    )

    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)
    
    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )
    for value in [a, b]:
        assert f"dbg print :: {value:.2f}\n".encode('utf-8') in process.stdout
        assert process.returncode == 0
    
    # the stack must end up empty
    assert process.stdout.endswith(b"stack: []\n")


def test_invalid_assignment(tmp_path):
    source = textwrap.dedent(
        """
        fn main() {
            let x = 3;
            1 = x;
        }
        main();
        """
    )

    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    assert b"Compiler error: Invalid assignment.\n" in process.stderr
    assert process.returncode == 1
