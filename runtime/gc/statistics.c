/* Copyright (C) 1999-2005 Henry Cejtin, Matthew Fluet, Suresh
 *    Jagannathan, and Stephen Weeks.
 * Copyright (C) 1997-2000 NEC Research Institute.
 *
 * MLton is released under a HPND-style license.
 * See the file MLton-LICENSE for details.
 */

/******************************/
/* Static Function Prototypes */
/******************************/
void outputCollectionStatisticsJSON(FILE* out,
                                    const char *type,
                                    struct rusage *ru,
                                    uintmax_t num,
                                    uintmax_t bytes);

/************************/
/* Function Definitions */
/************************/

struct GC_cumulativeStatistics *newCumulativeStatistics(void) {
  struct GC_cumulativeStatistics *cumulativeStatistics;

  cumulativeStatistics = (struct GC_cumulativeStatistics *)
    malloc (sizeof (struct GC_cumulativeStatistics));
  cumulativeStatistics->bytesAllocated = 0;
  cumulativeStatistics->bytesHHLocaled = 0;
  cumulativeStatistics->bytesReclaimedByLocal = 0;
  cumulativeStatistics->bytesReclaimedByCC = 0;
  cumulativeStatistics->bytesInScopeForLocal = 0;
  cumulativeStatistics->bytesInScopeForCC = 0;
  cumulativeStatistics->maxBytesLive = 0;
  cumulativeStatistics->maxStackSize = 0;
  cumulativeStatistics->maxStackFramesWalkedForHeartbeat = 0;
  cumulativeStatistics->maxStackSizeForHeartbeat = 0;
  cumulativeStatistics->numCopyingGCs = 0;
  cumulativeStatistics->numMarkCompactGCs = 0;
  cumulativeStatistics->numMinorGCs = 0;
  cumulativeStatistics->numHHLocalGCs = 0;
  cumulativeStatistics->numCCs = 0;
  cumulativeStatistics->numDisentanglementChecks = 0;
  cumulativeStatistics->numEntanglements = 0;
  cumulativeStatistics->numChecksSkipped = 0;
  cumulativeStatistics->numSuspectsMarked = 0;
  cumulativeStatistics->numSuspectsCleared = 0;
  cumulativeStatistics->bytesPinnedEntangled = 0;
  cumulativeStatistics->currentPhaseBytesPinnedEntangled = 0;
  cumulativeStatistics->bytesPinnedEntangledWatermark = 0;
  cumulativeStatistics->approxRaceFactor = 0;

  cumulativeStatistics->timeLocalGC.tv_sec = 0;
  cumulativeStatistics->timeLocalGC.tv_nsec = 0;
  cumulativeStatistics->timeLocalPromo.tv_sec = 0;
  cumulativeStatistics->timeLocalPromo.tv_nsec = 0;
  cumulativeStatistics->timeCC.tv_sec = 0;
  cumulativeStatistics->timeCC.tv_nsec = 0;

  rusageZero (&cumulativeStatistics->ru_gc);
  rusageZero (&cumulativeStatistics->ru_gcCopying);
  rusageZero (&cumulativeStatistics->ru_gcMarkCompact);
  rusageZero (&cumulativeStatistics->ru_gcMinor);
  rusageZero (&cumulativeStatistics->ru_gcHHLocal);
  rusageZero (&cumulativeStatistics->ru_gcCC);
  rusageZero (&cumulativeStatistics->ru_thread);
  cumulativeStatistics->ruThreadRecorded = FALSE;

  timespec_now(&(cumulativeStatistics->lastHeartbeatHandlerTimestamp));
  timespec_now(&(cumulativeStatistics->lastHeartbeatSignalTimestamp));

  struct timespec bucketWidth;
  bucketWidth.tv_sec = 0;
  bucketWidth.tv_nsec = 100 * 1000; // 100 us
  cumulativeStatistics->heartbeatHandlers = TimeHistogram_new(15, &bucketWidth);
  cumulativeStatistics->heartbeatSignals = TimeHistogram_new(15, &bucketWidth);

  return cumulativeStatistics;
}

struct GC_lastMajorStatistics *newLastMajorStatistics(void) {
  struct GC_lastMajorStatistics *lastMajorStatistics;

  lastMajorStatistics = (struct GC_lastMajorStatistics *)
    malloc (sizeof (struct GC_lastMajorStatistics));
  lastMajorStatistics->bytesHashConsed = 0;
  lastMajorStatistics->bytesLive = 0;
  /* RAM_NOTE: Does this need to be under an option? */
  lastMajorStatistics->kind = GC_COPYING;
  lastMajorStatistics->numMinorGCs = 0;

  return lastMajorStatistics;
}

void S_outputCumulativeStatisticsJSON(
    FILE* out, struct GC_cumulativeStatistics* statistics) {
  uintmax_t gcTime;

  gcTime = rusageTime (&statistics->ru_gc);

  fprintf(out, "{ ");
  {
    fprintf(out, "\"gcStats\" : ");
    fprintf(out, "[");
    {
      outputCollectionStatisticsJSON(out,
                                     "Hierarchical Heap Local",
                                     &statistics->ru_gcHHLocal,
                                     statistics->numHHLocalGCs,
                                     statistics->bytesHHLocaled);
    }
    fprintf(out, "]");

    fprintf(out, ", ");

    fprintf(out, "\"gcTime\" : %"PRIuMAX, gcTime);

    fprintf(out, ", ");

    fprintf(out, "\"bytesAllocated\" : %"PRIuMAX, statistics->bytesAllocated);

    fprintf(out, ", ");

    fprintf(out, "\"maxStackSize\" : %"PRIuMAX, statistics->maxStackSize);
  }
  fprintf(out, " }");
}

/*******************************/
/* Static Function Definitions */
/*******************************/

void outputCollectionStatisticsJSON(FILE* out,
                                    const char *type,
                                    struct rusage *ru,
                                    uintmax_t num,
                                    uintmax_t bytes) {
  uintmax_t ms = rusageTime(ru);

  fprintf(out, "{ ");
  {
    fprintf(out, "\"type\" : \"%s\"", type);

    fprintf(out, ", ");

    fprintf(out, "\"time\" : %"PRIuMAX, ms);

    fprintf(out, ", ");

    fprintf(out, "\"number\" : %"PRIuMAX, num);

    fprintf(out, ", ");

    fprintf(out, "\"bytes\" : %"PRIuMAX, bytes);
  }
  fprintf(out, " }");
}
