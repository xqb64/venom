import subprocess
import pytest
import textwrap

from tests.util import VALGRIND_CMD, Struct


@pytest.mark.parametrize(
    "a, b",
    [
        # Numbers
        (3.14, 3.14),
        (-3.14, 3.14),
        (-3.14, -3.14),
        (1024, -3.14),
        (1024, 1024),
        # Booleans
        ("true", "true"),
        ("true", "false"),
        ("false", "true"),
        ("false", "false"),
        # Nulls
        ("null", "null"),
        # Mixed
        ("true", "null"),
        ("null", "false"),
        ("null", Struct(name="spam", x=5, y=10)),
        (Struct(name="spam", x=5, y=10), "true"),
        (3.14, "true"),
        (10, "null"),
        (Struct(name="spam", x=5, y=10), 3.14),
        # Two structs of the same type and the same values.
        (Struct(name="spam", x=5, y=10), Struct(name="spam", x=5, y=10)),
        # Two structs of the same type and one different value.
    ],
)
def test_equality(tmp_path, a, b):
    source = textwrap.dedent(
        """
        fn main() {
          let a = %s;
          let b = %s;
          print a == b;
          return 0;
        }
        main();"""
    )

    current_source = source
    # If the two operands are both structs, and if their
    # types are different (have a different name), prepend
    # the source with the definitions of each of them, but
    # if they are of the same type, prepend the source with
    # the definition of one of them only
    if isinstance(a, Struct) and isinstance(b, Struct):
        if a.name == b.name:
            current_source = a.definition() + source
        else:
            current_source = a.definition() + b.definition() + source
    elif isinstance(a, Struct):
        current_source = a.definition() + source
    elif isinstance(b, Struct):
        current_source = b.definition() + source

    input_file = tmp_path / "input.vnm"
    input_file.write_text(current_source % (a, b))

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    expected = "true" if a == b else "false"

    assert f"dbg print :: {expected}\n".encode("utf-8") in process.stdout


def test_equality_same_object(tmp_path):
    source = textwrap.dedent(
        """
        struct spam {
          x;
          y;
        }
        fn main() {
          let a = spam { x: 3.14, y: 5.16 };
          let b = a;
          print a == b;
          return 0;
        }
        main();"""
    )

    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    expected = "true"

    assert f"dbg print :: {expected}\n".encode("utf-8") in process.stdout
