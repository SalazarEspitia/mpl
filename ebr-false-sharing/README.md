# EBR: false sharing in its allocations

**Claim.** The runtime's epoch-based reclamation (EBR) allocates its three blocks with `malloc`, so data that
every processor reads can share a cache line with data one processor writes all the time. Allocating the
blocks 128-byte aligned made an entanglement-heavy program 22 to 40% faster on Linux, and removed swings of
+39% and -15% between builds whose code differs elsewhere.

**How it happens** (lines on upstream `main`, 73b576a61).

- `EBR_new` (`runtime/gc/ebr.c:80`) `malloc`s the shared struct (`:82`; its first field is the epoch, read by
  every processor each time it leaves a quiescent state), then the announcement array (`:85-86`; processor 0's
  slot comes first and is written at every enter and leave), then the per-processor locals (`:87-88`).
- `struct EBR_local` is declared `aligned(128)` (`ebr.h:21`), but `malloc` aligns to 16 bytes, so neighbouring
  locals can share a line too.
- Which blocks end up on one line depends on the allocations made before, so on the build and the number of
  processors. The same code ran 39% slower at 4 processors and 15% faster at 5.

**The change.** One commit (`gc-ebr-align`, fb5828e01): the three blocks come from `posix_memalign` at 128
bytes, sizes rounded up. Nothing else changes.

**Run.** `./run.sh` times `msvspots safe 2 3 0 20000 ewt` (the program of `../stale-suspects/`, which collects
very often and so enters and leaves EBR's quiescent state constantly) at 2, 3 and 4 processors, 5 runs per
build, alternating, and compares medians of wall time, for two pairs of builds that differ only by that
commit:

- **weak cells**: the weak-cells branch one commit before the change (f57e866d8) against its tip (c08f29257).
  The program runs right on both, so every run is timed. This is the pair behind the numbers below.
- **main**: upstream `main` against `gc-ebr-align`. On `main` this program crashes in many runs for other
  reasons (#260), and only right runs are timed, so this pair may have little or nothing to compare.

**Expected.** On a quiet 32-core x86-64 Linux machine, weak-cells pair, medians of 5 runs:

| Processors | 3 | 4 | 5 | 6 | 8 |
|---|---|---|---|---|---|
| `malloc` | 4.02 s | 2.88 s | 2.14 s | 2.30 s | 1.64 s |
| aligned | 3.10 s | 2.09 s | 1.67 s | 1.38 s | 1.26 s |
| aligned is faster by | 23% | 27% | 22% | 40% | 23% |

On macOS arm64, about 14% at 4 and 6 processors.

**On CI this is an indication only.** The runners are shared, have 3 or 4 cores, and their timing is noisy. A
measurement needs a quiet machine with more cores than processors used. The claim is also about one program;
how much ordinary programs gain is not measured here.
