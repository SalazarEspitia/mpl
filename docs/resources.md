---
layout: page
title: Examples, Libraries and Papers
nav_order: 5
---

# Examples, Libraries and Papers
{: .no_toc }

## Table of Contents
{: .no_toc .text-delta }
1. Table of Contents
{:toc}

## Learning MPL

- **[The MPL tutorial](https://github.com/MPLLang/mpl-tutorial)**: a
  step-by-step introduction to parallel programming in MPL.
- **[Examples](https://github.com/MPLLang/mpl/tree/main/examples)** in the MPL
  repository: Fibonacci, N-queens, primes, mergesort, matrix multiplication,
  ray tracing, seam carving and more, each with the command to build and run
  it (`examples/README.md`).
- **[The Parallel ML benchmark suite](https://github.com/MPLLang/parallel-ml-bench)**:
  sophisticated parallel algorithms and applications in MPL, with
  performance comparisons against C++, Go, Java and multicore OCaml.

## Libraries and projects

MPL supports the full Standard ML language, so existing SML libraries can be
used. We recommend the [smlpkg](https://github.com/diku-dk/smlpkg) package
manager.

Libraries and projects that use MPL for parallelism:
- [`mpllib`](https://github.com/MPLLang/mpllib): data structures (sequences,
  sets, dictionaries, graphs, matrices, meshes, images, etc.) and parallel
  algorithms (map, reduce, scan, filter, sorting, search, tokenization, graph
  processing, computational geometry, etc.), plus utilities such as
  command-line parsing and benchmarking.
- [`sml-audio`](https://github.com/shwestrick/sml-audio): audio processing and
  I/O for `.wav` files.
- [`sml-matrix-market`](https://github.com/shwestrick/sml-matrix-market):
  parsing [MatrixMarket](https://math.nist.gov/MatrixMarket/formats.html)
  files.
- [`mpl-seam-carve`](https://github.com/shwestrick/mpl-seam-carve): image
  resizing by [seam carving](https://en.wikipedia.org/wiki/Seam_carving).
- [`mpl-wc`](https://github.com/shwestrick/mpl-wc): a simple parallel clone of
  the Unix `wc` utility.
- [`mpl-1brc`](https://github.com/shwestrick/mpl-1brc): a solution to the
  [One Billion Row Challenge](https://www.morling.dev/blog/one-billion-row-challenge/).
- [`mpl-tinykaboom`](https://github.com/shwestrick/mpl-tinykaboom): a port of
  a [port (Futhark)](https://github.com/athas/tinykaboom) of the
  [KABOOM (C++)](https://github.com/ssloy/tinykaboom) tiny graphics program.
- [`grafeyn`](https://github.com/UmutAcarLab/grafeyn): a hybrid
  Feynman-Schrödinger quantum simulator.

## Releases

Each [release](https://github.com/MPLLang/mpl/releases) comes with notes on
what changed. Development continues on the `main` branch between releases;
[`mpl-switch`](getting-started.html#with-mpl-switch) can install any tag or
commit.

## Getting help

Ask questions and report problems on
[GitHub Issues](https://github.com/MPLLang/mpl/issues), or start a thread in
[GitHub Discussions](https://github.com/MPLLang/mpl/discussions).

## Papers

The ideas behind MPL:

1. [Hierarchical Memory Management for Parallel Programs](http://cs.iit.edu/~smuller/papers/icfp16-preprint.pdf).
   Ram Raghunathan, Stefan K. Muller, Umut A. Acar, and Guy Blelloch.
   ICFP 2016.
2. [Hierarchical Memory Management for Mutable State](http://www.cs.cmu.edu/~swestric/18/ppopp.pdf).
   Adrien Guatto, Sam Westrick, Ram Raghunathan, Umut Acar, and Matthew Fluet.
   PPoPP 2018.
3. [Disentanglement in Nested-Parallel Programs](http://www.cs.cmu.edu/~swestric/20/popl-disentangled.pdf).
   Sam Westrick, Rohan Yadav, Matthew Fluet, and Umut A. Acar.
   POPL 2020.
4. [Provably Space-Efficient Parallel Functional Programming](http://www.cs.cmu.edu/~swestric/21/popl.pdf).
   Jatin Arora, Sam Westrick, and Umut A. Acar.
   POPL 2021.
5. [Entanglement Detection with Near-Zero Cost](http://www.cs.cmu.edu/~swestric/22/icfp-detect.pdf).
   Sam Westrick, Jatin Arora, and Umut A. Acar.
   ICFP 2022.
6. [Efficient Parallel Functional Programming with Effects](https://www.cs.cmu.edu/~swestric/23/epfpe.pdf).
   Jatin Arora, Sam Westrick, and Umut A. Acar.
   PLDI 2023.
7. [Automatic Parallelism Management](https://www.cs.cmu.edu/~swestric/24/popl24-par-manage.pdf).
   Sam Westrick, Matthew Fluet, Mike Rainey, and Umut A. Acar.
   POPL 2024.
