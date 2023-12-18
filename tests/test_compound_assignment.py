import subprocess
import pytest
import textwrap

from tests.util import VALGRIND_CMD
from tests.util import assert_output


@pytest.mark.parametrize(
    "start, cond_op, end, step, op, intermediate",
    [
        [0, "<", 10, 2, "+", [0, 2, 4, 6, 8]],
        [10, ">", 0, 2, "-", [10, 8, 6, 4, 2]],
        [1, "!=", 64, 2, "*", [1, 2, 4, 8, 16, 32]],
        [64, "!=", 1, 2, "/", [64, 32, 16, 8, 4, 2]],
        [128, "!=", 1, 1, ">>", [128, 64, 32, 16, 8, 4, 2]],
        [1, "!=", 128, 1, "<<", [1, 2, 4, 8, 16, 32, 64]],
    ],
)
def test_compound_assignment(tmp_path, start, cond_op, end, step, op, intermediate):
    source = textwrap.dedent(
        """
        fn test_compound_assignment() {
          let x = %s;
          while (x %s %s) {
            print x;
            x %s= %s;
          }
          return 0;
        }
        test_compound_assignment();
        """
        % (start, cond_op, end, op, step)
    )

    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, intermediate)


@pytest.mark.parametrize(
    "left, op, right, expected",
    [
        [32, "&", 2, 0],
        [32, "|", 1, 33],
        [32, "^", 1, 33],
        [5, "%", 3, 2],
    ],
)
def test_compound_assignment_other(tmp_path, left, op, right, expected):
    source = textwrap.dedent(
        """
        fn test_compound_assignment() {
          let a = %s;
          a %s= %s;
          return a;
        }
        print test_compound_assignment();
        """
        % (left, op, right)
    )

    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [expected])
