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
        assert f"dbg print :: {value:.2f}\n".encode("utf-8") in process.stdout
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
          return 0;
        }
        main();"""
        % (a, b)
    )

    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )
    for value in [a, b]:
        assert f"dbg print :: {value:.2f}\n".encode("utf-8") in process.stdout
        assert process.returncode == 0

    # the stack must end up empty
    assert process.stdout.endswith(b"stack: []\n")


def test_invalid_assignment(tmp_path):
    source = textwrap.dedent(
        """
        fn main() {
          let x = 3;
          1 = x;
          return 0;
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


def test_struct_property_assignment(tmp_path):
    source = textwrap.dedent(
        """
        struct spam {
          x;
          y;
        }
        fn main() {
          let egg = spam { x: 0, y: 0 };
          egg.x = 3;
          print egg.x;
          return 0;
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

    output = process.stdout.decode("utf-8")

    assert "dbg print :: 3.00\n" in output
    assert output.endswith("stack: []\n")

    assert process.returncode == 0


def test_struct_property_assignment_nested(tmp_path):
    source = textwrap.dedent(
        """
        struct spam {
          x;
          y;
        }
        fn main() {
          let egg = spam {
            x: 0,
            y: spam {
              x: 0,
              y: spam {
                x: 0,
                y: 0
              }
            }
          };
          egg.y.y.x = "Hello, world!";
          print egg.y.y.x;
          return 0;
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

    output = process.stdout.decode("utf-8")

    assert "dbg print :: Hello, world!\n" in output
    assert output.endswith("stack: []\n")

    assert process.returncode == 0
