import subprocess
import pytest
import textwrap

from tests.util import VALGRIND_CMD
from tests.util import TWO_OPERANDS_GROUP


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
        return (
            isinstance(other, Struct)
            and self.name == other.name
            and self.properties == other.properties
        )

    def definition(self):
        return textwrap.dedent(
            """
            struct %s {
                %s
            }
            """
            % (self.name, "".join(f"{k};" for k in self.properties.keys()))
        )


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
        (Struct(name="spam", x=5, y=10), Struct(name="spam", x=3, y=10)),
        # Two structs of the same type with one boolean.
        (Struct(name="spam", x="true", y=10), Struct(name="spam", x="true", y=10)),
        (Struct(name="spam", x="true", y=10), Struct(name="spam", x="false", y=10)),
        # Two structs of the same type with one null.
        (Struct(name="spam", x=5, y="null"), Struct(name="spam", x=5, y="null")),
        (Struct(name="spam", x=5, y="null"), Struct(name="spam", x=5, y="false")),
        # Two structs with different types but same values.
        (Struct(name="spam", x=5, y=10), Struct(name="egg", x=5, y=10)),
        # Two structs with same types containing nested structs.
        (
            Struct(name="spam", x=5, y=Struct(name="spam", x=32, y=64)),
            Struct(name="spam", x=5, y=Struct(name="spam", x=32, y=64)),
        ),
        (
            Struct(
                name="spam",
                x=5,
                y=Struct(name="spam", x=Struct(name="spam", x=128, y=3.14), y=64),
            ),
            Struct(
                name="spam",
                x=5,
                y=Struct(name="spam", x=Struct(name="spam", x=128, y=3.14), y=64),
            ),
        ),
    ],
)
def test_equality(tmp_path, a, b):
    source = textwrap.dedent(
        """
        fn main() {
          let a = %s;
          let b = %s;
          print a == b;
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
    )

    expected = "true" if a == b else "false"

    assert f"dbg print :: {expected}\n".encode("utf-8") in process.stdout
    assert process.returncode == 0

    # the stack must end up empty
    assert f"stack: []".encode("utf-8") in process.stdout
