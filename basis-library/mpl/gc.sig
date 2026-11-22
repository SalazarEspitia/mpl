(* Copyright (C) 2020 Sam Westrick.
 *
 * MLton is released under a HPND-style license.
 * See the file MLton-LICENSE for details.
 *)

signature MPL_GC =
sig
  (* The current size of the heap, in bytes: the blocks the run-time system
   * has taken from its block allocator, for objects or for its own data
   * (remembered sets, work lists, ...), and not yet given back. Other
   * processors may be allocating or collecting while it is read, so it is a
   * snapshot of a moving value. Free space inside blocks counts as used.
   *)
  val currentHeapSize: unit -> IntInf.int

  (* The memory the block allocator has mapped from the system and not
   * released, in bytes; at least currentHeapSize. Mapped memory is reused,
   * and not released while the program runs (except very large blocks), so
   * this is also about the most it has held so far.
   *)
  val currentHeapMapped: unit -> IntInf.int

  (* How many times the read barrier has been triggered, which
   * checks that the program is disentangled.
   *)
  val numberDisentanglementChecks: unit -> IntInf.int

  (* How many times the entanglement is detected *)
  val numberEntanglements: unit -> IntInf.int

  val approxRaceFactor: unit -> Real32.real

  val numberSuspectsMarked: unit -> IntInf.int
  val numberSuspectsCleared: unit -> IntInf.int
  val bytesPinnedEntangled: unit -> IntInf.int
  val bytesPinnedEntangledWatermark: unit -> IntInf.int

  val getControlMaxCCDepth: unit -> int

  val maxStackSizeForHeartbeat: unit -> IntInf.int
  val maxStackFramesWalkedForHeartbeat: unit -> IntInf.int

  (* The following are all cumulative statistics (initially 0, and only
   * increase throughout execution).
   *
   * Each stat comes in two flavors: total, and per-processor. The total
   * stat is always just a sum of the corresponding per-proc stats.
   *
   * Naming convention for `stat` of type `t`:
   *   val stat: unit -> t
   *   val statOfProc: int -> t
   *)

  val bytesAllocated: unit -> IntInf.int
  val bytesAllocatedOfProc: int -> IntInf.int

  val localBytesReclaimed: unit -> IntInf.int
  val localBytesReclaimedOfProc: int -> IntInf.int

  val bytesInScopeForLocal: unit -> IntInf.int

  val numLocalGCs: unit -> IntInf.int
  val numLocalGCsOfProc: int -> IntInf.int

  val localGCTime: unit -> Time.time
  val localGCTimeOfProc: int -> Time.time

  val promoTime: unit -> Time.time
  val promoTimeOfProc: int -> Time.time

  val numCCs: unit -> IntInf.t
  val numCCsOfProc: int -> IntInf.t

  val ccBytesReclaimed: unit -> IntInf.int
  val ccBytesReclaimedOfProc: int -> IntInf.int

  val bytesInScopeForCC: unit -> IntInf.int

  val ccTime: unit -> Time.time
  val ccTimeOfProc: int -> Time.time

  (* DEPRECATED *)
  val rootBytesReclaimed: unit -> IntInf.int
  val rootBytesReclaimedOfProc: int -> IntInf.int
  val internalBytesReclaimed: unit -> IntInf.int
  val internalBytesReclaimedOfProc: int -> IntInf.int
  val numRootCCs: unit -> IntInf.int
  val numRootCCsOfProc: int -> IntInf.int
  val numInternalCCs: unit -> IntInf.int
  val numInternalCCsOfProc: int -> IntInf.int
  val rootCCTime: unit -> Time.time
  val rootCCTimeOfProc: int -> Time.time
  val internalCCTime: unit -> Time.time
  val internalCCTimeOfProc: int -> Time.time
end
