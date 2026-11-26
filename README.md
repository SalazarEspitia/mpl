# MPL runtime findings: evidence you can run

This branch holds nothing but small test programs, scripts, and a GitHub Actions workflow. Each folder is one
finding about MPL's runtime: a claim, the program that shows it, and one command that runs it and prints a
table. The workflow runs the same commands on Linux x86-64, Linux on Arm and macOS, so the results are there to
read without building anything: open the latest run of the "Linux evidence" workflow under Actions and look at
its summary.

Everything is built from commits of this fork (`SalazarEspitia/mpl`), named in `common/refs.sh`. Nothing here
changes MPL.

## The findings

| Folder | What it shows | Run | Expected on Linux x86-64 | Expected on macOS arm64 | Time |
|---|---|---|---|---|---|
| [`stale-suspects/`](stale-suspects/) | #260, #261: suspect entries outlive the chunks they point into, and a later clear writes into freed or reused memory. Base against the weak-cells prototype | `stale-suspects/run.sh` | base crashes in most runs (32 of 80 right); weak cells 80 of 80 | base mostly right (see `heap-placement/`); weak cells all right | 5-10 min |
| [`parallel-clear/`](parallel-clear/) | The same kind of stale entry on a third path: the scheduler's parallel clear, when a local collection runs while the clear set is out of its heap | `parallel-clear/run.sh` | base mostly right, some crashes; weak cells all right and count the stale entries they dropped (above 0) | the same, crashes rarer | 3-5 min |
| [`heap-placement/`](heap-placement/) | Why Linux shows the stale entries and macOS mostly doesn't: bit 30 of the heap's addresses. One build, heap placed by an environment variable | `heap-placement/run.sh` | bit 30 clear: 50 of 50 right; set: 4 of 50; default: 24 of 50 | clear 5 of 5, default 5 of 5, **set 2 of 5**: the way to see the bug on a Mac | 3-8 min |
| [`ebr-false-sharing/`](ebr-false-sharing/) | EBR's three blocks come from `malloc` and can share cache lines; allocating them 128-byte aligned makes this program faster | `ebr-false-sharing/run.sh` | weak-cells pair, quiet 32-core machine: aligned 23% faster at 3 processors, 27% at 4, 22-40% at 5 to 8 | about 14% faster | 3-6 min |
| [`write-barrier-depth/`](write-barrier-depth/) | On upstream `main`: the write barrier's entangled branch can pin an object deeper than the writing thread | `write-barrier-depth/run.sh` | not run before this pack: the CI's Linux columns are the first | debug runtime: assertion in 3 of 3; release: 5 of 5 crash with the write, 5 of 5 right without | 2-6 min |

"Expected" is what my own runs gave: Linux on a 32-core x86-64 machine (WSL2), macOS on a 15-core Apple
Silicon machine. The times are for the scripts' default run counts with the compilers already built; a build
adds about 5 minutes each. Each folder's README has the claim, how it happens with file and line, and the
expected results.

The first three findings are one bug seen from three sides, and `stale-suspects/README.md` is the place to
start. The last two are separate.

## How the CI works

`.github/workflows/linux-evidence.yml` runs on every push to this branch:

1. **build**: one job per build and platform. `common/build.sh` fetches the commit and builds it the way
   upstream's `ci.yml` does on Linux (`apt-get install mlton libgmp-dev gcc make`, then `make --silent`), and the
   way the README says on macOS (`brew install mlton make gmp`, then `gmake WITH_GMP_DIR=$(brew --prefix gmp)`;
   upstream's CI has no Mac job). Builds are cached by commit and platform, so a later push only reruns the tests.
2. **finding**: one job per finding and platform. It restores the builds it needs, runs the folder's `run.sh`,
   writes the table to the job's summary, and uploads `results.md`, the per-run table and every run's output as
   an artifact.
3. **results by platform**: collects every platform's results and writes one table per finding with a column
   per platform (`common/combine.sh`), in the run's summary and as the artifact `results-combined`.

The platforms:

- `ubuntu-latest`: Linux x86-64, 4 cores. The main evidence.
- `ubuntu-24.04-arm`: Linux on Arm, 4 cores. MPL's README doesn't name this platform and I hadn't tested it, so
  its jobs are allowed to fail (the build may not work there). If it runs, it shows Linux's heap placement
  together with Arm's weaker memory ordering.
- `macos-latest`: macOS arm64, 3 cores, so the scripts use 2 and 3 processors there instead of 2 and 4.

Windows is left out: MPL supports Linux and macOS only, and on Windows it runs in WSL2, which is Linux.

A green job means the script ran to the end, not that nothing crashed: crashes are the evidence. Read the
tables. The runners are shared machines with few cores, so counts will differ from the "expected" column, and
the timing in `ebr-false-sharing/` is only an indication there.

## Running it yourself

You need what MPL needs (MLton, GMP, gcc, GNU make; `brew install mlton make gmp` on a Mac), `git`, `perl` and
bash.

```
git clone -b linux-evidence --single-branch https://github.com/SalazarEspitia/mpl mpl-evidence
cd mpl-evidence
stale-suspects/run.sh
```

Each `run.sh` builds the compilers it needs first (about 5 minutes each, kept in `.builds/` and reused), runs
the programs one at a time, prints a table, and leaves `out/results.md` and every run's output in the folder's
`out/`. To put several platforms' results side by side, copy their `out/` folders under one folder and run
`common/combine.sh THAT_FOLDER`.

To use compilers you already have instead of building:

- findings with one build take `MPL=/path/to/bin/mpl` (`heap-placement/`: a build of `heapaddr-hint`;
  `write-barrier-depth/`: a build of upstream `main`);
- findings with two take one variable per build: `MPL_BASE` and `MPL_WEAK` (`stale-suspects/`,
  `parallel-clear/`); `MPL_MAIN`, `MPL_EBR`, `MPL_WEAK0` and `MPL_WEAK` (`ebr-false-sharing/`).

Other settings: `RUNS` (runs per row), `PROCS` (processor counts), `EV_TLIMIT` (seconds before a run counts as
hung, 120), `JOBS` (`make -j` for the builds). The header of each script lists its own.

A run is **right** when it exits with 0 and prints the expected line (each program checks its own data),
**wrong** when it exits with 0 without it, **crashed** when it dies by a signal or exits with another code
(the table gives the signal and the innermost frames of the faulting thread, from `common/segvbt.c`, or the
failed assertion), and **hung** when it is still running at the time limit.

## The builds

| Name | Branch on this fork | Commit | What it is |
|---|---|---|---|
| `main` | `main` | 73b576a61 | upstream `main` |
| `base` | `cc261-base` | 5336a5cc4 | upstream `main` at ddb5b5c08 plus four fixes for other crashes of the same test program (see `stale-suspects/README.md`) |
| `weak` | `cc-suspects-weakcells` | c08f29257 | `base` plus the weak-cells prototype, plus the EBR alignment change |
| `weak0` | the parent of `weak` | f57e866d8 | `weak` without the EBR alignment change |
| `hint` | `heapaddr-hint` | 757d675aa | `base` plus a measurement-only `MPL_HEAP_HINT` |
| `ebr` | `gc-ebr-align` | fb5828e01 | upstream `main` plus the EBR alignment change |

I used AI coding assistants (large language models) to investigate this, write the scripts, and draft this description.
