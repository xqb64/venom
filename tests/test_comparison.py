import subprocess
import pytest
import textwrap

from tests.util import VALGRIND_CMD
from tests.util import TWO_OPERANDS_GROUP


@pytest.mark.parametrize(
    "x, y",
    TWO_OPERANDS_GROUP,
)
def test_comparison_global(tmp_path, x, y):
    for op in {'>', '<', '>=', '<='}:
        source = textwrap.dedent(
            f"""\
            let x = {x};
            let y = {y};
            print x {op} y;
            """
        )
        input_file = tmp_path / "input.vnm"
        input_file.write_text(source)
        
        process = subprocess.run(
            VALGRIND_CMD + [input_file],
            capture_output=True,
        )

        expected = 'true' if eval(f"{x} {op} {y}") else 'false'

        assert f"dbg print :: {expected}\n".encode('utf-8') in process.stdout
        assert process.returncode == 0

        # the stack must end up empty
        assert process.stdout.endswith(b"stack: []\n")


@pytest.mark.parametrize(
    "x, y",
    TWO_OPERANDS_GROUP,
)
def test_comparison_func(tmp_path, x, y):
    for op in {'>', '<', '>=', '<='}:
        source = textwrap.dedent(
            """
            fn main() {
              let x = %d;
              let y = %d;
              print x %s y;
            }
            main();""" % (x, y, op)
        )

        input_file = tmp_path / "input.vnm"
        input_file.write_text(source)
        
        process = subprocess.run(
            VALGRIND_CMD + [input_file],
            capture_output=True,
        )

        expected = 'true' if eval(f"{x} {op} {y}") else 'false'

        assert f"dbg print :: {expected}\n".encode('utf-8') in process.stdout
        assert process.returncode == 0
        
        # the stack must end up empty
        assert process.stdout.endswith(b"stack: []\n")
