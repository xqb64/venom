import subprocess
import pytest
import textwrap

from tests.util import VALGRIND_CMD
from tests.util import SINGLE_OPERAND_GROUP


@pytest.mark.parametrize(
    "x",
    SINGLE_OPERAND_GROUP,
)
def test_negate_global(tmp_path, x):
    for minus_count in range(10):
        source = textwrap.dedent(
            f"""\
            let x = {'-' * minus_count + str(x)};
            print x;
            """
        )
        input_file = tmp_path / "input.vnm"
        input_file.write_text(source)
        
        process = subprocess.run(
            VALGRIND_CMD + [input_file],
            capture_output=True,
        )

        assert ("dbg print :: " + 
            # for x == 0 and for every odd number of minuses,
            # the result is going to have a minus in front. 
            f"{'-' if (x == 0 and minus_count % 2 != 0) else ''}" +
            f"{eval('-' * minus_count + str(x)):.2f}\n").encode('utf-8') in process.stdout
        assert process.returncode == 0
    
        # the stack must end up empty
        assert f"stack: []".encode('utf-8') in process.stdout


@pytest.mark.parametrize(
    "x",
    SINGLE_OPERAND_GROUP,
)
def test_negate_func(tmp_path, x):
    for minus_count in range(10):
        source = textwrap.dedent(
            """
            fn main() {
                let x = %s;
                print x;
            }
            main();
            """ % ('-' * minus_count + str(x))
        )
        input_file = tmp_path / "input.vnm"
        input_file.write_text(source)
        
        process = subprocess.run(
            VALGRIND_CMD + [input_file],
            capture_output=True,
        )

        assert ("dbg print :: " + 
            # for x == 0 and for every odd number of minuses,
            # the result is going to have a minus in front. 
            f"{'-' if (x == 0 and minus_count % 2 != 0) else ''}" +
            f"{eval('-' * minus_count + str(x)):.2f}\n").encode('utf-8') in process.stdout
        assert process.returncode == 0
    
        # the stack must end up empty
        assert process.stdout.endswith(b"stack: []\n")


def test_negate_variable(tmp_path):
    source = textwrap.dedent(
        """
        fn main() {
            let x = 5;
            print -x;
        }
        main();
        """
    )
    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    assert "dbg print :: -5.00".encode('utf-8') in process.stdout
    assert process.returncode == 0 

    # the stack must end up empty
    assert process.stdout.endswith(b"stack: []\n")
