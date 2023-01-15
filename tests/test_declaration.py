import textwrap
import subprocess
import pytest

from tests.util import VALGRIND_CMD
from tests.util import SINGLE_OPERAND_GROUP
from tests.util import TWO_OPERANDS_GROUP


@pytest.mark.parametrize(
    "value",
    SINGLE_OPERAND_GROUP,
)
def test_declarations(tmp_path, value):
    source = textwrap.dedent(
        f"""
        let x = {value};
        print x;"""
    )

    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    expected = f"{value:.2f}"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    assert f"dbg print :: {expected}\n".encode('utf-8') in process.stdout
    assert process.returncode == 0

    # the stack must end up empty
    assert b"stack: []\n" in process.stdout


@pytest.mark.parametrize(
    "value",
    SINGLE_OPERAND_GROUP,
)
def test_printing_declared_variable(tmp_path, value):
    source = textwrap.dedent(
        f"""
        let x = {value};
        print x + 1;"""
    )

    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    expected = '%.2f' % (value + 1)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    assert f"dbg print :: {expected}\n".encode('utf-8') in process.stdout
    assert process.returncode == 0

    # the stack must end up empty
    assert b"stack: []\n" in process.stdout


@pytest.mark.parametrize(
    "x, y",
    TWO_OPERANDS_GROUP,
)
def test_printing_declared_variables(tmp_path, x, y):
   for op in {'+', '-', '*', '/'}:
        source = textwrap.dedent(
            f"""
            let x = {x};
            let y = {y};
            print x {op} y + 2;"""
        )

        input_file = tmp_path / "input.vnm"
        input_file.write_text(source)

        expected = "%.2f" % eval(f"{x} {op} {y} + 2")

        process = subprocess.run(
            VALGRIND_CMD + [input_file],
            capture_output=True,
        )

        assert f"dbg print :: {expected}\n".encode('utf-8') in process.stdout
        assert process.returncode == 0

        # the stack must end up empty
        assert b"stack: []\n" in process.stdout


@pytest.mark.parametrize(
    "a, b",
    TWO_OPERANDS_GROUP,
)
def test_declarations_with_expressions(tmp_path, a, b):
    for op in {'+', '-', '*', '/'}:
        source = textwrap.dedent(
            f"""
            let x = {a} {op} {b};
            print x;"""
        )

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
        assert b"stack: []\n" in process.stdout


@pytest.mark.parametrize(
    "a, b",
    TWO_OPERANDS_GROUP,
)
def test_reuse_declaration(tmp_path, a, b):
    for op in {'+', '-', '*', '/'}:
        source = textwrap.dedent(
            f"""
            let x = {a} {op} {b};
            print x;
            let x = {a} {op} {b};
            print x;"""
        )

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
        assert b"stack: []\n" in process.stdout
