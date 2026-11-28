---
layout: page
title: Parallel Programming
nav_order: 3
---

# Parallel Programming
{: .no_toc }

MPL extends Standard ML with primitives for parallelism and concurrency, in two
structures: `ForkJoin` (from `$(SML_LIB)/basis/fork-join.mlb`) and
`MLton.Parallel` (from `$(SML_LIB)/basis/mlton.mlb`). The
[examples](https://github.com/MPLLang/mpl/tree/main/examples) show them in
use, and [`mpllib`](https://github.com/MPLLang/mpllib) builds parallel
sequences, sorting, graphs and more on top of them.

{: .note}
These pages describe MPL as it is on the `main` branch. The latest release,
v0.5.3 (January 2025), and the Docker image are older: they don't have
`ForkJoin.parform` and `ForkJoin.reducem`, and they collect less memory (see
[Known Issues](known-issues.html)). To get the current version, install a
recent commit of `main` with [`mpl-switch`](getting-started.html#with-mpl-switch).

## Table of Contents
{: .no_toc .text-delta }
1. Table of Contents
{:toc}

## The `ForkJoin` structure

```sml
val par: (unit -> 'a) * (unit -> 'b) -> 'a * 'b
val parform: (int * int) -> (int -> unit) -> unit
val reducem: ('a * 'a -> 'a) -> 'a -> (int * int) -> (int -> 'a) -> 'a
val parfor: int -> (int * int) -> (int -> unit) -> unit
val alloc: int -> 'a array
```

### `par`

`par (f, g)` evaluates `f ()` and `g ()`, possibly in parallel, and returns
both results. MPL decides at run time whether to create a parallel task
(automatic parallelism management), so `par` is cheap enough for fine-grained
code:
```sml
val (x, y) = ForkJoin.par (fn () => 1 + 2, fn () => 3 * 4)   (* (3, 12) *)
```
[Getting Started](getting-started.html#a-first-parallel-program) has a
complete program built on `par`.

### `parform`

*Not in v0.5.3.*
`parform (i, j) f` is a parallel for loop: it runs `f k` for each
`i <= k < j`, in parallel. MPL decides how to split the range (automatic
parallelism management), so there is no grain size to choose. For example,
squaring every element of an array:
```sml
val n = 10000
val a = Array.tabulate (n, fn i => i)
val () = ForkJoin.parform (0, n) (fn i => Array.update (a, i, Array.sub (a, i) * Array.sub (a, i)))
```

### `reducem`

*Not in v0.5.3.*
`reducem c z (i, j) f` computes the "sum" of `f i, ..., f (j-1)` with respect
to the function `c`, with "zero" element `z`, in parallel, automatically
managed like `parform`. `c` must be associative and `z` an identity for it
(`c (z, x) = x`), since MPL may split the range anywhere and start each piece
from `z`. For example, the sum of an array `b`:
```sml
val sum = ForkJoin.reducem op+ 0 (0, Array.length b) (fn i => Array.sub (b, i))
```
or the maximum of a function `f: real -> real` sampled at `m` evenly spaced
points of `[0.0, 1.0)`:
```sml
val mx = ForkJoin.reducem Real.max Real.negInf (0, m) (fn i => f (Real.fromInt i / Real.fromInt m))
```

### `parfor`

`parfor g (i, j) f` is a parallel for loop with a grain size `g`, for manual
granularity control: it splits the range into about `(j-i)/g` pieces of at
most `g` iterations each, and runs each piece sequentially. `g` must be at
least 1; with `g = 1`, every iteration can run in parallel.
```sml
val () = ForkJoin.parfor 1000 (0, n) (fn i => Array.update (b, i, 2 * i))
```
Choosing a good grain size is often cumbersome; **prefer `parform`**, unless
you know the grain size you want.

### `alloc`

`alloc n` returns a fresh array of length `n` whose elements are **not
initialized**. It is meant for writing efficient libraries: it allocates in
parallel, works with the scheduler and the memory manager, and is safe for
garbage collection.
```sml
val c: int array = ForkJoin.alloc n
val () = ForkJoin.parform (0, n) (fn i => Array.update (c, i, i + 1))
```

{: .warning}
Initialize every element of an array from `alloc` before reading it.

## The `MLton.Parallel` structure

```sml
val compareAndSwap: 'a ref -> ('a * 'a) -> 'a
val arrayCompareAndSwap: ('a array * int) -> ('a * 'a) -> 'a

val fetchAndAdd: int ref -> int -> int
val arrayFetchAndAdd: int array * int -> int -> int
```

`compareAndSwap r (x, y)` performs an atomic
[compare-and-swap](https://en.wikipedia.org/wiki/Compare-and-swap): if `r`
contains `x`, it is set to `y`. Either way, it returns what `r` contained
before.
```sml
val r = ref 5
val old = MLton.Parallel.compareAndSwap r (5, 7)   (* old = 5, and now !r = 7 *)
```
Equality here works like [`MLton.eq`](http://mlton.org/MLtonStructure.html):
an ordinary equality check for simple types (`char`, `int`, `word`, etc.), and
pointer equality for other types (`array`, `string`, tuples, datatypes, etc.).
The semantics are a bit murky.

`arrayCompareAndSwap (a, i) (x, y)` does the same at index `i` of the array
`a`, and reads or writes no other element of `a`.

`fetchAndAdd r d` performs an atomic
[fetch-and-add](https://en.wikipedia.org/wiki/Fetch-and-add): it adds `d` to
the contents of `r` and returns what `r` contained before.
```sml
val counter = ref 0
val () = ForkJoin.parform (0, n) (fn _ => ignore (MLton.Parallel.fetchAndAdd counter 1))
(* now !counter = n *)
```

`arrayFetchAndAdd (a, i) d` does the same at index `i` of the array `a`, and
reads or writes no other element of `a`.
