import subprocess
import pytest
import textwrap

from tests.util import VALGRIND_CMD
from tests.util import TWO_OPERANDS_GROUP


@pytest.mark.parametrize(
    "x, y",
    TWO_OPERANDS_GROUP,
)
def test_equality(tmp_path, x, y):
    source = textwrap.dedent(
        """
        fn main() {
          let x = %d;
          let y = %d;
          print x == y;
        }
        main();"""
        % (x, y)
    )

    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    expected = "true" if eval(f"{x} == {y}") else "false"

    assert f"dbg print :: {expected}\n".encode("utf-8") in process.stdout
    assert process.returncode == 0

    # the stack must end up empty
    assert process.stdout.endswith(b"stack: []\n")


def test_equality_two_structs(tmp_path):
    class Struct:
        def __init__(self, name, **kwargs):
            self.name = name
            self.properties = kwargs

        def __str__(self):
            return "%s { %s }" % (
                self.name,
                ", ".join(f"{k}: {str(v)}" for k, v in self.properties.items()),
            )
        
        def __eq__(self, other):
            return self.name == other.name and self.properties == other.properties

        def definition(self):
            return textwrap.dedent(
                """
                struct %s {
                    %s
                }
                """
                % (self.name, "".join(f"{k};" for k in self.properties.keys()))
            )

    source = textwrap.dedent(
        """
        fn main() {
          let a = %s;
          let b = %s;
          print a == b;
        }
        main();"""
    )

    structs = [
        # Two structs of the same type and the same values.
        (Struct(name="spam", x=5, y=10),
         Struct(name="spam", x=5, y=10)),

        # Two structs of the same type and one different value.
        (Struct(name="spam", x=5, y=10),
        Struct(name="spam", x=3, y=10)),

        # Two structs of the same type with one boolean.
        (Struct(name="spam", x="true", y=10),
         Struct(name="spam", x="true", y=10)),

        (Struct(name="spam", x="true", y=10),
         Struct(name="spam", x="false", y=10)),

        # Two structs of the same type with one null.
        (Struct(name="spam", x=5, y="null"),
        Struct(name="spam", x=5, y="null")),

        (Struct(name="spam", x=5, y="null"),
         Struct(name="spam", x=5, y="false")),

        # Two structs with different types but same values.
        (Struct(name="spam", x=5, y=10),
        Struct(name="egg", x=5, y=10)),

        # Two structs with same types containing nested structs.
        (Struct(name="spam", x=5, y=Struct(name="spam", x=32, y=64)),
         Struct(name="spam", x=5, y=Struct(name="spam", x=32, y=64))),

        (Struct(
            name="spam",
            x=5,
            y=Struct(
                name="spam",
                x=Struct(
                    name="spam", x=128, y=3.14
                ),
                y=64)
            ),
        Struct(
            name="spam",
            x=5,
            y=Struct(
                name="spam",
                x=Struct(
                    name="spam", x=128, y=3.14
                ),
                y=64)
            ),
        ),
    ]

    for a, b in structs:
        current_source = ""
        if a.name == b.name:
            current_source = a.definition() + source
        else:
            current_source = a.definition() + b.definition() + source

        input_file = tmp_path / "input.vnm"
        input_file.write_text(current_source % (a, b))

        print(input_file.read_text())

        process = subprocess.run(
            VALGRIND_CMD + [input_file],
            capture_output=True,
        )

        expected = 'true' if a == b else 'false'

        assert f"dbg print :: {expected}\n".encode("utf-8") in process.stdout
        assert process.returncode == 0

        # the stack must end up empty
        assert f"stack: []".encode("utf-8") in process.stdout


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

        expected = "true" if is_equal else "false"

        assert f"dbg print :: {expected}\n".encode("utf-8") in process.stdout
        assert process.returncode == 0

        # the stack must end up empty
        assert process.stdout.endswith(b"stack: []\n")


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

    expected = "true"

    assert f"dbg print :: {expected}\n".encode("utf-8") in process.stdout
    assert process.returncode == 0

    # the stack must end up empty
    assert process.stdout.endswith(b"stack: []\n")
