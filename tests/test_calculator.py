import subprocess
import itertools
import pytest

from tests.util import VALGRIND_CMD
from tests.util import TWO_OPERANDS_GROUP
from tests.util import THREE_OPERANDS_GROUP


@pytest.mark.parametrize(
    "a, b",
    TWO_OPERANDS_GROUP,
)
def test_calculator(tmp_path, a, b):
    for op in {'+', '-', '*', '/'}:
        source = f"print {a} {op} {b};"
        input_file = tmp_path / "input.vnm"
        input_file.write_text(source)
        expected = "%.2f" % eval(f"{a} {op} {b}")
        process = subprocess.run(
            VALGRIND_CMD + [input_file],
            capture_output=True,
        )
        assert f"dbg print :: {expected}\n".encode('utf-8') in process.stdout
        assert process.returncode == 0

        # the stack must end up empty
        assert process.stdout.endswith(b"stack: []\n")


@pytest.mark.parametrize(
    "a, b, c",
    THREE_OPERANDS_GROUP,
)
def test_calculator_grouping(tmp_path, a, b, c):
    for op, op2 in itertools.permutations({'+', '-', '*', '/'}, 2):
        source = f"print ({a} {op} {b}) {op2} {c};"
        input_file = tmp_path / "input.vnm"
        input_file.write_text(source)
        expected = "%.2f" % eval(f"({a} {op} {b}) {op2} {c}")
        process = subprocess.run(
            VALGRIND_CMD + [input_file],
            capture_output=True,
        )
        assert f"dbg print :: {expected}\n".encode('utf-8') in process.stdout
        assert process.returncode == 0

        # the stack must end up empty
        assert process.stdout.endswith(b"stack: []\n")
