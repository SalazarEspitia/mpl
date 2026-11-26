/* ========================================================================
 * PROTOTYPE: weak cells (see entanglement-suspects.h)
 * ======================================================================== */

static struct FixedSizeAllocator *ES_cellAllocators = NULL;

void ES_initCellAllocators(GC_state s) {
  assert(NULL == ES_cellAllocators);
  ES_cellAllocators = malloc(s->numberOfProcs * sizeof(struct FixedSizeAllocator));
  if (NULL == ES_cellAllocators)
    DIE("Out of memory. Unable to allocate the suspect cell allocators.");
  for (uint32_t i = 0; i < s->numberOfProcs; i++) {
    initFixedSizeAllocator(&(ES_cellAllocators[i]), sizeof(struct ES_cell), BLOCK_FOR_SUSPECTS);
  }
}

static inline FixedSizeAllocator ES_cellAllocator(GC_state s) {
  assert(NULL != ES_cellAllocators);
  assert(s->procNumber >= 0 && (uint32_t)s->procNumber < s->numberOfProcs);
  return &(ES_cellAllocators[s->procNumber]);
}

static inline void ES_freeCell(GC_state s, ES_cell cell) {
  /* no reference and no lease: nobody can reach the cell any more */
  assert(0 == (__atomic_load_n(&(cell->state), __ATOMIC_RELAXED) & ~ES_CELL_DEAD));
  s->cumulativeStatistics->numSuspectCellsFreed++;
  freeFixedSize(ES_cellAllocator(s), cell);
}

ES_cell ES_cellOf(GC_state s, HM_chunk chunk) {
  assert(CHUNK_MAGIC == chunk->magic);
  ES_cell cell = __atomic_load_n(&(chunk->suspectCell), __ATOMIC_ACQUIRE);
  if (NULL != cell)
    return cell;

  ES_cell fresh = (ES_cell)allocateFixedSize(ES_cellAllocator(s));
  __atomic_store_n(&(fresh->state), ES_CELL_REF, __ATOMIC_RELAXED); /* the chunk's reference */
  s->cumulativeStatistics->numSuspectCellsAllocated++;
  if (__atomic_compare_exchange_n(&(chunk->suspectCell), &cell, fresh, FALSE,
                                  __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
    return fresh;

  /* another processor installed one first; ours was never published */
  __atomic_store_n(&(fresh->state), 0, __ATOMIC_RELAXED);
  ES_freeCell(s, fresh);
  assert(NULL != cell);
  return cell;
}

bool ES_newEntry(GC_state s, objptr op, struct ES_entry *entry) {
  ES_cell cell = ES_cellOf(s, HM_getChunkOf(objptrToPointer(op, NULL)));
  uint64_t old = __atomic_fetch_add(&(cell->state), ES_CELL_REF, __ATOMIC_SEQ_CST);
  if (old & ES_CELL_DEAD) {
    /* NOT a safety net. An add is safe only under the assumption stated at
     * ES_newEntry's declaration: the caller reaches op, so no collector is
     * freeing op's chunk. If that were false (a collector bug, the same one
     * that would make mark_suspect's header write unsafe), this branch would
     * catch one interleaving of three: the kill landing between ES_cellOf's
     * load and the fetch_add above, with other entries outstanding. The
     * other two pass in silence: the free reading suspectCell == NULL just
     * before ES_cellOf installs a cell (the cell sits on a freed block and is
     * never marked dead), and the cell freed and recycled between the load
     * and the fetch_add (the reference lands on another chunk's cell). */
    assert(FALSE && "a suspect was added to a chunk that is being freed");
    uint64_t now = __atomic_sub_fetch(&(cell->state), ES_CELL_REF, __ATOMIC_SEQ_CST);
    if (0 == (now & ~ES_CELL_DEAD))
      ES_freeCell(s, cell);
    return FALSE;
  }
  entry->op = op;
  entry->cell = cell;
  return TRUE;
}

bool ES_lease(GC_state s, __attribute__((unused)) objptr op, ES_cell cell) {
  assert(NULL != cell); /* invariant 1 */
  uint64_t old = __atomic_fetch_add(&(cell->state), ES_CELL_LEASE, __ATOMIC_SEQ_CST);
  assert(ES_CELL_REFS(old) >= 1); /* the entry's own reference */
  assert((old & ES_CELL_LEASE_MASK) != ES_CELL_LEASE_MASK);
  if (old & ES_CELL_DEAD) {
    /* the chunk is gone: give back the lease and the entry's reference */
    uint64_t now = __atomic_sub_fetch(&(cell->state), ES_CELL_LEASE + ES_CELL_REF, __ATOMIC_SEQ_CST);
    if (0 == (now & ~ES_CELL_DEAD))
      ES_freeCell(s, cell);
    return FALSE;
  }
#if ASSERT
  /* invariant 2: under the lease, op's chunk is live and this is its cell */
  HM_chunk chunk = (HM_chunk)blockOf(objptrToPointer(op, NULL));
  assert(CHUNK_MAGIC == chunk->magic);
  assert(cell == __atomic_load_n(&(chunk->suspectCell), __ATOMIC_ACQUIRE));
#endif
  return TRUE;
}

#if ASSERT
void ES_assertListInvariants(HM_chunkList list) {
  if (NULL == list)
    return;
  for (HM_chunk chunk = HM_getChunkListFirstChunk(list); chunk != NULL; chunk = chunk->nextChunk) {
    pointer frontier = HM_getChunkFrontier(chunk);
    assert(0 == ((size_t)(frontier - HM_getChunkStart(chunk))) % sizeof(struct ES_entry));
    for (pointer p = HM_getChunkStart(chunk); p < frontier; p += sizeof(struct ES_entry)) {
      struct ES_entry *entry = (struct ES_entry *)p;
      assert(isObjptr(entry->op));
      assert(NULL != entry->cell);
      /* a lease that keeps the entry's reference when the cell is dead */
      uint64_t old = __atomic_fetch_add(&(entry->cell->state), ES_CELL_LEASE, __ATOMIC_SEQ_CST);
      assert(ES_CELL_REFS(old) >= 1);
      if (!(old & ES_CELL_DEAD)) {
        HM_chunk objChunk = (HM_chunk)blockOf(objptrToPointer(entry->op, NULL));
        assert(CHUNK_MAGIC == objChunk->magic);
        assert(entry->cell == __atomic_load_n(&(objChunk->suspectCell), __ATOMIC_ACQUIRE));
        /* inside the chunk. Not "below the frontier": the chunk's frontier
         * field is brought up to date only when its thread enters the
         * runtime, and an entangled reader can hold an entry for an object
         * another thread has allocated in its current chunk since then. */
        assert(objptrToPointer(entry->op, NULL) >= HM_getChunkStart(objChunk));
        assert(objptrToPointer(entry->op, NULL) < HM_getChunkLimit(objChunk));
        /* the word is the object's header, with the suspect bit, or a
         * forwarding pointer */
        GC_header h = getHeader(objptrToPointer(entry->op, NULL));
        assert(!(h & GC_VALID_HEADER_MASK) || (h & SUSPECT_MASK));
      }
      __atomic_fetch_sub(&(entry->cell->state), ES_CELL_LEASE, __ATOMIC_SEQ_CST);
    }
  }
}
#endif

void ES_release(__attribute__((unused)) GC_state s, ES_cell cell, bool dropRef) {
  LOCAL_USED_FOR_ASSERT uint64_t now =
    __atomic_sub_fetch(&(cell->state), ES_CELL_LEASE + (dropRef ? ES_CELL_REF : 0), __ATOMIC_SEQ_CST);
  /* the chunk cannot die under a lease (invariant 4), so it still holds its
   * reference and the cell cannot be freed here */
  assert(!(now & ES_CELL_DEAD));
  assert(ES_CELL_REFS(now) >= 1);
}

void ES_chunkDying(GC_state s, HM_chunk chunk) {
  ES_cell cell = __atomic_load_n(&(chunk->suspectCell), __ATOMIC_ACQUIRE);
  if (NULL == cell)
    return;

  size_t tries = 0;
  uint64_t state = __atomic_load_n(&(cell->state), __ATOMIC_ACQUIRE);
  while (TRUE) {
    assert(!(state & ES_CELL_DEAD));   /* a chunk dies once */
    assert(ES_CELL_REFS(state) >= 1);  /* the chunk's reference */
    if (state & ES_CELL_LEASE_MASK) {
      /* an entry of this chunk is in use (a header read and one CAS): wait */
      s->cumulativeStatistics->numSuspectCellLeaseWaits++;
      tries++;
      if (tries > 100) sched_yield();
      state = __atomic_load_n(&(cell->state), __ATOMIC_ACQUIRE);
      continue;
    }
    uint64_t dead = (state | ES_CELL_DEAD) - ES_CELL_REF;
    if (__atomic_compare_exchange_n(&(cell->state), &state, dead, FALSE,
                                    __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE)) {
      s->cumulativeStatistics->numChunksFreedWithSuspectCell++;
      if (ES_CELL_REFS(dead) > 0) {
        /* entries still point into this chunk: each one is dropped at its
         * next use */
        s->cumulativeStatistics->numChunksFreedWithSuspectEntries++;
        s->cumulativeStatistics->numSuspectEntriesOutlivedChunk += ES_CELL_REFS(dead);
      } else {
        ES_freeCell(s, cell);
      }
      break;
    }
  }
  __atomic_store_n(&(chunk->suspectCell), NULL, __ATOMIC_RELEASE);
}

/* ======================================================================== */

static inline bool suspicious_header(GC_header h) {
  return (1 == ((h & SUSPECT_MASK) >> SUSPECT_SHIFT));
}

static inline bool mark_suspect(objptr op)
{
  pointer p = objptrToPointer(op, NULL);
  GC_header header = __sync_fetch_and_or(getHeaderp(p), SUSPECT_MASK);
  assert(1 == (header & GC_VALID_HEADER_MASK));
  // while (TRUE)
  // {
  //   GC_header header = getHeader(p);
  //   GC_header newHeader = header | SUSPECT_MASK;
  //   if (header == newHeader)
  //   {
  //     /*
  //        just return because the suspect bit is already set
  //      */
  //     return false;
  //   }
  //   else
  //   {
  //     /*
  //        otherwise, install the new header with the bit set. this might fail
  //        if a concurrent thread changes the header first.
  //      */
  //     if (__sync_bool_compare_and_swap(getHeaderp(p), header, newHeader))
  //       return true;
  //   }
  // }
  // DIE("should be impossible to reach here");
  /*return true if this call marked the header, false if someone else did*/
  return !suspicious_header(header);
}

static inline bool is_suspect(objptr op)
{
  pointer p = objptrToPointer(op, NULL);
  GC_header h = getHeader(p);
  /* have to check first that the header is valid
   * (otherwise, there could be a forward pointer in this spot)
   */
  return (1 == (h & GC_VALID_HEADER_MASK)) && suspicious_header(h);
}


void clear_suspect(
    GC_state s,
    objptr *opp,
    objptr op,
    void *rawArgs)
{
  /* opp points at the op field of an entry in the list being cleared */
  struct ES_entry entry = *(struct ES_entry *)opp;
  assert(entry.op == op);
  pointer p = objptrToPointer(op, NULL);
  ES_clearArgs eargs = (ES_clearArgs) rawArgs;

  if (!ES_lease(s, op, entry.cell)) {
    /* the object's chunk has been freed: nothing to clear */
    s->cumulativeStatistics->numSuspectsDroppedDeadClear++;
    eargs->numDropped++;
    return;
  }

  /* Under the lease the chunk is live: the word is the object's header, or
   * its forwarding pointer if a local collection copied the object without
   * seeing this entry. */
  GC_header header = getHeader(p);
  if (!(header & GC_VALID_HEADER_MASK)) {
    ES_release(s, entry.cell, TRUE);
    s->cumulativeStatistics->numSuspectsDroppedForwardedClear++;
    eargs->numDropped++;
    return;
  }
  uint32_t unpinDepth = (header & UNPIN_DEPTH_MASK) >> UNPIN_DEPTH_SHIFT;

  if (pinType(header) == PIN_ANY && unpinDepth < eargs->heapDepth) {
    /* Not ready to be cleared */
    ES_release(s, entry.cell, FALSE);
    HM_HierarchicalHeap unpinHeap = HM_HH_getHeapAtDepth(s, eargs->thread, unpinDepth);
    HM_storeInChunkListWithPurpose(HM_HH_getSuspects(unpinHeap), &entry, sizeof(struct ES_entry), BLOCK_FOR_SUSPECTS);
    eargs->numMoved++;
    return;
  }

  GC_header newHeader = header & ~(SUSPECT_MASK);
  if (__sync_bool_compare_and_swap(getHeaderp(p), header, newHeader)) {
    /* clearing successful */
    ES_release(s, entry.cell, TRUE);
    eargs->numCleared++;
    return;
  }
  else {
    /*oops something changed in b/w, let's try at the next join*/
    ES_release(s, entry.cell, FALSE);
    HM_storeInChunkListWithPurpose(eargs->newList, &entry, sizeof(struct ES_entry), BLOCK_FOR_SUSPECTS);
    eargs->numFailed++;

    LOG(LM_HIERARCHICAL_HEAP, LL_INFO,
        "WARNING: failed suspect clear!");
  }
}

bool ES_contains(__attribute__((unused)) HM_chunkList es, objptr op)
{
  return is_suspect(op);
}

bool ES_mark(__attribute__((unused)) GC_state s, objptr op) {
  return mark_suspect(op);
}

void ES_add(GC_state s, HM_chunkList es, objptr op)
{

  if (!mark_suspect(op))
  {
    /* if op is already in es, skip*/
    return;
  }
  s->cumulativeStatistics->numSuspectsMarked++;
  struct ES_entry entry;
  if (!ES_newEntry(s, op, &entry))
    return;
  HM_storeInChunkListWithPurpose(es, &entry, sizeof(struct ES_entry), BLOCK_FOR_SUSPECTS);
}

int ES_foreachSuspect(
    GC_state s,
    HM_chunkList storage,
    GC_foreachObjptrClosure fObjptrClosure)
{
  if (storage == NULL)
    return 0;
  int count = 0;
  HM_chunk chunk = HM_getChunkListFirstChunk(storage);
  while (chunk != NULL)
  {
    pointer p = HM_getChunkStart(chunk);
    pointer frontier = HM_getChunkFrontier(chunk);
    while (p < frontier)
    {
      /* p is an entry; the callback gets a pointer to its op field (the
       * entry's first field) and finds the cell from there */
      assert(isObjptr(((struct ES_entry *)p)->op));
      callIfIsObjptr(s, fObjptrClosure, &(((struct ES_entry *)p)->op));
      p += sizeof(struct ES_entry);
    }
    count += ((int)(frontier - HM_getChunkStart(chunk)))/(sizeof(struct ES_entry));
    chunk = chunk->nextChunk;
  }
  return count;
}

HM_chunkList ES_append(
  __attribute__((unused)) GC_state s,
  HM_chunkList es1,
  HM_chunkList es2)
{
  assert(es1 != NULL);
  HM_appendChunkList(es1 ,es2);
  return es1;
}

void ES_move(HM_chunkList list1, HM_chunkList list2) {
  HM_appendChunkList(list1, list2);
  HM_initChunkList(list2);
}

static size_t SUSPECTS_THRESHOLD = 10000;


void ES_clear(GC_state s, HM_HierarchicalHeap hh)
{
  struct timespec startTime;
  struct timespec stopTime;

  HM_chunkList es = HM_HH_getSuspects(hh);
  uint32_t heapDepth = HM_HH_getDepth(hh);
  struct HM_chunkList oldList = *(es);
  HM_initChunkList(HM_HH_getSuspects(hh));

  size_t numSuspects = HM_getChunkListUsedSize(&oldList) / sizeof(struct ES_entry);
  if (numSuspects >= SUSPECTS_THRESHOLD) {
    timespec_now(&startTime);
  }

  struct ES_clearArgs eargs = {
    .newList = HM_HH_getSuspects(hh),
    .heapDepth = heapDepth,
    .thread = getThreadCurrent(s),
    .numMoved = 0,
    .numCleared = 0,
    .numFailed = 0,
    .numDropped = 0
  };
  
  struct GC_foreachObjptrClosure fObjptrClosure =
      {.fun = clear_suspect, .env = &(eargs)};
#if ASSERT
  int ns = ES_foreachSuspect(s, &oldList, &fObjptrClosure);
  assert(numSuspects == (size_t)ns);
#else
  ES_foreachSuspect(s, &oldList, &fObjptrClosure);
#endif
  s->cumulativeStatistics->numSuspectsCleared += numSuspects;

  HM_freeChunksInListWithInfo(s, &(oldList), NULL, BLOCK_FOR_SUSPECTS);

  if (eargs.numFailed > 0) {
    LOG(LM_HIERARCHICAL_HEAP, LL_INFO,
      "WARNING: %zu failed suspect clear(s)",
      eargs.numFailed);
  }

  if (numSuspects >= SUSPECTS_THRESHOLD) {
    timespec_now(&stopTime);
    timespec_sub(&stopTime, &startTime);
    LOG(LM_HIERARCHICAL_HEAP, LL_FORCE,
      "time to process %zu suspects (%zu cleared, %zu moved, %zu dropped) at depth %u: %ld.%09ld",
      numSuspects,
      eargs.numCleared,
      eargs.numMoved,
      eargs.numDropped,
      HM_HH_getDepth(hh),
      (long)stopTime.tv_sec,
      stopTime.tv_nsec
    );
  }
}


size_t ES_numSuspects(
  __attribute__((unused)) GC_state s,
  HM_HierarchicalHeap hh)
{
  return HM_getChunkListUsedSize(HM_HH_getSuspects(hh)) / sizeof(struct ES_entry);
}


ES_clearSet ES_takeClearSet(
  __attribute__((unused)) GC_state s,
  HM_HierarchicalHeap hh)
{
  struct timespec startTime;
  timespec_now(&startTime);

  size_t numSuspects = ES_numSuspects(s, hh);

  ES_clearSet result = malloc(sizeof(struct ES_clearSet));
  HM_chunkList es = HM_HH_getSuspects(hh);
  struct HM_chunkList oldList = *es;
  HM_initChunkList(es);

  size_t numChunks = 0;
  for (HM_chunk cursor = HM_getChunkListFirstChunk(&oldList);
       cursor != NULL;
       cursor = cursor->nextChunk)
  {
    numChunks++;
  }

  HM_chunk *chunkArray = malloc(numChunks * sizeof(HM_chunk));
  result->chunkArray = chunkArray;
  result->lenChunkArray = numChunks;
  result->depth = HM_HH_getDepth(hh);
  result->numSuspects = numSuspects;
  result->startTime = startTime;

  size_t i = 0;
  for (HM_chunk cursor = HM_getChunkListFirstChunk(&oldList);
       cursor != NULL;
       cursor = cursor->nextChunk)
  {
    chunkArray[i] = cursor;
    i++;
  }

  return result;
}


size_t ES_numChunksInClearSet(
  __attribute__((unused)) GC_state s,
  ES_clearSet es)
{
  return es->lenChunkArray;
}


void clear_suspect_par_safe(
  GC_state s,
  struct ES_entry *entryp,
  struct HM_chunkList *output,
  size_t lenOutput)
{
  struct ES_entry entry = *entryp;
  pointer p = objptrToPointer(entry.op, NULL);

  if (!ES_lease(s, entry.op, entry.cell)) {
    /* the object's chunk has been freed: nothing to clear */
    s->cumulativeStatistics->numSuspectsDroppedDeadParClear++;
    return;
  }

  while (TRUE) {
    GC_header header = getHeader(p);
    if (!(header & GC_VALID_HEADER_MASK)) {
      /* A forwarding pointer: a local collection copied the object while
       * this entry was in the clear set, out of the collection's sight. The
       * copy keeps its suspect bit and has no entry (reads of it stay on the
       * slow path). */
      ES_release(s, entry.cell, TRUE);
      s->cumulativeStatistics->numSuspectsDroppedForwardedParClear++;
      return;
    }
    uint32_t unpinDepth = (header & UNPIN_DEPTH_MASK) >> UNPIN_DEPTH_SHIFT;

    // Note: lenOutput == depth of heap whose suspects we are clearing
    if (pinType(header) == PIN_ANY && unpinDepth < lenOutput) {
      /* Not ready to be cleared; move it instead */
      ES_release(s, entry.cell, FALSE);
      HM_storeInChunkListWithPurpose(&(output[unpinDepth]), &entry, sizeof(struct ES_entry), BLOCK_FOR_SUSPECTS);
      return;
    }

    GC_header newHeader = header & ~(SUSPECT_MASK);
    if (__sync_bool_compare_and_swap(getHeaderp(p), header, newHeader)) {
      /* clearing successful */
      ES_release(s, entry.cell, TRUE);
      return;
    }
    else {
      // oops, something changed in between
      // Is this possible?
      //   - Seems like it could be, if there is a CGC happening
      //   simultaneously at the same level (and it's marking/unmarking objects)?
      //   - If this is the only possibility, then we should be able to just
      //   do the CAS with newHeader in a loop?
      LOG(LM_HIERARCHICAL_HEAP, LL_INFO,
        "WARNING: failed suspect clear; trying again");
    }
  }
}


ES_finishedClearSetGrain ES_processClearSetGrain(
  GC_state s,
  ES_clearSet es,
  size_t start,
  size_t stop)
{
  ES_finishedClearSetGrain result = malloc(sizeof(struct ES_finishedClearSetGrain));
  struct HM_chunkList *output = malloc(es->depth * sizeof(struct HM_chunkList));
  result->output = output;
  result->lenOutput = es->depth;
  
  // initialize output
  for (uint32_t i = 0; i < es->depth; i++) {
    HM_initChunkList(&(output[i]));
  }

  // process each input chunk
  for (size_t i = start; i < stop; i++) {
    HM_chunk chunk = es->chunkArray[i];
    pointer p = HM_getChunkStart(chunk);
    pointer frontier = HM_getChunkFrontier(chunk);
    while (p < frontier)
    {
      struct ES_entry *entryp = (struct ES_entry *)p;
      assert(isObjptr(entryp->op));
      if (isObjptr(entryp->op)) {
        clear_suspect_par_safe(s, entryp, output, es->depth);
      }
      p += sizeof(struct ES_entry);
    }
  }

  return result;
}


void ES_commitFinishedClearSetGrain(
  GC_state s,
  GC_thread thread,
  ES_finishedClearSetGrain es)
{
  for (size_t i = 0; i < es->lenOutput; i++) {
    HM_chunkList list = &(es->output[i]);
    if (HM_getChunkListSize(list) == 0)
      continue;

    HM_HierarchicalHeap dest = HM_HH_getHeapAtDepth(s, thread, i);
    HM_appendChunkList(HM_HH_getSuspects(dest), list);
    HM_initChunkList(list);
  }

  free(es->output);
  free(es);
}


void ES_deleteClearSet(GC_state s, ES_clearSet es) {
  s->cumulativeStatistics->numSuspectsCleared += es->numSuspects;

  for (size_t i = 0; i < es->lenChunkArray; i++) {
    HM_freeChunkWithInfo(s, es->chunkArray[i], NULL, BLOCK_FOR_SUSPECTS);
  }

  size_t numSuspects = es->numSuspects;
  uint32_t depth = es->depth;
  struct timespec startTime = es->startTime;
  free(es->chunkArray);
  free(es);

  struct timespec stopTime;
  timespec_now(&stopTime);
  timespec_sub(&stopTime, &startTime);
  LOG(LM_HIERARCHICAL_HEAP, LL_INFO,
    "time to process %zu suspects at depth %u: %ld.%09ld",
    numSuspects,
    depth,
    (long)stopTime.tv_sec,
    stopTime.tv_nsec
  );

}
