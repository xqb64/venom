import subprocess
from pathlib import Path

from tests.util import VALGRIND_CMD

RESULTS = {
    "assignment.vnm": {
        "debug_prints": [],
        "ends_with": "Compiler error: Invalid assignment.",
        "return_code": 1,
    },
    "big.vnm": {
        "debug_prints": [],
        "ends_with": "stack: []",
        "return_code": 0,
    },
    "break.vnm": {
        "debug_prints": [],
        "ends_with": "Compiler error: Variable 'y' is not defined.",
        "return_code": 1,
    },
    "breakcomplex.vnm": {
        "debug_prints": [
            "dbg print :: 1",
            "dbg print :: Hello, world!",
            "dbg print :: Hello, world!",
            "dbg print :: Hello, world!",
            "dbg print :: Hello, world!",
            "dbg print :: Hello, world!",
            "dbg print :: 3",
        ],
        "ends_with": "stack: []",
        "return_code": 0,
    },
    "call.vnm": {
        "debug_prints": [
            "dbg print :: 7",
        ],
        "ends_with": "stack: []",
        "return_code": 0,
    },
    "comparison.vnm": {
        "debug_prints": [
            "dbg print :: false",
        ],
        "ends_with": "stack: []",
        "return_code": 0,
    },
    "cursed_struct.vnm": {
        "debug_prints": [],
        "ends_with": "Compiler error: struct 'spam' has no property 'z'",
        "return_code": 1,
    },
    "doubleloop.vnm": {
        "debug_prints": [
            "dbg print :: Printing i...",
            "dbg print :: 1",
            "dbg print :: Printing i...",
            "dbg print :: 2",
        ],
        "ends_with": "stack: []",
        "return_code": 0,
    },
    "empty.vnm": {
        "debug_prints": [
            "dbg print :: 1",
        ],
        "ends_with": "stack: []",
        "return_code": 0,
    },
    "global_loop.vnm": {
        "debug_prints": [
            "dbg print :: 7",
            "dbg print :: Hello world!",
            "dbg print :: Hello world!",
            "dbg print :: Hello world!",
            "dbg print :: Hello world!",
            "dbg print :: Hello world!",
        ],
        "ends_with": "stack: []",
        "return_code": 0,
    },
    "if.vnm": {
        "debug_prints": [
            "dbg print :: null",
        ],
        "ends_with": "stack: []",
        "return_code": 0,
    },
    "logical.vnm": {
        "debug_prints": [
            "dbg print :: Run!",
        ],
        "ends_with": "stack: []",
        "return_code": 0,
    },
    "loop.vnm": {
        "debug_prints": [
            "dbg print :: 0",
            "dbg print :: 1",
            "dbg print :: 2",
        ],
        "ends_with": "stack: []",
        "return_code": 0,
    },
    "null.vnm": {
        "debug_prints": [
            "dbg print :: true",
        ],
        "ends_with": "stack: []",
        "return_code": 0,
    },
    "pytest.vnm": {
        "debug_prints": [
            "dbg print :: false",
        ],
        "ends_with": "stack: []",
        "return_code": 0,
    },
    "recursion.vnm": {
        "debug_prints": [
            "dbg print :: 0",
        ],
        "ends_with": "stack: []",
        "return_code": 0,
    },
    "simple_fn.vnm": {
        "debug_prints": [
            "dbg print :: 10",
        ],
        "ends_with": "stack: []",
        "return_code": 0,
    },
    "strcat.vnm": {
        "debug_prints": [
            "dbg print :: Hello, world!",
        ],
        "ends_with": "stack: []",
        "return_code": 0,
    },
    "try_this.vnm": {
        "debug_prints": [],
        "ends_with": "Compiler error: Variable 'x' is not defined.",
        "return_code": 1,
    },
    "undefined.vnm": {
        "debug_prints": [],
        "ends_with": "Compiler error: Variable 'z' is not defined.",
        "return_code": 1,
    },
    "while.vnm": {
        "debug_prints": [
            "dbg print :: Hello, world!",
            "dbg print :: Hello, world!",
            "dbg print :: Hello, world!",
            "dbg print :: null",
        ],
        "ends_with": "stack: []",
        "return_code": 0,
    },
}


def test_debug():
    for file, info in RESULTS.items():
        process = subprocess.run(
            VALGRIND_CMD + [Path(".") / "debug" / file],
            capture_output=True,
        )

        if info["return_code"] == 0:
            output = process.stdout.decode("utf-8")
            for debug_print in info["debug_prints"]:
                assert debug_print in output
                output = output[output.index(debug_print) + len(debug_print) :]
            assert output.endswith(info["ends_with"] + "\n")
        elif info["return_code"] == 1:
            assert (info["ends_with"] + "\n").encode("utf-8") in process.stderr

        assert process.returncode == info["return_code"]
