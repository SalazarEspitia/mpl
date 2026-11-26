(* Does the write barrier's entangled branch (assign.c:213-220) compute an unpin
 * depth deeper than the writer's own depth?  It calls
 *   manage_entangled (s, src, HM_getChunkOf(dstp)->decheckState)
 * so the depth comes from two chunk labels, lcaHeapDepth(label(dst), label(src)),
 * and neither is the writing task.
 *
 * Fork tree (every par is promoted: each body spins before it does anything):
 *
 *   root --par--> E (path L, heap depth 2)            the whole experiment
 *     phase 1:  E --par--> . --par--> A (path L.L.L, depth 4) allocates X; joins: X at depth 2
 *     phase 2:  E --par--> . --par--> T (path L.L.L, depth 4) allocates D, publishes it
 *                    \--> W (path L.R, depth 3; must be stolen) reads D, does D[0] := X
 *
 * label(X) and label(D) have the same path, so lcaHeapDepth gives 3+1 = 4 (decheck.c:286-288);
 * W is at depth 3.  With the debug runtime the expected stop is
 *   hierarchical-heap.c:571  assert(depth <= thread->currentDepth)
 * reached from decheck.c:449.
 *)
val par = ForkJoin.par

fun now () = Time.toMilliseconds (Time.now ())
fun spin ms =
  let val t0 = now ()
      fun loop (i: int) = if now () - t0 >= Int.toLarge ms then i else loop (i+1)
  in ignore (loop 0) end

val big = 200000

fun experiment () =
  let
    val () = spin 40
    val cell : int array array option ref = ref NONE
    val flag = ref 0
    (* phase 1 *)
    val ((x, ()), ()) =
      par (fn () => par (fn () => (spin 40; Array.array (big, 0)), fn () => ()),
           fn () => ())
    val () = print "phase 1 done\n"
    fun waitFlag t0 =
      if !flag = 1 then true
      else if now () - t0 > 10000 then false
      else waitFlag t0
    fun taskT () =
      let
        val () = spin 40
        val x0 = Array.array (1, 0)
        val d = Array.array (big, x0)
        val () = cell := SOME d
      in
        if waitFlag (now ()) then print "T: W wrote\n" else print "T: timeout (W not stolen?)\n"
      end
    fun taskW () =
      let
        fun wait t0 =
          case !cell of
            SOME d => SOME d
          | NONE => if now () - t0 > 10000 then NONE else wait t0
      in
        case wait (now ()) of
          NONE => print "W: timeout (not concurrent with T)\n"
        | SOME d => (Array.update (d, 0, x); flag := 1; print "W: wrote X into D\n")
      end
    val _ = par (fn () => par (taskT, fn () => ()), taskW)
  in
    print "phase 2 done\n"
  end

val _ = par (experiment, fn () => ())
val _ = print "end\n"
