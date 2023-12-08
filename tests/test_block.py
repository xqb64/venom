import subprocess
import textwrap

from tests.util import VALGRIND_CMD


def test_block_func_param_inherited(tmp_path):
    source = textwrap.dedent(
        """
        fn main(x) {
          if (x == 0) {
            print x;
          }
          return 0;
        }
        main(0);
        """
    )

    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    assert f"dbg print :: {0:.16g}\n".encode("utf-8") in process.stdout
    assert process.returncode == 0

    # the stack must end up empty
    assert process.stdout.endswith(b"stack: []\n")


def test_block_local_var_inherited(tmp_path):
    source = textwrap.dedent(
        """
        fn main(x) {
          let z = 3;
          if (x == 0) {
            print z;
          }
          return 0;
        }
        main(0);
        """
    )

    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    assert f"dbg print :: {3:.16g}\n".encode("utf-8") in process.stdout
    assert process.returncode == 0

    # the stack must end up empty
    assert process.stdout.endswith(b"stack: []\n")


def test_block_undefined_var(tmp_path):
    source = textwrap.dedent(
        """
        fn main(x) {
          if (x == 0) {
            let z = 3;
            print z;
          }
          print z;
          return 0;
        }
        main(0);
        """
    )

    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    assert (
        "Compiler error: Variable 'z' is not defined.\n".encode("utf-8")
        in process.stderr
    )
    assert process.returncode == 1


def test_block_return_value_remains_on_stack(tmp_path):
    source = textwrap.dedent(
        """
        fn main(x) {
          let z = 3;
          print x+z;
          return null;
        }
        let spam = main(4);
        print spam;
        """
    )

    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    output = process.stdout.decode("utf-8")

    asserts = [
        "dbg print :: 7\n",
        "dbg print :: null\n",
    ]

    for _assert in asserts:
        assert _assert in output
        output = output[output.index(_assert) + len(_assert) :]

    assert process.returncode == 0

    # the stack must end up empty because we're consuming the return value
    assert output.endswith("stack: []\n")


def test_block_return_value_gets_popped(tmp_path):
    source = textwrap.dedent(
        """
        fn main(x) {
          let z = 3;
          print x+z;
          return 0;
        }
        main(4);
        let egg = 0;
        while (egg < 5) {
          let wut = "Hello, world!";
          egg = egg+1;
          print wut;
        }
        """
    )

    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    output = process.stdout.decode("utf-8")

    asserts = [
        "dbg print :: 7\n",
        "dbg print :: Hello, world!\n",
        "dbg print :: Hello, world!\n",
        "dbg print :: Hello, world!\n",
        "dbg print :: Hello, world!\n",
        "dbg print :: Hello, world!\n",
    ]

    for _assert in asserts:
        assert _assert in output
        output = output[output.index(_assert) + len(_assert) :]

    assert process.returncode == 0

    # the stack must end up empty because we're consuming the
    # boolean value in the while condition
    assert output.endswith("stack: []\n")
