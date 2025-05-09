import textwrap
import subprocess
import pytest

from tests.util import VALGRIND_CMD
from tests.util import typestr
from tests.util import Object
from tests.util import Struct

@pytest.mark.parametrize(
    "lhs, rhs",
    [
        [
            [1, 2, 3, "Hello, world!"],
            [True, False, None, "Hello, world!"],
        ],
        [
            Struct(name="spam", x=1, y="Hello, world!"),
            Struct(name="eggs", a=True, b=None), 
        ]
    ],
)
def test_binary_op_leak(tmp_path, lhs, rhs):
    for op in (
        "+",
        "-",
        "*",
        "/",
        "%",
        ">",
        "<",
        ">=",
        "<=",
        "&",
        "^",
        "|",
        ">>",
        "<<",
        "++",
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

        current_source = source

        if isinstance(lhs, Struct):
            current_source = lhs.definition() + current_source

        if isinstance(rhs, Struct):
            current_source = rhs.definition() + current_source

        input_file = tmp_path / "input.vnm"
        input_file.write_text(current_source)

        process = subprocess.run(
            VALGRIND_CMD + [input_file],
            capture_output=True,
        )

        t1 = typestr(lhs)
        t2 = typestr(rhs)

        # This little dance is because of the RISC-ness of the Venom vm.
        if op == ">=":
            op = "<"
        elif op == "<=":
            op = ">"

        error_msg = f"vm: cannot '{op}' objects of types: '{t1}' and '{t2}'"

        decoded = process.stderr.decode("utf-8")

        assert error_msg in decoded
        assert process.returncode == 255


@pytest.mark.parametrize(
    "x",
    [
        [1, 2, 3, "Hello, world!"],
        Struct(name="spam", x=1, y="Hello, world!"),
    ],
)
def test_unary_leak(tmp_path, x):
    for op in (
        "-",
        "~",
        "!",
   ):
        venom_x = Object(x)

        source = textwrap.dedent(
            f"""\
            fn main() {{
                let x = {venom_x};
                print {op}x;
                return 0;
            }}
            main();
            """
        )

        current_source = source

        if isinstance(x, Struct):
            current_source = x.definition() + current_source

        input_file = tmp_path / "input.vnm"
        input_file.write_text(current_source)

        process = subprocess.run(
            VALGRIND_CMD + [input_file],
            capture_output=True,
        )

        t = typestr(x)

        error_msg = f"vm: cannot '{op}' objects of type: '{t}'"

        decoded = process.stderr.decode("utf-8")

        assert error_msg in decoded
        assert process.returncode == 255

@pytest.mark.parametrize(
    "x",
    [
        None,
        True,
        12345,
        Struct(name="spam", x=1, y="Hello, world!"),
    ],
)
def test_len_leak(tmp_path, x):
    venom_x = Object(x)
 
    source = textwrap.dedent(
         f"""\
         fn main() {{
             let x = {venom_x};
             print len(x);
             return 0;
         }}
         main();
         """
     )
 
    current_source = source
 
    if isinstance(x, Struct):
        current_source = x.definition() + current_source
 
    input_file = tmp_path / "input.vnm"
    input_file.write_text(current_source)
 
    process = subprocess.run(
       VALGRIND_CMD + [input_file],
       capture_output=True,
    )
 
    t = typestr(x)
 
    error_msg = f"vm: cannot 'len()' objects of type: '{t}'"
        
    decoded = process.stderr.decode("utf-8")
 
    assert error_msg in decoded
    assert process.returncode == 255


@pytest.mark.parametrize(
    "obj, attr",
    [
        [None, "spam"],
        [True, "spam"],
        [12345, "spam"],
    ],
)
def test_hasattr_leak(tmp_path, obj, attr):
    venom_obj = Object(obj)
 
    source = textwrap.dedent(
         f"""\
         fn main() {{
             let x = {venom_obj};
             print hasattr(x, {Object(attr)});
             return 0;
         }}
         main();
         """
     )
 
    current_source = source

    if isinstance(obj, Struct):
        current_source = obj.definition() + current_source
    
    print(current_source)
 
    input_file = tmp_path / "input.vnm"
    input_file.write_text(current_source)
 
    process = subprocess.run(
       VALGRIND_CMD + [input_file],
       capture_output=True,
    )
 
    t = typestr(obj)

    print(t)

    error_msg = f"vm: cannot 'hasattr()' objects of type: '{t}'"
        
    decoded = process.stderr.decode("utf-8")
 
    assert error_msg in decoded
    assert process.returncode == 255

@pytest.mark.parametrize(
    "obj, attr",
    [
        [None, "spam"],
        [True, "spam"],
        [12345, "spam"],
    ],
)
def test_getattr_leak(tmp_path, obj, attr):
    venom_obj = Object(obj)
 
    source = textwrap.dedent(
         f"""\
         fn main() {{
             let x = {venom_obj};
             print getattr(x, {Object(attr)});
             return 0;
         }}
         main();
         """
     )
 
    current_source = source

    if isinstance(obj, Struct):
        current_source = obj.definition() + current_source
    
    print(current_source)
 
    input_file = tmp_path / "input.vnm"
    input_file.write_text(current_source)
 
    process = subprocess.run(
       VALGRIND_CMD + [input_file],
       capture_output=True,
    )
 
    t = typestr(obj)

    print(t)

    error_msg = f"vm: cannot 'getattr()' objects of type: '{t}'"
        
    decoded = process.stderr.decode("utf-8")
 
    assert error_msg in decoded
    assert process.returncode == 255


@pytest.mark.parametrize(
    "obj, attr, val",
    [
        [None, "spam", "Hello world!"],
        [True, "spam", Struct(name="spam", a=1, b="Goodbye")],
        [12345, "spam", None],
    ],
)
def test_setattr_leak(tmp_path, obj, attr, val):
    venom_obj = Object(obj)
 
    source = textwrap.dedent(
         f"""\
         fn main() {{
             let x = {venom_obj};
             setattr(x, {Object(attr)}, {Object(val)});
             print x.{attr};
             return 0;
         }}
         main();
         """
     )
 
    current_source = source

    if isinstance(obj, Struct):
        current_source = obj.definition() + current_source
    
    if isinstance(val, Struct):
        current_source = val.definition() + current_source

    print(current_source)
 
    input_file = tmp_path / "input.vnm"
    input_file.write_text(current_source)
 
    process = subprocess.run(
       VALGRIND_CMD + [input_file],
       capture_output=True,
    )
 
    t = typestr(obj)

    print(t)

    error_msg = f"vm: cannot 'setattr()' objects of type: '{t}'"
        
    decoded = process.stderr.decode("utf-8")
 
    assert error_msg in decoded
    assert process.returncode == 255


@pytest.mark.parametrize(
    "obj",
    [
        None,
        12345,
        "Hello, world!",
        Struct(name="spam", a=None, b="foobar"),
    ],
)
def test_assert_leak(tmp_path, obj):
    venom_obj = Object(obj)
 
    source = textwrap.dedent(
         f"""\
         fn main() {{
             let x = {venom_obj};
             assert(x);
             return 0;
         }}
         main();
         """
     )
 
    current_source = source

    if isinstance(obj, Struct):
        current_source = obj.definition() + current_source
    
    print(current_source)
 
    input_file = tmp_path / "input.vnm"
    input_file.write_text(current_source)
 
    process = subprocess.run(
       VALGRIND_CMD + [input_file],
       capture_output=True,
    )
 
    t = typestr(obj)

    print(t)

    error_msg = f"vm: cannot 'assert()' objects of type: '{t}'"
        
    decoded = process.stderr.decode("utf-8")
 
    assert error_msg in decoded
    assert process.returncode == 255


def test_assert_leak_failed_assertion(tmp_path):
    obj = Struct(name="spam", a=1, b="Hello world!")
    venom_obj = Object(obj)
 
    source = textwrap.dedent(
         f"""\
         fn main() {{
             let x = {venom_obj};
             assert(false);
             return 0;
         }}
         main();
         """
     )
 
    current_source = source

    if isinstance(obj, Struct):
        current_source = obj.definition() + current_source
    
    print(current_source)
 
    input_file = tmp_path / "input.vnm"
    input_file.write_text(current_source)
 
    process = subprocess.run(
       VALGRIND_CMD + [input_file],
       capture_output=True,
    )
 
    t = typestr(obj)

    print(t)

    error_msg = f"vm: assertion failed"
        
    decoded = process.stderr.decode("utf-8")
 
    assert error_msg in decoded
    assert process.returncode == 255

@pytest.mark.parametrize(
    "obj",
    [
        None,
        12345,
        "Hello, world!",
    ],
)
def test_callmethod_leak(tmp_path, obj):
    venom_obj = Object(obj)
 
    source = textwrap.dedent(
         f"""\
         fn main() {{
             let x = {venom_obj};
             print x.say_hi();
             return 0;
         }}
         main();
         """
     )
 
    current_source = source

    if isinstance(obj, Struct):
        current_source = obj.definition() + current_source
    
    print(current_source)
 
    input_file = tmp_path / "input.vnm"
    input_file.write_text(current_source)
 
    process = subprocess.run(
       VALGRIND_CMD + [input_file],
       capture_output=True,
    )
 
    t = typestr(obj)

    print(t)

    error_msg = f"vm: cannot call objects of type: '{t}'"
        
    decoded = process.stderr.decode("utf-8")
 
    assert error_msg in decoded
    assert process.returncode == 255


def test_callmethod_leak2(tmp_path):
    obj = Struct(name="spam", a=1, b="Hello, world!")
    venom_obj = Object(obj)
 
    source = textwrap.dedent(
         f"""\
         fn main() {{
             let x = {venom_obj};
             print x.say_hi();
             return 0;
         }}
         main();
         """
     )
 
    current_source = source

    if isinstance(obj, Struct):
        current_source = obj.definition() + current_source
    
    print(current_source)
 
    input_file = tmp_path / "input.vnm"
    input_file.write_text(current_source)
 
    process = subprocess.run(
       VALGRIND_CMD + [input_file],
       capture_output=True,
    )
 
    t = obj.name
    print(t)

    error_msg = f"vm: method 'say_hi' is not defined on struct: '{t}'"
        
    decoded = process.stderr.decode("utf-8")
 
    assert error_msg in decoded
    assert process.returncode == 255


