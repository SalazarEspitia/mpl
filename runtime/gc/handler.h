/* Copyright (C) 2019 Matthew Fluet.
 * Copyright (C) 1999-2005 Henry Cejtin, Matthew Fluet, Suresh
 *    Jagannathan, and Stephen Weeks.
 * Copyright (C) 1997-2000 NEC Research Institute.
 *
 * MLton is released under a HPND-style license.
 * See the file MLton-LICENSE for details.
 */

#if (defined (MLTON_GC_INTERNAL_FUNCS))

static inline void switchToSignalHandlerThreadIfNonAtomicAndSignalPending (GC_state s);

#endif /* (defined (MLTON_GC_INTERNAL_FUNCS)) */

#if (defined (MLTON_GC_INTERNAL_BASIS))

void relayerLoop(GC_state s);

PRIVATE void GC_startSignalHandler (GC_state s);
PRIVATE void GC_finishSignalHandler (GC_state s);

PRIVATE void GC_sendHeartbeatToOtherProc(GC_state s, uint32_t target);

/** Returns the HH that we abandoned. This should then be passed to
  * GC_handlerLeaveHeapOfThread, to get back to it.
  */
PRIVATE pointer GC_handlerEnterHeapOfThread(GC_state s, objptr threadp);
PRIVATE void GC_handlerLeaveHeapOfThread(GC_state s, objptr threadp, pointer abandonedHH);

/** The same for a C-call worker running an exported function in its caller's
  * heap, before the scheduler boots only. After the boot it returns NULL (the
  * worker stays in its own heap), and the leave then does nothing.
  */
PRIVATE pointer GC_callbackEnterHeapOfThread(GC_state s, objptr threadp);
PRIVATE void GC_callbackLeaveHeapOfThread(GC_state s, objptr threadp, pointer abandonedHH);

#endif /* (defined (MLTON_GC_INTERNAL_BASIS)) */

PRIVATE void GC_handler (int signum);
