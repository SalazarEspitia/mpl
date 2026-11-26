# Heap placement: why Linux shows the stale entries and macOS mostly doesn't

**Claim.** Whether the stale-entry bug of `../stale-suspects/` crashes a program depends on bit 30 of the
heap's addresses. With the heap where bit 30 is clear, the same build runs right every time; where it is set,
it crashes in almost every run. This is also the way to reproduce the bug on a Mac.

**How it happens.** The stray clear turns bit 30 off in a word it takes for an object header
(`clear_suspect`, `entanglement-suspects.c:67-68` on `cc261-base`; `SUSPECT_MASK` is 0x40000000,
`entanglement-suspects.h:6`). When the word is a pointer:

- if the pointer's bit 30 is already clear, the compare-and-swap writes the value it read: nothing changes;
- if it is set, the pointer moves 1 GiB down, and whoever follows it next crashes.

Linux maps the heap around 0x7fxxxxxxxxxx, randomized, so bit 30 is set in about half of the addresses. On
macOS arm64 the heap of these runs sat around 0x12xxxxxxx, where bit 30 is clear.

**The build.** `heapaddr-hint` (757d675aa) is `cc261-base` plus one measurement-only change in
`runtime/gc/block-allocator.c` (`:243-278`): with `MPL_HEAP_HINT=<hex address>` set, every heap mapping is
asked for at consecutive addresses from there, inside one 1 GiB window, so bit 30 is the same in every heap
address. It prints a line on stderr if the kernel ignores the hint or the window fills up. It is not for
merging.

**Run.** `./run.sh` runs `msvspots safe 2 3 0 20000 ewt` (the program of `../stale-suspects/`) at 4
processors, 15 runs of each, alternating:

- default placement (variable unset);
- `MPL_HEAP_HINT=0x300000000000`: bit 30 clear;
- `MPL_HEAP_HINT=0x300040000000`: bit 30 set.

**Expected** (right runs).

| | Linux x86-64 (32 cores) | macOS arm64 |
|---|---|---|
| default | 24 of 50 | 5 of 5 |
| bit 30 clear | 50 of 50 | 5 of 5 |
| bit 30 set | 4 of 50 | 2 of 5 |

The stale writes happen in all three; only their effect differs. A heap that happens to sit where bit 30 is
clear hides the bug, it doesn't remove it.
