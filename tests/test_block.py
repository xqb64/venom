import subprocess
import pytest
import textwrap

from tests.util import VALGRIND_CMD
from tests.util import TWO_OPERANDS_GROUP


def test_block_func_param_inherited(tmp_path):
    source = textwrap.dedent(
        """
        fn main(x) {
            if (x == 0) {
                print x;
            }
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

    assert f"dbg print :: {0:.2f}\n".encode('utf-8') in process.stdout
    assert process.returncode == 0

    # null must remain on the stack because it's a void func
    assert f"stack: [null]".encode('utf-8') in process.stdout


def test_block_local_var_inherited(tmp_path):
    source = textwrap.dedent(
        """
        fn main(x) {
            let z = 3;
            if (x == 0) {
                print z;
            }
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

    assert f"dbg print :: {3:.2f}\n".encode('utf-8') in process.stdout
    assert process.returncode == 0

    # null must remain on the stack because it's a void func
    assert f"stack: [null]".encode('utf-8') in process.stdout


def test_block_undefined_var(tmp_path):
    source = textwrap.dedent(
        """
        fn main(x) {
            if (x == 0) {
                let z = 3;
                print z;
            }
            print z;
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

    assert f"Compiler error: Variable 'z' is not defined.\n".encode('utf-8') in process.stderr
    assert process.returncode == 1
