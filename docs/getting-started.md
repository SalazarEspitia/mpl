---
layout: page
title: Getting Started
nav_order: 2
---

# Getting Started
{: .no_toc }

{: .note}
These pages describe MPL as it is on the `main` branch. The latest release,
v0.5.3 (January 2025), and the Docker image are older: they don't have
`ForkJoin.parform` and `ForkJoin.reducem`, and they collect less memory (see
[Known Issues](known-issues.html)). To get the current version, install a
recent commit of `main` with [`mpl-switch`](#with-mpl-switch).

## Table of Contents
{: .no_toc .text-delta }
1. Table of Contents
{:toc}

## Try it with Docker

The quickest way to try MPL is its Docker image, which has the compiler and
the [examples](https://github.com/MPLLang/mpl/tree/main/examples) already
built:
```
$ docker pull shwestrick/mpl
$ docker run -it shwestrick/mpl /bin/bash
...# examples/bin/primes @mpl procs 4 --
```

To compile your own code, mount a local directory inside the container. For
example, to compile and run a `main.mlb` in the current directory:
```
$ docker run -it -v $(pwd -P):/root/mycode shwestrick/mpl /bin/bash
...# cd /root/mycode
...# mpl main.mlb
...# ./main @mpl procs 4 --
```

{: .note}
The image has MPL v0.5.3, the latest release (see the note at the top), and is
built for x86-64 (amd64) only. On an Arm machine, such as a Mac with Apple
Silicon, Docker runs it under emulation, which is slower; installing MPL
natively (below) is the better choice there.

## Install natively

MPL runs natively on Linux (x86-64) and macOS (x86-64 and Apple Silicon). Its
releases don't include binaries, so you build it from source, which needs
[GCC](http://gcc.gnu.org), [GMP](http://gmplib.org), GNU Make and Bash,
binutils, the usual Unix tools, and an existing Standard ML compiler:
[MLton](http://mlton.org) (recommended, with `mllex` and `mlyacc`) or
[SML/NJ](http://www.smlnj.org).

### With `mpl-switch`

[`mpl-switch`](https://github.com/mpllang/mpl-switch) builds and installs MPL
versions side by side and lets you switch between them. It needs Python 3 and
`git`.
```bash
$ git clone https://github.com/mpllang/mpl-switch
# ... add ./mpl-switch/ to your PATH ...

$ mpl-switch init
$ mpl-switch install v0.5.3
$ mpl-switch select v0.5.3

# MPL is now installed at ~/.mpl/bin/mpl
# final step: add ~/.mpl/bin/ to your PATH
```
You can name any tag or commit hash of the MPL repository. For the current
version, with everything these pages describe, name a recent commit of
[`main`](https://github.com/MPLLang/mpl/commits/main):
```bash
$ mpl-switch install <commit>
$ mpl-switch select <commit>
```
`mpl-switch list` shows what's installed; `~/.mpl/` can be deleted at any
time, since it can be rebuilt.

### By hand, on Linux

Clone [the repository](https://github.com/MPLLang/mpl), then build. This
produces `build/bin/mpl`:
```
$ make
```
To install it somewhere, give a `PREFIX`:
```
$ make PREFIX=/opt/mpl install
```

{: .warning}
Don't run `make install` without `PREFIX=...`: it can overwrite an existing
installation of MLton
([issue 170](https://github.com/MPLLang/mpl/issues/170)).

### By hand, on macOS

Install GNU Make, GMP and MLton with [Homebrew](https://brew.sh/), clone [the
repository](https://github.com/MPLLang/mpl), then build with GNU Make
(`gmake`), telling it where GMP is. This produces `build/bin/mpl`:
```
$ brew install mlton make gmp
$ gmake WITH_GMP_DIR=$(brew --prefix gmp)
```
To install it somewhere, give a `PREFIX` here too:
```
$ gmake WITH_GMP_DIR=$(brew --prefix gmp) PREFIX=/opt/mpl install
```

## A first parallel program

Save this as `fib.sml`. It computes Fibonacci numbers by the doubly recursive
formula, running the two recursive calls in parallel with `ForkJoin.par`:
```sml
(* Optional: below this size, fib runs sequentially. MPL manages par
 * automatically, but a sequential cutoff still makes it faster. *)
fun sfib n = if n < 2 then n else sfib (n-1) + sfib (n-2)

fun fib n =
  if n < 20 then sfib n
  else
    let
      val (a, b) = ForkJoin.par (fn () => fib (n-1), fn () => fib (n-2))
    in
      a + b
    end

val n =
  case CommandLine.arguments () of
    [s] => valOf (Int.fromString s)
  | _ => 35

val t0 = Time.now ()
val r = fib n
val t1 = Time.now ()
val () = print ("fib " ^ Int.toString n ^ " = " ^ Int.toString r ^ "\n")
val () = print ("time " ^ Time.fmt 3 (Time.- (t1, t0)) ^ "s\n")
```

MPL compiles whole programs described by an
[ML Basis](http://mlton.org/MLBasis.html) (`.mlb`) file. Save this as
`fib.mlb`:
```
$(SML_LIB)/basis/basis.mlb
$(SML_LIB)/basis/fork-join.mlb
$(SML_LIB)/basis/mlton.mlb

fib.sml
```
The three libraries are the [Standard ML Basis
Library](https://smlfamily.github.io/Basis/), the `ForkJoin` structure, and
the `MLton` structure (which includes MPL's `MLton.Parallel`); see
[Parallel Programming](parallel.html).

MPL's default `int` is 32 bits, as in MLton, so `fib` overflows above 46; the
`-default-type int64` option below makes it 64 bits.

Compile it, then run it on 4 processors:
```
$ mpl fib.mlb
$ ./fib @mpl procs 4 -- 38
fib 38 = 39088169
time ...s
```

## Compiling

```
$ mpl [compile-time options...] foo.mlb
```
produces an executable named `foo`. Many of
[MLton's compile-time options](http://mlton.org/CompileTimeOptions.html) also
work with MPL; some useful ones:
* `-output NAME`: name the executable.
* `-default-type int64 -default-type word64`: use 64-bit integers and words
  by default.
* `-debug true -debug-runtime true -keep g`: for debugging; keeps the
  generated C files and links the debug run-time system (with assertions
  enabled), so the program can be examined with tools like `gdb`.

For example:
```
$ mpl -default-type int64 -output foo sources.mlb
```

## Running

An MPL program takes options for the run-time system between `@mpl` and `--`,
before its own arguments:
```
$ <program> [@mpl [run-time options...] --] [program args...]
```
These options aren't visible to the program through
[`CommandLine.arguments`](https://smlfamily.github.io/Basis/command-line.html).
Some useful ones:
* `procs N`: run on `N` worker threads.
* `set-affinity`: pin the worker threads to processors; with
  `affinity-base B` and `affinity-stride S`, thread `i` goes on processor
  `B + S*i`. Linux only: on macOS it is accepted and does nothing.
* `block-size X`: the heap's block size in bytes, with suffixes `K`, `M` and
  `G` (e.g. `64K`). It must be a power of two and a multiple of the system's
  page size; the default is one page.

For example, this runs `foo` on 4 pinned processors, with one argument `bar`:
```
$ foo @mpl procs 4 set-affinity -- bar
```
