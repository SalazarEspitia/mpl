# The parallel suspect clear against a local collection

**Claim.** There is a third way for a suspect entry to outlive its object, besides #260 and #261, and neither
#260's fix nor keeping suspects' chunks alive during a concurrent collection covers it. When a join has more
than 10,000 suspects to clear, the scheduler clears them in parallel, and a local collection can run between
taking the list and clearing it. The clear then writes into forwarding pointers and freed chunks.

**How it happens** (lines on `cc261-base`, 5336a5cc4).

- `maybeParClearSuspectsAtDepth` (`basis-library/schedulers/spork/Scheduler.sml:988`) takes the list out of
  its heap into `malloc`ed memory (`HH.takeClearSetAtDepth`, `:993`; `ES_takeClearSet`,
  `entanglement-suspects.c:217`).
- The next allocation, `ArrayExtra.alloc numGrains` (`:998`), is a sequence allocation, and those always
  evaluate the local-collection policy.
- A local collection that runs there, at the list's depth, copies the suspects without seeing their entries
  (the list is not in the heap any more) and frees the from-space.
- `clear_suspect_par_safe` (`entanglement-suspects.c:267`) then clears bit 30 (`:286`) of words that are now
  forwarding pointers or lie in freed chunks.

**The program.** `t3.sml` allocates N mutable cells in one task and has its children store a fresh value into
each one, so each cell becomes a suspect of the parent's depth; the join clears them. It then allocates and
checks every cell, and prints `bad 0` when they are intact.

**Run.** `./run.sh` runs seven settings of `t3` on both builds, alternating, 5 runs per row.

**Expected.** The stray writes mostly land where nothing shows, so this finding has two kinds of evidence:

- **Counts.** The weak-cells build checks each entry before using it and counts the ones it drops
  (`@mpl gc-summary`). The table's last row sums, over its runs, the entries the parallel clear found pointing
  into freed chunks or at forwarded objects. Above 0 means the situation arose; the base does a write for each.
  One run of the plain setting at 2 processors drops about 200,000 (Linux and macOS alike).
- **Crashes.** On Linux (16 cores, 32 threads) the base crashed in 2 of 6 runs of the plain settings with 2 or 4
  processors, in the write barrier, and ran right in the others. Weak cells: all right. On macOS crashes are
  rarer (the heap's placement again, see `../heap-placement/`).
