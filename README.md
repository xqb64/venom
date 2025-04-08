<h1 align="center">venom</h1>

This project is my debut in the realm of programming language design. I had a blast putting it together, and its sole purpose was fun and education, as opposed to implementing a complete enterprise-grade system. As such, I'm sure it has plenty of drawbacks. Despite this, though, I implemented a bunch of useful features, such as:

- basic data types
  - numbers (double-precision floating point)
  - booleans
  - strings
  - structures
  - arrays
  - pointers
  - closures
  - generators
  - null
- operators for the said types
  - `==`, `!=`, `<`, `>`, `<=`, `>=`
  - `+`, `-`, `*`, `/`, `%`
  - `+=`, `-=`, `*=`, `/=`, `%=` (compound assignment)
  - `&`, `|`, `^`, `~`, `<<`, `>>` (bitwise and/or/xor/not/shift (left|right))
  - `&=`, `|=`, `^=`, `<<=`, `>>=` (bitwise compound assignment)
  - `&&`, `||`, `!` (logical and/or/not)
  - `++` (string concatenation)
  - `&`, `*`, `->` (for pointers)
  - `.` (member access)
  - `,` (comma)
- control flow
  - `if`, `else`
  - `while`
  - `for` (C-style)
  - `break` and `continue`
  - `yield`
- a few useful builtins:
  - `next()` (for generator resumption)
  - `len()` (a la Python)
  - `hasattr()`
  - `getattr()`
  - `setattr()`
- functions
  - first-class citizens
  - methods
  - `return` is mandatory
  - recursion!
- `print` statement
- global scope
- import subsystem that caches imports and detects and prevents import cycles
- reference counting
- optional NaN boxing

The system includes what you would expect from a programming language implementation:

  - a lexer based on finite state machines
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
❯ hyperfine --runs 5 './venom benchmarks/fib40.vnm'
Benchmark 1: ./venom benchmarks/fib40.vnm
  Time (mean ± σ):     12.662 s ±  0.121 s    [User: 12.606 s, System: 0.008 s]
  Range (min … max):   12.574 s … 12.874 s    5 runs
```

```
❯ hyperfine --runs 5 'python3 fib.py'
Benchmark 1: python3 fib.py
  Time (mean ± σ):     27.132 s ±  0.152 s    [User: 27.057 s, System: 0.038 s]
  Range (min … max):   26.964 s … 27.335 s    5 runs
```

...which is faster than Python! To be fair, besides being orders of magnitude more useful, Python could also execute this code in a blink of an eye with `@functools.lru_cache()`.

But in any case, this is about where I'd draw the line in terms of functionality. As I continue to improve as a programmer, I might come back to it to make it a little faster. 

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
make graph.png <file>
```

...where `<file>` could, e.g., be: `benchmarks/fib40.vnm`.

### Compiling with NaN boxing enabled

```
make -j$(nproc) opt=nan_boxing
```

However, note that I've found this to not improve the performance, at all.

## Tests

The tests are written in Python and venom's behavior is tested externally.

The test suite relies on venom being compiled with `debug=vm` (because of the prefix in debug prints). To run the test suite, create a Python virtual environment and activate it, install `pytest` (ideally also install `pytest-xdist` because it's a time-consuming process), then execute the command below:

```
make test
```

##  Design notes

- The design is the balance among performance and RISC-alikeness. For example, when string concatenation was introduced into the language, there was a choice whether to reuse the current `OP_ADD` opcode or have a separate opcode for string concatenation, e.g. `OP_STRCAT`. At first, I decided to reuse `OP_ADD` (and the `+` operator) at the expense of slightly more complexity in the virtual machine which introduced a performance regression. Later I rewrote the code to use a separate opcode (and the corresponding `++` operator).

- Structures and strings can get arbitrarily large and we do not know their size ahead of time, which required implementing them both underneath as pointers whose size is known. This introduced the whole memory management issue. There were two pathways from here since these pointers need to be freed: either let the venom users explicitly free() their instances, or introduce automatic memory management. I opted for automatic memory management via refcounting because, frankly, I thought I'd have a lot of fun implementing refcounting, but I have to admit that chasing down INCREF/DECREF bugs led to me letting fly a great deal of profanity. ;-)

## Contributing

Contributors to this project are very welcome -- specifically, suggestions (and PRs) as for how to make the whole system even faster, because I suspect there's still more performance left to be squeezed out.

Before submitting a PR, make sure:
- C code is formatted properly (use `make format`)
- Python code (tests) pass the ruff check (use `make ruff`)
- all tests pass

There is also a pre-commit hook in the `hooks` folder that you could copy/paste into your `.git/hooks` folder which helps with this process.

## See also

- [synapse](https://github.com/xqb64/synapse) - My second attempt, written in Rust
- [viper](https://github.com/xqb64/viper) - My third attempt, a tree-walk interpreter for a dynamic language, written in Python
- [ucc](https://github.com/xqb64/ucc) - My fourth attempt, an optimizing compiler for a large subset of the C programming language, written in Rust
## Licensing

Licensed under the [MIT License](https://opensource.org/licenses/MIT). For details, see [LICENSE](https://github.com/xqb64/venom/blob/master/LICENSE).
