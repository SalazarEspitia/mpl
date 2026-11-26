/* Copyright (C) 1999-2007 Henry Cejtin, Matthew Fluet, Suresh
 *    Jagannathan, and Stephen Weeks.
 * Copyright (C) 1997-2000 NEC Research Institute.
 *
 * MLton is released under a HPND-style license.
 * See the file MLton-LICENSE for details.
 */

/* Move the heap that `from` runs in to `to`: the heap, the chunk to
 * allocate in, the depth, the allocation counters and the entanglement
 * checker's state. `from` is left with none of them. Before the scheduler's
 * boot the program has one heap, which follows the running thread: the
 * thread switches hand it over (GC_switchToThread, and the switch to a
 * signal handler). A signal handler runs in the interrupted thread's heap
 * the same way (GC_handlerEnterHeapOfThread and ...Leave...). */
void handOverHeap(GC_thread from, GC_thread to) {
  assert(from != to);
  to->currentDepth = from->currentDepth;
  to->currentChunk = from->currentChunk;
  to->hierarchicalHeap = from->hierarchicalHeap;
  to->bytesAllocatedSinceLastCollection =
    from->bytesAllocatedSinceLastCollection;
  to->bytesSurvivedLastCollection = from->bytesSurvivedLastCollection;
#ifdef DETECT_ENTANGLEMENT
  to->decheckState = from->decheckState;
  memcpy(&(to->decheckSyncDepths[0]),
         &(from->decheckSyncDepths[0]),
         DECHECK_DEPTHS_LEN * sizeof(uint32_t));
  from->decheckState = DECHECK_BOGUS_TID;
#endif

  from->currentChunk = NULL;
  from->hierarchicalHeap = NULL;
  from->bytesAllocatedSinceLastCollection = 0;
  from->bytesSurvivedLastCollection = 0;
}

/* Before the scheduler's boot: hand the one heap from the thread that
 * stops running to the thread that runs next, which has none. */
void handOverHeapBeforeBoot(
  ARG_USED_FOR_ASSERT GC_state s,
  GC_thread from,
  GC_thread to)
{
  assert(HM_HH_beforeBoot(s));
  assert(NULL != from->hierarchicalHeap);
  assert(0 == HM_HH_getDepth(from->hierarchicalHeap));
  assert(0 == from->currentDepth);
  /* every store before the boot is within the one heap */
  assert(HM_HH_remembersNothing(from->hierarchicalHeap));
  if (from == to)
    return;
  assert(NULL == to->hierarchicalHeap);
  handOverHeap(from, to);
}

/* A thread made before the scheduler's boot has no heap of its own: until
 * the boot the program has one heap, which the running thread holds (see
 * copyThreadWithHeap and GC_switchToThread). The first time such a thread
 * runs after the boot, give it what newThreadWithHeap would have given it: a
 * fresh depth-0 heap, with its stack in a chunk of its own. The stack is
 * copied into it: left in the heap of before the boot, it would grow in a
 * chunk list that the other threads made before the boot share, now on
 * several processors. The thread object stays where it is, in a depth-0
 * heap that is never collected again. The caller has claimed the thread
 * (currentProcNum), so no other processor touches it. */
static void giveHeapToThreadMadeBeforeBoot(GC_state s, GC_thread thread) {
  assert(!HM_HH_beforeBoot(s));
  assert(NULL == thread->currentChunk);

  /* Only a thread made before the boot, by newThreadInCurrentHeap (depth
   * 0), may come here; newThread leaves HM_HH_INVALID_DEPTH, so this also
   * catches a switch to a heapless thread made by newThread (a prototype of
   * copyCurrent, or GC_HH_forkThread's), which needs a heap from its maker. */
  assert(0 == thread->currentDepth);

  HM_HierarchicalHeap hh = HM_HH_new(s, 0);
  thread->hierarchicalHeap = hh;
  thread->bytesAllocatedSinceLastCollection = 0;
  thread->bytesSurvivedLastCollection = 0;

  GC_stack from = (GC_stack)objptrToPointer(thread->stack, NULL);
  GC_stack to = newStackInOwnChunk(s, hh, from->reserved, DECHECK_BOGUS_TID);
  copyStack(s, from, to);
  thread->stack = pointerToObjptr((pointer)to, NULL);

  if (!HM_HH_extend(s, thread, GC_HEAP_LIMIT_SLOP)) {
    DIE("Ran out of space for Hierarchical Heap!");
  }
  assert(thread->currentChunk->mightContainMultipleObjects);
}

void switchToThread(GC_state s, objptr op) {
  GC_thread thread = (GC_thread)(objptrToPointer(op, NULL) + offsetofThread(s));

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

  if (NULL == thread->hierarchicalHeap)
    giveHeapToThreadMadeBeforeBoot(s, thread);
  assert(thread->hierarchicalHeap != NULL);

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
  // HM_HH_updateValues(oldCurrentThread, s->frontier);
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

  if (HM_HH_beforeBoot(s))
    handOverHeapBeforeBoot(s, oldCurrentThread,
      threadObjptrToStruct(s, pointerToObjptr(p, NULL)));

  switchToThread(s, pointerToObjptr(p, NULL));
  /* SAM_NOTE: TODO: do signal handlers work properly? */
  s->atomicState--;
  switchToSignalHandlerThreadIfNonAtomicAndSignalPending(s);

  assert(threadAndHeapOkay(s));
  s->frontier = HM_HH_getFrontier(getThreadCurrent(s));
  s->limitPlusSlop = HM_HH_getLimit(getThreadCurrent(s));
  s->limit = s->limitPlusSlop - GC_HEAP_LIMIT_SLOP;
  HM_ensureHierarchicalHeapAssurances(s, FALSE, getThreadCurrent(s)->bytesNeeded, FALSE);

  LOG(LM_THREAD, LL_DEBUG,
    "GC_switchToThread succeeded: old current = %p, new = %p",
    (void*)objptrToPointer(currop, NULL),
    (void*)objptrToPointer(getThreadCurrentObjptr(s), NULL));

  leave(s);
  // endAtomic (s);
  // assert(invariantForMutatorStack(s));
  //LEAVE0 (s);
}
