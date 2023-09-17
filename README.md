# venom

A handcrafted virtual stack machine capable of executing a reduced instruction set consisting of only 34 microinstructions. The programs for the VM are written in a minimal, dynamically-typed, Turing-complete programming language featuring basic data types, functions, pointers, structures, and flow control. Besides the VM, the system includes an on-demand tokenizer, a recursive-descent parser, and a bytecode compiler.

## Examples

Fibonacci:

```rust
fn fib(n) { 
  if (n < 2) return n;
  return fib(n-1) + fib(n-2);
}

print fib(40);
```

- **NOTE**: The above program has been the go-to benchmark throughout the development cycle. In the early versions of Venom, the running time on my system (AMD Ryzen 3 3200G with Radeon Vega Graphics) used to go as high as 9 minutes (admittedly, with debug prints enabled). The latest Venom version runs `fib(40)` in...wait for it:

    ```
    ❯ time ./venom benchmarks/fib40.vnm
    102334155.00

    real	0m19,768s
    user	0m19,761s
    sys	0m0,004s
    ```

    ```
    ❯ time python3 fib.py
    102334155

    real	0m24,585s
    user	0m24,550s
    sys	0m0,035s
    ```

    ...which is faster than Python! To be fair, Python could execute this code in a blink of an eye with `@functools.lru_cache()`.

Linked list:
```rust
struct node {
  next;
  value;
}

fn list_print(list) {
  let current = list;
  while (*current != null) {
    print current->value;
    current = &current->next;
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
  list_print(&list);
}

main();
```

## Compiling

Clone the repository and run:

```
make -j$(nproc)
```

To enable the debug prints for one of the components of the system, run:

```
make debug=vm
```

To enable the debug prints for all system components, run:

```
make debug=all
```

To generate the performance graph, run:

```
make graph.png
```

## Tests

The tests are written in Python and venom's behavior is tested externally.

The test suite relies on venom being compiled with `debug=vm` (because of the prefix in debug prints). To run the tests, execute the command below, but make sure you have `pytest-xdist` installed because it's a time-consuming process.

```
make test
```

# Design notes

- The design of the entire system is the balance between performance and RISC-alikeness. For example, when string concatenation was introduced into the language, I decided to reuse the current `OP_ADD` opcode (as opposed to a separate opcode for string concatenation, e.g. `OP_STRCAT`) at the expense of performance and slightly more complexity in the virtual machine, so that the instruction count remains as low as possible.
- Structures and strings can get arbitrarily large and we do not know their size ahead of time, which required implementing them both underneath as pointers whose size is known. This introduced the whole memory management issue. There were two pathways from here since these pointers need to be freed: either let the venom users explicitly free() their instances, or introduce automatic memory management. I opted for automatic memory management via refcounting because, frankly, I thought I'd have a lot of fun implementing refcounting, but I have to admit that chasing down INCREF/DECREF bugs led to me letting fly a great deal of profanity. ;-)

