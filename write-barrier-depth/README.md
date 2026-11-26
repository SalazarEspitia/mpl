# Write barrier: an unpin depth deeper than the writing thread

**Claim.** On upstream `main`, when a task stores a pointer to an older object into a mutable object that a
concurrent task allocated, the write barrier can pin the older object with an unpin depth deeper than the
writing thread itself, and adds a suspect entry at that depth of the writer's heap chain. The debug runtime
stops on an assertion. The release runtime creates a heap deeper than the thread, and the program later
crashes or hangs.

**Status.** Reproduced on macOS arm64 only before this pack, and not yet reviewed by anyone else. The mechanism
up to the assertion is read from the code; what happens after it in the release runtime is inferred, without a
backtrace. This pack's CI is its first run on Linux.

**How it happens** (lines on upstream `main`, 73b576a61).

1. `assign.c:217`: in the barrier's entangled branch (`src` is ordered for the writer, `dst` is not), it calls
   `manage_entangled(s, src, HM_getChunkOf(dstp)->decheckState)`. The "reader" it passes is the label of
   `dst`'s chunk, the task that allocated `dst`, not the writer.
2. `decheck.c:484`: `unpinDepth = lcaHeapDepth(reader, allocator)`, computed from two labels, neither of them
   the writing task's.
3. `decheck.c:286-288`: for two labels with equal paths, `lcaHeapDepth` returns `tree_depth + 1`, the depth of
   that task itself.
4. The entry is added at `HM_HH_getHeapAtDepth(s, thread, unpinDepth)` on the writer's chain.
   `hierarchical-heap.c:572` asserts `depth <= thread->currentDepth`. Without assertions, if there is no heap
   at that depth it makes one and links it in as the chain's head (`:590-592`), deeper than the thread.

(The comments inside the two programs give the lines of an earlier `main`, ddb5b5c08: one less in
`hierarchical-heap.c`.)

**The programs.** Both arrange the same schedule (every `par` is given time to be promoted):

```
root --par--> E (depth 2)
  phase 1:  E --par--> . --par--> A (depth 4)  allocates X; after the joins X is at depth 2
  phase 2:  E --par--> . --par--> T (depth 4)  allocates D and publishes it in a ref of E
                 \--> W (depth 3, stolen)      reads D from the ref, then does  D[0] := X
```

X's and D's labels have the same path, so the depth comes out as 4; W is at depth 3.

- `wb217.sml` stops there. With `-debug-runtime true` it fails the assertion.
- `wb217b.sml` goes on: E builds a list and forks; one side reads the list again and again, the other
  allocates. `wb217b write` does W's store; `wb217b nowrite` is the same program without it.

**Run.** `./run.sh` builds both with upstream `main`, then runs `wb217` (debug runtime) 3 times and `wb217b
nowrite` and `write` (release) 5 times each, at 4 processors, 60 s limit.

**Expected** (macOS arm64).

| | Result |
|---|---|
| `wb217`, debug runtime | every run: `Assertion failed: (depth <= thread->currentDepth)`, `hierarchical-heap.c:572` |
| `wb217b nowrite`, release | right, 5 of 5 |
| `wb217b write`, release | SIGSEGV, 5 of 5 (on another build: some runs hang instead) |

The schedule needs W to be stolen while T runs. A run where that didn't happen prints a timeout and proves
nothing; the script counts those.
