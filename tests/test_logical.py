import subprocess
import pytest
import textwrap

from tests.util import VALGRIND_CMD


@pytest.mark.parametrize(
    "a, b, if_a_equals, op, if_b_equals, should_print",
    [
        # Logical AND
        (32, 64, 32, "&&", 64, "Run!"),
        (32, 64, 16, "&&", 64, "Shouldn't run!"),
        (32, 64, 32, "&&", 16, "Shouldn't run!"),
        (32, 64, 128, "&&", 256, "Shouldn't run!"),
        # Logical OR
        (32, 64, 32, "||", 64, "Run!"),
        (32, 64, 16, "||", 64, "Run!"),
        (32, 64, 32, "||", 16, "Run!"),
        (32, 64, 128, "||", 256, "Shouldn't run!"),
    ],
)
def test_logical(tmp_path, a, b, if_a_equals, op, if_b_equals, should_print):
    source = textwrap.dedent(
        """\
        fn main() {
          let x = %s;
          let y = %s;
          if (x == %s %s y == %s) {
            print "Run!";
          } else {
            print "Shouldn't run!";
          }
          return 0;
        }
        main();
        """
    )
    input_file = tmp_path / "input.vnm"
    input_file.write_text(source % (a, b, if_a_equals, op, if_b_equals))

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    expected = should_print

    assert f"dbg print :: {expected}\n".encode("utf-8") in process.stdout
    assert process.returncode == 0
