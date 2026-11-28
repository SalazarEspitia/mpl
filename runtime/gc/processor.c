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
  size_t pcounter = 0;
  /* Acquire: pairs with the release in Proc_signalInitialization, so the
   * increment below sees Proc_initializedCount = 1. */
  while (!__atomic_load_n (&Proc_beginInit, __ATOMIC_ACQUIRE)) {
    GC_MayTerminateThreadRarely(s, &pcounter);
  }

  __sync_add_and_fetch (&Proc_initializedCount, 1);

  while (!Proc_isInitialized (s)) {
    GC_MayTerminateThreadRarely(s, &pcounter);
  }
}

void Proc_signalInitialization (GC_state s) {
  Proc_initializedCount = 1;
  /* Release: a processor that sees Proc_beginInit also sees the count of 1.
   * With two plain stores, a weakly ordered processor (Arm) may make the
   * flag visible first; a waiter could then increment from 0 and lose its
   * increment to the store of 1, and the count would never reach
   * numberOfProcs. */
  __atomic_store_n (&Proc_beginInit, TRUE, __ATOMIC_RELEASE);

  while (!Proc_isInitialized (s)) { }
}

bool Proc_isInitialized (GC_state s) {
  return Proc_initializedCount == s->numberOfProcs;
}
