/* Copyright (C) 1999-2007 Henry Cejtin, Matthew Fluet, Suresh
 *    Jagannathan, and Stephen Weeks.
 * Copyright (C) 1997-2000 NEC Research Institute.
 *
 * MLton is released under a HPND-style license.
 * See the file MLton-LICENSE for details.
 */

void switchToThread(GC_state s, objptr op) {
  GC_thread thread = (GC_thread)(objptrToPointer(op, NULL) + offsetofThread(s));

  assert(thread->hierarchicalHeap != NULL);

  size_t terminateCheckCounter = 0;
  int otherProcNum = atomicLoadS32(&(thread->currentProcNum));
  while (otherProcNum >= 0) {
    /* Spin while someone else is currently executing this thread. The
     * termination checks happen rarely, and reset terminateCheckCounter to 0
     * when they do. */
    GC_MayTerminateThreadRarely(s, &terminateCheckCounter);
    if (terminateCheckCounter == 0) sched_yield();
    // Sanity check: don't get deadlocked by self
    assert(otherProcNum != s->procNumber);
    otherProcNum = atomicLoadS32(&(thread->currentProcNum));
  }
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  atomicStoreS32(&(thread->currentProcNum), s->procNumber);

  if (DEBUG_THREADS) {
    // GC_thread thread;
    GC_stack stack;

    // thread = (GC_thread)(objptrToPointer (op, NULL)
    //                      + offsetofThread (s));
    stack = (GC_stack)(objptrToPointer (thread->stack, NULL));

    fprintf (stderr, "switchToThread ("FMTOBJPTR")  used = %"PRIuMAX
             "  reserved = %"PRIuMAX"\n",
             op, (uintmax_t)stack->used, (uintmax_t)stack->reserved);
  }

  s->currentThread = op;
  setGCStateCurrentThreadAndStack (s);
}

void GC_switchToThread (GC_state s, pointer p, size_t ensureBytesFree) {
  enter(s);
  objptr currop = getThreadCurrentObjptr(s);
  // GC_thread currThread = threadObjptrToStruct(s, currop);
  LOG(LM_THREAD, LL_DEBUG,
    "GC_switchToThread current = %p, p = %p, ensureBytesFree = %zu",
    (void*)objptrToPointer(currop, NULL),
    (void*)p,
    ensureBytesFree);

  // printf("[%d] GC_switchToThread\n  from "FMTPTR"\n  to   "FMTPTR"\n",
  //   s->procNumber,
  //   (uintptr_t)(void*)objptrToPointer(currop, NULL),
  //   (uintptr_t)(void*)p);

  GC_thread oldCurrentThread = getThreadCurrent(s);

  assert(HM_HH_getDepth(oldCurrentThread->hierarchicalHeap) <= oldCurrentThread->currentDepth);

  //ENTER1 (s, p);
  /* SPOONHOWER_NOTE: copied from enter() */
  /* used needs to be set because the mutator has changed s->stackTop. */
  // getStackCurrent(s)->used = sizeofGCStateCurrentStackUsed(s);
  // oldCurrentThread->exnStack = s->exnStack;
  // beginAtomic(s);

  // assert(threadAndHeapOkay(s));
  // HM_HH_updateValues(s, oldCurrentThread, s->frontier);
  s->frontier = 0;
  s->limitPlusSlop = 0;
  s->limit = 0;

  oldCurrentThread->bytesNeeded = ensureBytesFree;

  s->currentThread = BOGUS_OBJPTR;
  /* SAM_NOTE: This write synchronizes with the spinloop in switchToThread (above) */
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  atomicStoreS32(&(oldCurrentThread->currentProcNum), -1);

  // printf("[%d] switchToThread\n  from %p\n    to %p\n",
  //   s->procNumber,
  //   (void*)oldCurrentThread,
  //   (void*)p);

  switchToThread(s, pointerToObjptr(p, NULL));
  /* SAM_NOTE: TODO: do signal handlers work properly? */
  s->atomicState--;
  switchToSignalHandlerThreadIfNonAtomicAndSignalPending(s);

  assert(threadAndHeapOkay(s));
  GC_thread thread = getThreadCurrent(s);
  s->frontier = HM_HH_getFrontier(thread);
  s->limitPlusSlop = HM_HH_getLimit(thread);
  s->limit = s->limitPlusSlop - GC_HEAP_LIMIT_SLOP;

  /* A thread that resumes at depth 1 or deeper while its current chunk is
   * in a depth-0 heap gets a fresh chunk at its own depth. This happens
   * after the scheduler boots: it moves the original thread to depth 1
   * (setDepth changes only the thread's depth), and the thread would
   * otherwise keep allocating in its depth-0 chunk until something moves it,
   * such as that chunk filling up, a large array, or a promoted par.
   * The depth-0 heap is never collected, and anything stored into an object
   * there is pinned until exit, so every value ever stored into the
   * program's first top-level refs and arrays would be retained.
   *
   * Doing this when the thread resumes, rather than in setDepth, leaves
   * what it allocated before switching away where it is. At boot, that is
   * everything up to the creation of the scheduler thread, whose stack (a
   * copy of the original thread's) refers to those objects.
   *
   * Only depth 0: a thread also resumes in a chunk at a shallower depth of 1
   * or more (a new task thread starts in a heap at its parent's depth), and
   * those heaps are collected. */
  bool leaveDepth0Chunk =
    thread->currentDepth > 0 &&
    HM_HH_getDepth(HM_getLevelHead(thread->currentChunk)) == 0;
  if (leaveDepth0Chunk) {
    LOG(LM_THREAD, LL_INFO,
      "thread %p resumes at depth %"PRIu32" in a depth-0 chunk: moving it to a fresh chunk",
      (void*)thread,
      thread->currentDepth);
  }

  HM_ensureHierarchicalHeapAssurances(s, FALSE, thread->bytesNeeded, leaveDepth0Chunk);

  LOG(LM_THREAD, LL_DEBUG,
    "GC_switchToThread succeeded: old current = %p, new = %p",
    (void*)objptrToPointer(currop, NULL),
    (void*)objptrToPointer(getThreadCurrentObjptr(s), NULL));

  leave(s);
  // endAtomic (s);
  // assert(invariantForMutatorStack(s));
  //LEAVE0 (s);
}
