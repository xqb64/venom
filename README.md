# venom

A handcrafted virtual stack machine capable of executing a reduced instruction set consisting of only 27 microinstructions. The programs for the VM are written in a minimal, dynamically-typed, Turing-complete programming language featuring basic data types, functions, structures, and flow control. Besides the VM, the system includes an on-demand tokenizer, a recursive-descent parser, and a bytecode compiler.

Status: usable, but needs thorough testing (coming soon).

## Examples

Fibonacci:

```rust
fn fib(n) { 
    if (n == 0) return 0;
    if (n == 1) return 1;
    return fib(n-1) + fib(n-2);
}

print fib(40);
```

- **NOTE**: The above program has been the go-to benchmark throughout the development cycle. In the early versions of Venom, the running time on my system (AMD Ryzen 3 3200G with Radeon Vega Graphics) used to go as high as 9 minutes (admittedly, with debug prints enabled). The latest Venom version runs `fib(40)` in...wait for it:

    ```
    ❯ time ./venom benchmarks/fib40.vnm
    { 102334155.00 }

    real	0m28,325s
    user	0m28,242s
    sys	0m0,008s
    ```

    ```
    ❯ time python3 fib.py
    102334155

    real	0m28,893s
    user	0m28,666s
    sys	0m0,052s
    ```

    ...which is as fast as Python! To be fair, Python could execute this code in a blink of an eye with `@functools.lru_cache()`.

## Compiling

Clone the repository and run:

```
make
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
