/* Copyright (C) 2012,2015 Matthew Fluet.
 * Copyright (C) 1999-2008 Henry Cejtin, Matthew Fluet, Suresh
 *    Jagannathan, and Stephen Weeks.
 * Copyright (C) 1997-2000 NEC Research Institute.
 *
 * MLton is released under a HPND-style license.
 * See the file MLton-LICENSE for details.
 */

static void displayCol (FILE *out, size_t width, const char *s) {
  size_t extra;
  size_t i;
  size_t len;

  len = strlen (s);
  if (len < width) {
    extra = width - len;
    for (i = 0; i < extra; i++)
      fprintf (out, " ");
  }
  fprintf (out, "%s\t", s);
}

static void displayCollectionStats (FILE *out, const char *name, struct rusage *ru,
                                    uintmax_t num, uintmax_t bytes) {
  uintmax_t ms;

  ms = rusageTime (ru);
  fprintf (out, "%s", name);
  displayCol (out, 7, uintmaxToCommaString (ms));
  displayCol (out, 7, uintmaxToCommaString (num));
  displayCol (out, 15, uintmaxToCommaString (bytes));
  displayCol (out, 15,
              (ms > 0)
              ? uintmaxToCommaString ((uintmax_t)(1000.0 * (float)bytes/(float)ms))
              : "-");
  fprintf (out, "\n");
}

static void displayHHAllocStats(FILE *out, GC_state s) {
  FixedSizeAllocator fsa = getHHAllocator(s);
  fprintf(out, "num hh allocated: %zu\n", numFixedSizeAllocated(fsa));
  fprintf(out, "num hh freed: %zu\n", numFixedSizeFreed(fsa));
  fprintf(out, "num hh shared freed: %zu\n", numFixedSizeSharedFreed(fsa));
  fprintf(out, "num hh currently in use: %zu\n", numFixedSizeCurrentlyInUse(fsa));
  fprintf(out, "current hh alloc capacity: %zu\n", currentFixedSizeCapacity(fsa));
  fprintf(out, "current hh space util: %.1f%%\n",
          100.0 * currentFixedSizeSpaceUtilization(fsa));

  size_t maxSize = 0;
  size_t maxHeight = 0;
  for (HM_HierarchicalHeap cursor = getHierarchicalHeapCurrent(s);
       cursor != NULL;
       cursor = cursor->nextAncestor)
  {
    if (cursor->numDependants > maxSize)
      maxSize = cursor->numDependants;
    if (cursor->heightDependants > maxHeight) maxHeight =
      cursor->heightDependants;
  }

  fprintf(out, "current max num hh dependants: %zu\n", maxSize);
  fprintf(out, "current max dependant tree height: %zu\n", maxHeight);
}


static uintmax_t timespecToMillis (struct timespec *t) {
  return (uintmax_t)t->tv_sec * 1000 + (uintmax_t)t->tv_nsec / 1000000;
}

static void displayCumulativeStatistics (FILE *out, struct GC_cumulativeStatistics *cumulativeStatistics) {
  struct rusage ru_total;
  uintmax_t totalTime;
  uintmax_t gcTime;

#ifdef __MACH__
  getrusage_thread (&ru_total);
#else
  getrusage (RUSAGE_THREAD, &ru_total);
#endif
  totalTime = rusageTime (&ru_total);
  gcTime = rusageTime (&cumulativeStatistics->ru_gc);
  fprintf (out, "GC type\t\ttime ms\t number\t\t  bytes\t      bytes/sec\n");
  fprintf (out, "-------------\t-------\t-------\t---------------\t---------------\n");
  displayCollectionStats
      (out, "HHLocal\t\t",
       &cumulativeStatistics->ru_gcHHLocal,
       cumulativeStatistics->numHHLocalGCs,
       cumulativeStatistics->bytesHHLocaled);
  fprintf (out, "total time: %s ms\n",
           uintmaxToCommaString (totalTime));
  fprintf (out, "total GC time: %s ms (%.1f%%)\n",
           uintmaxToCommaString (gcTime),
           (0 == totalTime) ?
           0.0 : 100.0 * ((double) gcTime) / (double)totalTime);
  fprintf (out, "total bytes allocated: %s bytes\n",
           uintmaxToCommaString (cumulativeStatistics->bytesAllocated));
  fprintf (out, "local collections: %s\n",
           uintmaxToCommaString (cumulativeStatistics->numHHLocalGCs));
  fprintf (out, "local collections: wall time: %s ms, plus %s ms on remembered sets\n",
           uintmaxToCommaString (timespecToMillis (&cumulativeStatistics->timeLocalGC)),
           uintmaxToCommaString (timespecToMillis (&cumulativeStatistics->timeLocalPromo)));
  fprintf (out, "local collections: bytes in scope: %s bytes\n",
           uintmaxToCommaString (cumulativeStatistics->bytesInScopeForLocal));
  fprintf (out, "local collections: bytes reclaimed: %s bytes\n",
           uintmaxToCommaString (cumulativeStatistics->bytesReclaimedByLocal));
  fprintf (out, "concurrent collections: %s\n",
           uintmaxToCommaString (cumulativeStatistics->numCCs));
  fprintf (out, "concurrent collections: wall time: %s ms\n",
           uintmaxToCommaString (timespecToMillis (&cumulativeStatistics->timeCC)));
  fprintf (out, "concurrent collections: bytes in scope: %s bytes\n",
           uintmaxToCommaString (cumulativeStatistics->bytesInScopeForCC));
  fprintf (out, "concurrent collections: bytes reclaimed: %s bytes\n",
           uintmaxToCommaString (cumulativeStatistics->bytesReclaimedByCC));
  fprintf (out, "max stack size: %s bytes\n",
           uintmaxToCommaString (cumulativeStatistics->maxStackSize));
  /* PROTOTYPE: weak cells (entanglement-suspects.h) */
  /* "entries visited by clears" counts every entry a clear at a join looked
   * at (an entry moved to a shallower list is counted again later), so it can
   * exceed "marked"; "bits cleared" counts the clears that succeeded. Per
   * processor these lines do not balance (an entry is made on one processor
   * and dropped on another; so are cells); the sums over processors do:
   * marked = bits cleared + dropped dead + dropped forwarded + entries still
   * in lists at exit. */
  fprintf (out, "suspects: marked %"PRIuMAX", entries visited by clears %"PRIuMAX", bits cleared %"PRIuMAX"\n",
           cumulativeStatistics->numSuspectsMarked,
           cumulativeStatistics->numSuspectsCleared,
           cumulativeStatistics->numSuspectBitsCleared);
  fprintf (out, "suspect cells: allocated %"PRIuMAX", freed %"PRIuMAX", lost install races %"PRIuMAX", chunk frees that waited for a lease %"PRIuMAX", lease wait retries %"PRIuMAX"\n",
           cumulativeStatistics->numSuspectCellsAllocated,
           cumulativeStatistics->numSuspectCellsFreed,
           cumulativeStatistics->numSuspectCellInstallRaces,
           cumulativeStatistics->numSuspectCellLeaseWaits,
           cumulativeStatistics->numSuspectCellLeaseRetries);
  fprintf (out, "suspect chunks freed: with a cell %"PRIuMAX", with entries outstanding %"PRIuMAX" (%"PRIuMAX" entries)\n",
           cumulativeStatistics->numChunksFreedWithSuspectCell,
           cumulativeStatistics->numChunksFreedWithSuspectEntries,
           cumulativeStatistics->numSuspectEntriesOutlivedChunk);
  fprintf (out, "suspect entries dropped dead: clear %"PRIuMAX", parallel clear %"PRIuMAX", local collection %"PRIuMAX"\n",
           cumulativeStatistics->numSuspectsDroppedDeadClear,
           cumulativeStatistics->numSuspectsDroppedDeadParClear,
           cumulativeStatistics->numSuspectsDroppedDeadLGC);
  fprintf (out, "suspect entries dropped forwarded: clear %"PRIuMAX", parallel clear %"PRIuMAX", local collection %"PRIuMAX"\n",
           cumulativeStatistics->numSuspectsDroppedForwardedClear,
           cumulativeStatistics->numSuspectsDroppedForwardedParClear,
           cumulativeStatistics->numSuspectsDroppedForwardedLGC);
#if ASSERT
  fprintf (out, "suspects pinned in a chunk a local collection frees (debug runtime): %"PRIuMAX"\n",
           cumulativeStatistics->numSuspectsPinnedInChunkLGCFrees);
#endif
}

static void displayCumulativeStatisticsJSON (FILE *out, GC_state s) {
  struct rusage ru_total;
  uintmax_t totalTime;

#ifdef __MACH__
  getrusage_thread (&ru_total);
#else
  getrusage (RUSAGE_THREAD, &ru_total);
#endif
  totalTime = rusageTime (&ru_total);

  fprintf(out, "{ ");

  {
    /* Print per-thread statistics */
    fprintf(out, "\"perThread\" : ");
    fprintf(out, "[");
    {
      if (s->procStates) {
        /* print cumulativeStatistics for each processor, separated by commas */
        uint32_t proc;
        for (proc = 0; proc < s->numberOfProcs - 1; proc++) {
          S_outputCumulativeStatisticsJSON(
              out, s->procStates[proc].cumulativeStatistics);
          fprintf(out, ", ");
        }
        S_outputCumulativeStatisticsJSON(
            out, s->procStates[proc].cumulativeStatistics);
      } else {
        S_outputCumulativeStatisticsJSON(out, s->cumulativeStatistics);
      }
    }
    fprintf(out, "]");

    fprintf(out, ", ");

    /* print global statistics */
    fprintf(out, "\"totalTime\" : %"PRIuMAX, totalTime);
  }

  fprintf(out, " }");
}


void displayHeartbeatStatistics(FILE *out, GC_state s) {
  TimeHistogram signalsH = s->cumulativeStatistics->heartbeatSignals;
  double signalsDist[TimeHistogram_numBuckets(signalsH)];
  size_t totalS = TimeHistogram_reportDistribution(signalsH, signalsDist);

  TimeHistogram handlersH = s->cumulativeStatistics->heartbeatHandlers;
  double handlersDist[TimeHistogram_numBuckets(handlersH)];
  size_t totalH = TimeHistogram_reportDistribution(handlersH, handlersDist);

  fprintf(out, "              ");
  fprintf(out, "  count ");
  for (size_t bucket = 0; bucket < TimeHistogram_numBuckets(signalsH); bucket++) {
    fprintf(out, "%*zu ", 3, bucket);
  }
  fprintf(out, "\n");

  fprintf(out, "[%*d] signals  ", 2, Proc_processorNumber(s));
  fprintf(out, "%*zu ", 7, totalS); 
  for (size_t bucket = 0; bucket < TimeHistogram_numBuckets(signalsH); bucket++) {
    fprintf(out, "%*d%% ", 2, (int)(100.0 * signalsDist[bucket]));
  }
  fprintf(out, "\n");

  fprintf(out, "[%*d] handlers ", 2, Proc_processorNumber(s));
  fprintf(out, "%*zu ", 7, totalH);
  for (size_t bucket = 0; bucket < TimeHistogram_numBuckets(handlersH); bucket++) {
    fprintf(out, "%*d%% ", 2, (int)(100.0 * handlersDist[bucket]));
  }
  fprintf(out, "\n");
}

void GC_done(GC_state s) {
  GC_PthreadAtExit(s);

  if (s->controls->heartbeatStats) {
    for (uint32_t proc = 0; proc < s->numberOfProcs; proc++) {
      displayHeartbeatStatistics(s->controls->summaryFile, &(s->procStates[proc]));
    }
  }

  if (s->controls->summary) {
    if (HUMAN == s->controls->summaryFormat) {
      if (s->procStates) {
        for (uint32_t proc = 0; proc < s->numberOfProcs; proc++) {
          fprintf (s->controls->summaryFile, "Thread [%d]::\n", proc);
          displayCumulativeStatistics
              (s->controls->summaryFile,
               s->procStates[proc].cumulativeStatistics);
          displayHHAllocStats(s->controls->summaryFile, &(s->procStates[proc]));
        }
      } else {
        displayCumulativeStatistics(s->controls->summaryFile,
                                    s->cumulativeStatistics);
        displayHHAllocStats(s->controls->summaryFile, s);
      }
    } else if (JSON == s->controls->summaryFormat) {
      displayCumulativeStatisticsJSON(s->controls->summaryFile, s);
      fprintf(s->controls->summaryFile, "\n");
    }
  }
}
