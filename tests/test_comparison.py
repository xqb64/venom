import subprocess
import pytest
import textwrap

from tests.util import VALGRIND_CMD


@pytest.mark.parametrize(
    "x, y",
    [[1, 3.14], [3.14, 1], [3.14, 3.14]],
)
def test_comparison(tmp_path, x, y):
    for op in {">", "<", ">=", "<="}:
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
            check=True,
        )

        expected = "true" if eval(f"{x} {op} {y}") else "false"

        assert f"dbg print :: {expected}\n".encode("utf-8") in process.stdout
