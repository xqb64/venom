import subprocess
import textwrap

from tests.util import VALGRIND_CMD


def test_strcat(tmp_path):
    source = textwrap.dedent(
        """
        fn main() {
          let s1 = "Hello, ";
          let s2 = "world!";
          print s1 ++ s2;
          return 0;
        }
        main();"""
    )
    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    expected = "Hello, world!"

    assert f"dbg print :: {expected}\n".encode("utf-8") in process.stdout
    assert process.returncode == 0

    # the stack must end up empty
    assert "stack: []".encode("utf-8") in process.stdout
