import subprocess
import pytest
import textwrap

from tests.util import VALGRIND_CMD

def test_func_undefined(tmp_path):
    source = textwrap.dedent(
      """
      fn main(x) {
        print x;
      }
      amain();
      """
    )
    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    expected = "Compiler error: Function 'amain' is not defined."

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    assert f"{expected}\n".encode('utf-8') in process.stderr
    assert process.returncode == 1


def test_func_wrong_argcount(tmp_path):
    source = textwrap.dedent(
      """
      fn main(x, y) {
        print x + y;
      }
      main(1);
      """
    )
    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    expected = "Compiler error: Function 'main' requires 2 arguments."

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    assert f"{expected}\n".encode('utf-8') in process.stderr
    assert process.returncode == 1
