import subprocess
import pytest
import textwrap

from tests.util import VALGRIND_CMD
from tests.util import TWO_OPERANDS_GROUP


@pytest.mark.parametrize(
    "x, y",
    TWO_OPERANDS_GROUP,
)
def test_equality_global(tmp_path, x, y):
    source = textwrap.dedent(
        f"""\
        let x = {x};
        let y = {y};
        print x == y;
        """
    )
    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)
    
    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    expected = 'true' if eval(f"{x} == {y}") else 'false'

    assert f"dbg print :: {expected}\n".encode('utf-8') in process.stdout
    assert process.returncode == 0

    # the stack must end up empty because we're not in a func
    assert f"stack: []".encode('utf-8') in process.stdout


@pytest.mark.parametrize(
    "x, y",
    TWO_OPERANDS_GROUP,
)
def test_equality_func(tmp_path, x, y):
    source = textwrap.dedent(
        """
        fn main() {
            let x = %d;
            let y = %d;
            print x == y;
        }
        main();""" % (x, y)
    )

    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)
    
    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    expected = 'true' if eval(f"{x} == {y}") else 'false'

    assert f"dbg print :: {expected}\n".encode('utf-8') in process.stdout
    assert process.returncode == 0
    
    # null must remain on the stack because it's a void func
    assert f"stack: [null]".encode('utf-8') in process.stdout


def test_equality_two_structs(tmp_path):
    source = textwrap.dedent(
        """
        struct spam {
            x;
            y;
        }

        fn main() {
            let a = %s;
            let b = %s;
            print a == b;
        }
        
        main();"""
    )

    structs = [
        ("spam { x: 5, y: 10 }", "spam { x: 5, y: 10 }", True),
        ("spam { x: 5, y: 10 }", "spam { x: 5, y: 0 }", False),
        ('spam { x: 5, y: "Hello, world!" }', 'spam { x: 5, y: "Hello, world!" }', True),
        ('spam { x: 5, y: "Hello, world!" }', 'spam { x: 5, y: "Bye there!" }', False),
        ("spam { x: null, y: null }", "spam { x: null, y: null }", True),
        ("spam { x: true, y: false }", "spam { x: true, y: false }", True),
        ("spam { x: true, y: null }", "spam { x: true, y: false }", False),
        ('spam { x: 5, y: spam { x: 3, y: 6 } }', 'spam { x: 5, y: spam { x: 3, y: 6 } }', True),
        ('spam { x: 5, y: spam { x: 3, y: 6 } }', 'spam { x: 5, y: spam { x: 12, y: 6 } }', False),
        ("spam { x: 5, y: 10 }", 'spam { x: "Hello, world!", y: 10 }', False),
    ]

    for a, b, is_equal in structs:
        input_file = tmp_path / "input.vnm"
        input_file.write_text(source % (a, b))
        
        process = subprocess.run(
            VALGRIND_CMD + [input_file],
            capture_output=True,
        )

        expected = 'true' if is_equal else 'false'

        assert f"dbg print :: {expected}\n".encode('utf-8') in process.stdout
        assert process.returncode == 0
        
        # null must remain on the stack because it's a void func
        assert f"stack: [null]".encode('utf-8') in process.stdout


def test_equality_booleans(tmp_path):
    source = textwrap.dedent(
        """
        fn main() {
            let a = %s;
            let b = %s;
            print a == b;
        }
        
        main();"""
    )

    pairs = [
        ("true", "true", True),
        ("true", "false", False),
        ("false", "true", False),
        ("false", "false", True),
    ]

    for a, b, is_equal in pairs:
        input_file = tmp_path / "input.vnm"
        input_file.write_text(source % (a, b))
        
        process = subprocess.run(
            VALGRIND_CMD + [input_file],
            capture_output=True,
        )

        expected = 'true' if is_equal else 'false'

        assert f"dbg print :: {expected}\n".encode('utf-8') in process.stdout
        assert process.returncode == 0
        
        # null must remain on the stack because it's a void func
        assert f"stack: [null]".encode('utf-8') in process.stdout


def test_equality_nulls(tmp_path):
    source = textwrap.dedent(
        """
        fn main() {
            let a = null;
            let b = null;
            print a == b;
        }
        
        main();"""
    )

    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)
    
    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    expected = 'true'

    assert f"dbg print :: {expected}\n".encode('utf-8') in process.stdout
    assert process.returncode == 0
    
    # null must remain on the stack because it's a void func
    assert f"stack: [null]".encode('utf-8') in process.stdout