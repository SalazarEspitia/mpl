---
layout: page
title: Known Issues
nav_order: 4
---

# Known Issues
{: .no_toc }

## Table of Contents
{: .no_toc .text-delta }
1. Table of Contents
{:toc}

## Garbage collection before the scheduler starts

Code that runs before MPL's scheduler starts isn't garbage collected yet
([issue 115](https://github.com/MPLLang/mpl/issues/115)). The scheduler starts
when the code of `$(SML_LIB)/basis/fork-join.mlb` runs, so this affects a
program that doesn't use `fork-join.mlb` at all, and code that comes before
`fork-join.mlb` in a program's `.mlb` file. Such a program, if it allocates
much before the scheduler starts, can use far more memory than it needs.

In v0.5.3 and earlier releases, the limit is wider: the garbage collector
doesn't collect the "top level" of the program (outside any call to
`ForkJoin.par`), even after the scheduler starts. For highly parallel programs
this has rarely been a problem, but a mostly sequential program can use far
more memory than it needs.

## The Basis Library

MPL's Basis Library is inherited from (sequential) Standard ML and hasn't been
thoroughly checked for parallel use: some functions may not be safe to call in
parallel ([issue 41](https://github.com/MPLLang/mpl/issues/41)).

## MLton features that aren't supported

MPL doesn't support profiling, nor many of the
[MLton-specific features](http://mlton.org/MLtonStructure.html), including
(but not limited to):
* `share`, `shareAll` and `size`
* `Finalizable`
* `Profile`
* `Signal` (partly supported: `MLton.Signal.setHandler` with
  `Handler.simple` works; the scheduler itself uses `SIGALRM` and `SIGUSR1`)
* `Thread` and `Cont` (partly supported, but not documented)
* `Weak`
* `World`

## Installation

`make install` without `PREFIX=...` can overwrite an existing installation of
MLton ([issue 170](https://github.com/MPLLang/mpl/issues/170)); see
[Getting Started](getting-started.html#by-hand-on-linux).

## Reporting a problem

Please report bugs and other problems on
[GitHub Issues](https://github.com/MPLLang/mpl/issues).
