import subprocess
import pytest
import textwrap

from tests.util import VALGRIND_CMD

def test_pointers_deref_set(tmp_path):
    source = textwrap.dedent(
        """
        fn change_thing(thing) {
          *thing = *thing + *thing + 8;
        }
        fn main() {
          let egg = 3;
          change_thing(&egg);
          print egg;
        }
        main();"""
    )
    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    expected = "14.00"

    assert f"dbg print :: {expected}\n".encode("utf-8") in process.stdout
    assert process.returncode == 0

    # the stack must end up empty
    assert f"stack: []".encode("utf-8") in process.stdout


def test_pointers_arrow_set(tmp_path):
    source = textwrap.dedent(
        """
        struct node {
          next;
          value;
        }
        fn change_value(x) {
          x->value = "Hello, world!";
        }
        fn main() {
          let egg = node { next: null, value: 3.14 };
          change_value(&egg);
          print egg.value;
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
    assert f"stack: []".encode("utf-8") in process.stdout


def test_pointers_chained(tmp_path):
    source = textwrap.dedent(
        """
        struct node {
          next;
          value;
        }
        fn change_value(n) {
          n->next->next.value = "Hello, world!";
        }
        fn main() {
          let a = node { next: null, value: 3.14 };
          let b = node { next: a, value: false };
          let c = node { next: &b, value: 1024 };
          change_value(&c);
          print a.value;
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
    assert f"stack: []".encode("utf-8") in process.stdout


def test_pointers_double_deref(tmp_path):
    source = textwrap.dedent(
        """
        struct node {
          next;
          value;
        }
        
        fn change_thing(thing) {
          **thing = true;
          print "thing is:";
          print thing;
          print "*thing is:";
          print *thing;
          print "**thing is:";
          print **thing;
        }
        
        fn main() {
          let egg = node {
            value: 3.14,
            next: node {
              value: "Hello, world!",
              next: node {
                value: false,
                next: null
              }
            }
          };
          
          let w = &egg.next.next.value;
          print "w is:";
          print w;
          print "*w is:";
          print *w;
          change_thing(&w);
          print *w;
        }
        
        main();"""
    )
    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    dbg_prints = [
        "w is:",
        "PTR",
        "*w is:",
        "false",
        "thing is:",
        "PTR",
        "*thing is:",
        "PTR",
        "**thing is:",
        "true",
        "true",
    ]

    output = process.stdout.decode('utf-8')
    for debug_print in dbg_prints:
        assert debug_print in output
        output = output[output.index(debug_print) + len(debug_print) if not debug_print.startswith("PTR") else 22:]

    assert process.returncode == 0

    # the stack must end up empty
    assert f"stack: []".encode("utf-8") in process.stdout


def test_pointers_werid(tmp_path):
    source = textwrap.dedent(
        """
        struct node {
          next;
          value;
        }
        fn preent(thing) {
          print thing->next->value;
        }
        fn main() {
          let bla = 3;
          let egg = node { next: null, value: &bla };
          let lol = node { next: &egg, value: 3.14 };
          let z = &lol.next->value;
          print **z;
        }
        main();"""
    )
    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    expected = "3.00"

    assert f"dbg print :: {expected}\n".encode("utf-8") in process.stdout
    assert process.returncode == 0

    # the stack must end up empty
    assert f"stack: []".encode("utf-8") in process.stdout


def test_pointers_linked_list(tmp_path):
    source = textwrap.dedent(
        """
        struct node {
          next;
          value;
        }
        fn list_preent(list) {
          let current = *list;
          while (current != null) {
            print current.value;
            current = current.next;
          }
        }
        fn list_insert(list, item) {
          let new_node = node { next: null, value: item };
          if (*list == null) {
            *list = new_node;
          } else {
            let current = list;
            while (current->next != null) {
              current = &current->next;
            }
            current->next = new_node;
          }
        }
        fn main() {
          let list = null;
          list_insert(&list, 3.14);
          list_insert(&list, false);
          list_insert(&list, "Hello, world!");
          list_preent(&list);
        }
        main();"""
    )
    input_file = tmp_path / "input.vnm"
    input_file.write_text(source)

    process = subprocess.run(
        VALGRIND_CMD + [input_file],
        capture_output=True,
    )

    dbg_prints = [
        "dbg print :: 3.14",
        "dbg print :: false",
        "dbg print :: Hello, world!",
    ]

    output = process.stdout.decode('utf-8')
    for debug_print in dbg_prints:
        assert debug_print in output
        output = output[output.index(debug_print) + len(debug_print):]

    assert process.returncode == 0

    # the stack must end up empty
    assert f"stack: []".encode("utf-8") in process.stdout


def test_pointers_arrow_set(tmp_path):
    source = textwrap.dedent(
        """
        struct node {
          next;
          value;
        }
        fn change_value(x) {
          x->value = "Hello, world!";
        }
        fn main() {
          let egg = node { next: null, value: 3.14 };
          change_value(&egg);
          print egg.value;
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
    assert f"stack: []".encode("utf-8") in process.stdout


def test_pointers_global_deref(tmp_path):
    source = textwrap.dedent(
        """
        let x = 1024;
        fn preent_global(global) {
          print *global + *global + 2048;
        }
        fn main() {
          preent_global(&x);
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

    expected = "4096.00"

    assert f"dbg print :: {expected}\n".encode("utf-8") in process.stdout
    assert process.returncode == 0

    # the stack must end up empty
    assert f"stack: []".encode("utf-8") in process.stdout


def test_pointers_global_deref_set(tmp_path):
    source = textwrap.dedent(
        """
        let x = 1024;
        fn change_global(global) {
          *global = 2048;
        }
        fn main() {
          change_global(&x);
          print x;
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

    expected = "2048.00"

    assert f"dbg print :: {expected}\n".encode("utf-8") in process.stdout
    assert process.returncode == 0

    # the stack must end up empty
    assert f"stack: []".encode("utf-8") in process.stdout


def test_pointers_global_double_deref_set(tmp_path):
    source = textwrap.dedent(
        """
        let x = 1024;
        fn change_global(global) {
          **global = 2048;
        }
        fn main() {
          let z = &x;
          change_global(&z);
          print x;
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

    expected = "2048.00"

    assert f"dbg print :: {expected}\n".encode("utf-8") in process.stdout
    assert process.returncode == 0

    # the stack must end up empty
    assert f"stack: []".encode("utf-8") in process.stdout


def test_pointers_global_arrow(tmp_path):
    source = textwrap.dedent(
        """
        struct node {
          next;
          value;
        }
        
        let x = node { next: null, value: 3.14 };
        let y = node { next: &x, value: false };
        let z = node { next: &y, value: "Hello, world!" };
        
        fn preent_global(x) {
          print x->next->next->value;
        }
        fn main() {
          preent_global(&z);
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

    expected = "3.14"

    assert f"dbg print :: {expected}\n".encode("utf-8") in process.stdout
    assert process.returncode == 0

    # the stack must end up empty
    assert f"stack: []".encode("utf-8") in process.stdout


def test_pointers_global_arrow_set(tmp_path):
    source = textwrap.dedent(
        """
        struct node {
          next;
          value;
        }
        
        let x = node { next: null, value: 3.14 };
        let y = node { next: &x, value: false };
        let z = node { next: &y, value: "Hello, world!" };
        
        fn change_global(x) {
          x->next->next->value = 1.23;
        }
        fn main() {
          change_global(&z);
          print x.value;
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

    expected = "1.23"

    assert f"dbg print :: {expected}\n".encode("utf-8") in process.stdout
    assert process.returncode == 0

    # the stack must end up empty
    assert f"stack: []".encode("utf-8") in process.stdout
