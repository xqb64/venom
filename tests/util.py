import textwrap
from pathlib import Path
from types import NoneType

VALGRIND_CMD = [
    "valgrind",
    "--leak-check=full",
    "--show-leak-kinds=all",
    "--errors-for-leak-kinds=all",
    "--error-exitcode=1",
    "./venom",
]

CASES_PATH = Path(".") / "tests" / "cases"


class Object:
    def __init__(self, obj):
        if isinstance(obj, list):
            self.obj = [Object(x) for x in obj]
        else:
            self.obj = obj

    def __str__(self):
        match self.obj:
            case v if isinstance(v, bool):
                return "true" if v else "false"
            case v if isinstance(v, int) or isinstance(v, float):
                return str(v)
            case v if v is None:
                return "null"
            case v if isinstance(v, str):
                return f'"{v}"'
            case v if isinstance(v, Struct):
                return str(v)
            case v if isinstance(v, list):
                return "[%s]" % (", ".join(f"{str(x)}" for x in v),)
            case _:
                return "unknown"


class Struct:
    def __init__(self, name, **kwargs):
        self.name = name
        self.properties = kwargs

    def __str__(self):
        return "%s { %s }" % (
            self.name,
            ", ".join(f"{k}: {Object(v)}" for k, v in self.properties.items()),
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


def assert_output(output, debug_prints):
    debug_prints = [f"dbg print :: {Object(x) if not isinstance(x, str) else x}" for x in debug_prints]
    for debug_print in debug_prints:
        assert debug_print in output
        output = output[output.index(debug_print) + len(debug_print) :]


def assert_error(error, debug_prints):
    debug_prints = [str(Object(x)) if not isinstance(x, str) else x for x in debug_prints]
    for debug_print in debug_prints:
        assert debug_print in error
        error = error[error.index(debug_print) + len(debug_print) :]


def typestr(obj) -> str:
    if isinstance(obj, list):
        return "array"
    elif isinstance(obj, float):
        return "number"
    elif isinstance(obj, Struct):
        return "struct"
    elif isinstance(obj, NoneType):
        return "null"
    elif isinstance(obj, bool):
        return "boolean"
    elif isinstance(obj, float) or isinstance(obj, int):
        return "number"
    else:
        return "unknown"
