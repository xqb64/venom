import subprocess

from tests.util import VALGRIND_CMD, CASES_PATH
from tests.util import assert_output


def test_negate_variable():
    input_file = CASES_PATH / "negate.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [-5])
