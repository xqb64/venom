import subprocess

from tests.util import VALGRIND_CMD, CASES_PATH
from tests.util import assert_output


def test_array():
    input_file = CASES_PATH / "array.vnm"

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
        check=True,
    )

    output = process.stdout.decode("utf-8")

    assert_output(output, [128])
