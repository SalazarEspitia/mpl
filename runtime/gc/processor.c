#include "processor.h"

#include <pthread.h>

/***************************/
/* Static Global Variables */
/***************************/
/* variables used in processor initialization */
static volatile bool Proc_beginInit = FALSE;
static volatile uint32_t Proc_initializedCount = 0;

/************************/
/* Function definitions */
/************************/

int32_t Proc_processorNumber (GC_state s) {
  return s->procNumber;
}

void Proc_waitForInitialization (GC_state s) {
  /* A processor that waits here holds no pointer to a heap record, so it is
   * quiescent for the reclamation of heap records (hhEBR). Without this its
   * announcement would stay at epoch 0, the epoch could not advance, and
   * the heap records that the collections before the scheduler's boot
   * retire (one for each collection of the root heap) would not be freed
   * until the boot; never, in a program without the scheduler. */
  HH_EBR_enterQuiescentState(s);

  size_t pcounter = 0;
  while (!Proc_beginInit) {
    GC_MayTerminateThreadRarely(s, &pcounter);
  }

  __sync_add_and_fetch (&Proc_initializedCount, 1);

  while (!Proc_isInitialized (s)) {
    GC_MayTerminateThreadRarely(s, &pcounter);
  }

  /* Announce the current epoch before running any SML code. The fence
   * makes the announcement visible to the other processors before this one
   * reads its first pointer to a heap record. */
  HH_EBR_leaveQuiescentState(s);
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

void Proc_signalInitialization (GC_state s) {
  Proc_initializedCount = 1;
  Proc_beginInit = TRUE;

  while (!Proc_isInitialized (s)) { }
}

bool Proc_isInitialized (GC_state s) {
  return Proc_initializedCount == s->numberOfProcs;
}
