import textwrap
import subprocess
import pytest

from tests.util import VALGRIND_CMD
from tests.util import typestr
from tests.util import Object


@pytest.mark.parametrize(
    "lhs, rhs",
    [
        [
            [1, 2, 3, "Hello, world!"],
            [True, False, None, "Hello, world!"],
        ]
    ],
)
def test_binary_op_leak(tmp_path, lhs, rhs):
    for op in (
        "+",
        "-",
        "*",
        "/",
    ):
        venom_lhs = Object(lhs)
        venom_rhs = Object(rhs)

        source = textwrap.dedent(
            f"""\
            fn main() {{
                let a = {venom_lhs};
                let b = {venom_rhs};
                print a {op} b;
                return 0;
            }}
            main();
            """
        )

        input_file = tmp_path / "input.vnm"
        input_file.write_text(source)

        process = subprocess.run(
            VALGRIND_CMD + [input_file],
            capture_output=True,
        )

        t1 = typestr(lhs)
        t2 = typestr(rhs)

        error_msg = f"vm: cannot '{op}' objects of types: '{t1}' and '{t2}'"

        decoded = process.stderr.decode("utf-8")

        assert error_msg in decoded
        assert process.returncode == 255
