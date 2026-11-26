#ifndef ES_SET_H
#define ES_SET_H

#if (defined(MLTON_GC_INTERNAL_FUNCS))

#define SUSPECT_MASK ((GC_header)0x40000000)
#define SUSPECT_SHIFT 30

/* PROTOTYPE (MPLLang/mpl#260, #261): weak cells.
 *
 * A suspect entry must never be used after its object's chunk has been freed
 * (by a local collection, a concurrent collection, or EBR): the clear at a
 * join writes the object's header word. Instead of making every collector
 * find the entries of the chunks it frees, each chunk that has held a suspect
 * owns a small cell, allocated apart from the chunk, and each entry carries a
 * counted reference to its chunk's cell. The one function every chunk free
 * goes through (HM_freeChunkWithInfo) marks the cell dead; every use of an
 * entry first takes a lease on the cell, and drops the entry if the cell is
 * dead. No chunk is kept for its suspects.
 *
 * The cell is one word, with atomic accesses only:
 *   bit 0        dead: the chunk has been freed (never reset);
 *   bits 1-23    leases held (uses of entries in progress);
 *   bits 24-63   references: one for the chunk while it lives, one per entry.
 * The cell's memory is freed when it has no reference and no lease, so an
 * entry's cell pointer is valid for as long as the entry exists; a new chunk
 * at the same address gets a new cell.
 *
 * Invariants:
 *  1. An entry's cell is not NULL, and its memory is valid for as long as the
 *     entry exists (the entry's reference).
 *  2. While a cell is not dead, HM_getChunkOf(entry.op)->suspectCell is that
 *     cell and the chunk's magic is CHUNK_MAGIC.
 *  3. No code reads or writes through entry.op outside an ES_lease /
 *     ES_release pair.
 *  4. HM_freeChunkWithInfo hands no block to the block allocator while its
 *     cell is alive or leased: the dead bit is set by a CAS that requires zero
 *     leases, on the word the leases are counted in.
 *  5. A cell's references equal its entries, plus one while the chunk lives.
 *
 * Under a lease the chunk is live, so the word in front of entry.op is the
 * object's header or, after a local collection copied the object, its
 * forwarding pointer (a live chunk never reuses an object's address). */
struct ES_cell {
  uint64_t state;
};
typedef struct ES_cell *ES_cell;

#define ES_CELL_DEAD ((uint64_t)1)
#define ES_CELL_LEASE ((uint64_t)1 << 1)
#define ES_CELL_LEASE_MASK ((((uint64_t)1 << 23) - 1) << 1)
#define ES_CELL_REF ((uint64_t)1 << 24)
#define ES_CELL_REFS(state) ((state) >> 24)

/* one per processor (cells are freed by whoever drops the last reference) */
void ES_initCellAllocators(GC_state s);
/* the chunk's cell, allocated and installed if it has none */
ES_cell ES_cellOf(GC_state s, HM_chunk chunk);
/* Take a lease for an entry on its cell. False: the chunk is gone; the
 * entry's reference has been dropped and the entry must be discarded. */
bool ES_lease(GC_state s, ES_cell cell);
/* Give a lease back; with dropRef, the entry's reference too (the entry is
 * being discarded). */
void ES_release(GC_state s, ES_cell cell, bool dropRef);
/* From HM_freeChunkWithInfo, for a chunk with a cell: wait out any lease,
 * mark the cell dead and drop the chunk's reference. */
void ES_chunkDying(GC_state s, HM_chunk chunk);

typedef struct ES_clearArgs {
  HM_chunkList newList;
  uint32_t heapDepth;
  GC_thread thread;
  size_t numMoved;
  size_t numFailed;
  size_t numCleared;
} * ES_clearArgs;


typedef struct ES_clearSet {
  HM_chunk *chunkArray; // array of chunks that need to be processed
  size_t lenChunkArray; // len(chunkArray)
  uint32_t depth;
  size_t numSuspects;
  struct timespec startTime;
} * ES_clearSet;

typedef struct ES_finishedClearSetGrain {
  struct HM_chunkList *output; // output[d]: unsuccessful clears that were moved to depth d
  size_t lenOutput; // len(output array)
} * ES_finishedClearSetGrain;


bool ES_mark(__attribute__((unused)) GC_state s, objptr op);
void ES_unmark(GC_state s, objptr op, ES_clearArgs args);

void ES_add(GC_state s, HM_chunkList es, objptr op);

bool ES_contains(HM_chunkList es, objptr op);

HM_chunkList ES_append(GC_state s, HM_chunkList es1, HM_chunkList es2);

void ES_clear(GC_state s, HM_HierarchicalHeap hh);

// These functions allow us to clear a suspect set in parallel,
// by integrating with the scheduler. The idea is...
size_t ES_numSuspects(GC_state s, HM_HierarchicalHeap hh);
ES_clearSet ES_takeClearSet(GC_state s, HM_HierarchicalHeap hh);
size_t ES_numChunksInClearSet(GC_state s, ES_clearSet es);
ES_finishedClearSetGrain ES_processClearSetGrain(GC_state s, ES_clearSet es, size_t start, size_t stop);
void ES_commitFinishedClearSetGrain(GC_state s, GC_thread thread, ES_finishedClearSetGrain es);
void ES_deleteClearSet(GC_state s, ES_clearSet es);

void ES_move(HM_chunkList list1, HM_chunkList list2);

int ES_foreachSuspect(GC_state s, HM_chunkList storage, struct GC_foreachObjptrClosure * fObjptrClosure);

#endif
#endif
