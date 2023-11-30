<p align="center">
  <img src="https://raw.githubusercontent.com/xqb64/venom/master/venom.png" alt="venom"/>
</p>

<h1 align="center">venom</h1>

This project is my debut in the realm of programming language design. I had a blast putting it together, and its sole purpose was fun and education, as opposed to implementing a complete enterprise-grade system. As such, I'm sure it has plenty of drawbacks. Despite this, though, I implemented a bunch of useful features, such as:

- basic data types
  - numbers (double-precision floating point)
  - booleans
  - strings
  - structures
  - pointers
  - null
- operators for the said types
  - `==`, `!=`, `<`, `>`, `<=`, `>=`
  - `+`, `-`, `*`, `/`, `%`
  - `++` (string concatenation)
  - `&`, `*`, `->` (for pointers)
- control flow
  - `if`, `else`
  - `while` (and `break` and `continue`, of course)
- functions
  - `return` is mandatory
  - recursion!

Global scope is allowed.

The system includes what you would expect from a programming language implementation:

  - tokenizer
  - recursive-descent parser
  - bytecode compiler
  - virtual machine
  - disassembler

## Let's talk numbers

### Fibonacci:

```rust
fn fib(n) { 
  if (n < 2) return n;
  return fib(n-1) + fib(n-2);
}

print fib(40);
```

The above program has been the go-to benchmark throughout the development cycle. The running time on my system (AMD Ryzen 3 3200G with Radeon Vega Graphics) for this program is...wait for it:

```
❯ time ./venom benchmarks/fib40.vnm
102334155.00

real	0m18,116s
user	0m18,111s
sys	0m0,004s
```

```
❯ time python3 fib.py
102334155

real	0m28,411s
user	0m28,410s
sys	0m0,001s
```

...which is faster than Python! To be fair, besides being orders of magnitude more useful, Python could also execute this code in a blink of an eye with `@functools.lru_cache()`.

But in any case, this is about where I'd draw the line in terms of functionality. As I continue to improve as a programmer, I might come back to it to make it a little faster (at least as fast as the [VM I wrote in Rust](https://github.com/xqb64/synapse)). 


### Linked list

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

(see Makefile for other options).

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

The test suite relies on venom being compiled with `debug=vm` (because of the prefix in debug prints). To run the test suite, execute the command below, but make sure you have `pytest-xdist` installed because it's a time-consuming process.

```
make test
```

##  Design notes

- The design is the balance among performance and RISC-alikeness. For example, when string concatenation was introduced into the language, there was a choice whether to reuse the current `OP_ADD` opcode or have a separate opcode for string concatenation, e.g. `OP_STRCAT`. At first, I decided to reuse `OP_ADD` (and the `+` operator) at the expense of slightly more complexity in the virtual machine which introduced a performance regression. Later I rewrote the code to use a separate opcode (and the corresponding `++` operator).

- Structures and strings can get arbitrarily large and we do not know their size ahead of time, which required implementing them both underneath as pointers whose size is known. This introduced the whole memory management issue. There were two pathways from here since these pointers need to be freed: either let the venom users explicitly free() their instances, or introduce automatic memory management. I opted for automatic memory management via refcounting because, frankly, I thought I'd have a lot of fun implementing refcounting, but I have to admit that chasing down INCREF/DECREF bugs led to me letting fly a great deal of profanity. ;-)

## Contributing

Contributors to this project are very welcome -- specifically, suggestions (and PRs) as for how to make the whole system even faster, because I suspect there's still more performance left to be squeezed out.

## See also

[synapse](https://github.com/xqb64/synapse) - My second attempt, written in Rust

## Licensing

Licensed under the [MIT License](https://opensource.org/licenses/MIT). For details, see [LICENSE](https://github.com/xqb64/venom/blob/master/LICENSE).