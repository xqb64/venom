import subprocess
import pytest
import textwrap

from tests.util import VALGRIND_CMD


@pytest.mark.parametrize(
    "a, op, b, expected",
    [
        [8, "|", 1, 9],
        [15, "&", 1, 1],
        [15, "^", 2, 13],
        [1, "<<", 5, 32],
        [64, ">>", 2, 16],
    ],
)
def test_bitwise(tmp_path, a, op, b, expected):
    source = textwrap.dedent(
        """
        fn test_bitwise(a, b) {
          return a %s b;
        }
        print test_bitwise(%s, %s);
        """
        % (op, a, b)
    )

    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    output = process.stdout.decode("utf-8")

    assert f"dbg print :: {expected:.16g}" in output

    assert process.returncode == 0

    # the stack must end up empty because we're consuming the
    # boolean value in the while condition
    assert output.endswith("stack: []\n")


def test_bitwise_not(tmp_path):
    source = textwrap.dedent(
        """
        fn test_bitwise_not(a) {
          return ~a;
        }
        print test_bitwise_not(0);
        """
    )

    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    output = process.stdout.decode("utf-8")

    assert f"dbg print :: {1:.16g}" in output

    assert process.returncode == 0

    # the stack must end up empty because we're consuming the
    # boolean value in the while condition
    assert output.endswith("stack: []\n")
