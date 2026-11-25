---
layout: home
title: Home
nav_order: 1
---

# MaPLe

MaPLe (MPL) is a functional language for provably efficient and safe multicore
parallelism, developed by researchers at Carnegie Mellon University, New York
University, and Rochester Institute of Technology.

- **Standard ML, in parallel.** MPL supports the full Standard ML language,
  extended with task-parallel and data-parallel primitives.
- **Native performance.** Whole-program compilation based on
  [MLton](http://www.mlton.org), with unboxed integers and floats, flattened
  tuples and records, and arrays of unboxed elements, for performance
  competitive with languages such as C/C++, on x86 and Arm.
- **Fine-grained parallelism.** [Automatic parallelism
  management](resources.html#papers) decides at run time when and where to
  create parallel tasks, so code without granularity control still performs
  well. Tuning grain sizes can still help, by a small constant factor.
- **Parallel garbage collection.** Provably efficient parallel collection,
  based on [hierarchical memory management and
  disentanglement](resources.html#papers). MPL scales to hundreds of cores and
  heaps of 1 TB or more.
- **Simple calls into C**, through MLton's
  [foreign-function interface](http://mlton.org/ForeignFunctionInterface.html).

## Using MaPLe

- [Getting Started](getting-started.html): install MPL, then write, compile and
  run a first parallel program.
- [Parallel Programming](parallel.html): the primitives MPL adds to Standard
  ML.
- [Known Issues](known-issues.html): what doesn't work yet.
- [Examples, Libraries and Papers](resources.html): where to go next, and
  where to get help.

## Inside MaPLe

The rest of this site documents the compiler and the run-time system, for
anyone who wants to contribute to MPL or learn how it works: start with
[Common Commands](common-cmds.html) and the [Runtime System](rts/).

{: .note}
This site is still under development. Some pages in the compiler and
run-time system sections are placeholders for now.
