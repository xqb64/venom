import subprocess

from tests.util import VALGRIND_CMD
from tests.util import CASES_PATH

import pytest


@pytest.mark.parametrize(
    "path, errmsg",
    [
        ["break_outside_loop.vnm", "'break' statement outside the loop"],
        ["continue_outside_loop.vnm", "'continue' statement outside the loop"],
   ],
)
def test_loop_label_error_stmt(path, errmsg):
    process = subprocess.run(
        VALGRIND_CMD + [CASES_PATH / "errors" / "loop_label" / path],
        capture_output=True,
    )
    
    print(path)

    decoded = process.stderr.decode('utf-8')

    print(decoded)

    assert f"loop_labeler: {errmsg}" in decoded
    assert process.returncode == 255

