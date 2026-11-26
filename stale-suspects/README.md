# Stale suspect entries (issues #260 and #261)

**Claim.** MPL's entanglement code keeps a list of "suspect" objects per heap, apart from the chunks the
objects live in. Collections can free a suspect's chunk and leave its entry in the list. A later clear then
writes into memory that is free or already reused, and the program crashes somewhere else, or, rarely, prints
a wrong result. The weak-cells prototype (`cc-suspects-weakcells`) removes the crashes.

**How it happens** (lines on `cc261-base`, 5336a5cc4).

- An object becomes a suspect when bit 30 of its header is set (`SUSPECT_MASK`, `entanglement-suspects.h:6`)
  and its address is stored in the list of a heap (`ES_add`, called at `assign.c:210` and `decheck.c:450`,
  `:517`).
- A concurrent collection frees every chunk of its scope that holds no marked object
  (`concurrent-collection.c:1170-1171`). Nothing in `concurrent-collection.c` reads a suspect list, so an
  unmarked suspect goes with its chunk and its entry stays. This is #261.
- At the next join, `clear_suspect` (`entanglement-suspects.c:47`) reads the word before the entry's address
  as a header and clears bit 30 of it with a compare-and-swap (`:67-68`). If the chunk has been reused, that
  word is someone else's data. A pointer there with bit 30 set now points exactly 1 GiB lower.
- The crash comes later and elsewhere: a concurrent collection following the changed pointer
  (`CC_tryUnpinOrKeepPinned`, `concurrent-collection.c:758`), its marking, the program's own code, or
  `clear_suspect` itself when the old chunk is unmapped.
- #260 is the same on another path: a local collection frees the chunk of a pinned suspect it never reached,
  and `copySuspect` or `clear_suspect` reads it afterwards.

**Why the base is `cc261-base` and not `main`.** On `main` this program crashes for several reasons at once.
`cc261-base` is upstream `main` (ddb5b5c08) plus the proposed fix for #260, two fixes for the public
remembered set, and a write-barrier fix. With those in, what still crashes is the stale entries of #261 alone,
so the comparison with weak cells changes one thing.

**The program.** `msvspots.sml` is a stress test: tasks share tables of mutable cells, write fresh cells into
their parent's table, read what their sibling wrote, and check every cell. It prints `bad 0` when all its data
is intact.

**Run.** `./run.sh` runs `msvspots safe 2 3 0 20000 ewt` on both builds, alternating, 10 runs per row, at 2
and 4 processors and once more with settings that make collections very frequent.

**Expected.**

| | Linux x86-64 | macOS arm64 |
|---|---|---|
| base | crashes in most runs: 32 of 80 right over eight settings on a 32-core machine | mostly right at the default heap placement; `../heap-placement/` shows why and how to make it crash |
| weak cells | 80 of 80 right | all right |

The base's crashes are spread over the places named above; `out/results.md` lists each one's signal and frames.
