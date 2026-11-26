(* wb217.sml, continued in the release runtime: what the too-deep heap does afterwards.
 *
 * After W's write, W's chain has a heap one level deeper than W (HM_HH_getHeapAtDepth,
 * hierarchical-heap.c:589-591, made it the chain's head).  The join leaves E's thread at depth 2
 * with a head heap at depth 3 (HM_HH_promoteChunks, :389-413, merges only one level), and E now
 * allocates there (HM_HH_extend, :626-638).  E then builds a list and forks: the stolen right
 * branch reads the list as ancestor data (no barrier for immutable reads; its chunks' labels are
 * E's), while the left branch, at depth 3, may collect depth 3 (the stolen fork is at depth 2),
 * which holds the list.
 *
 *   wb217b write     the run with W's write
 *   wb217b nowrite   the control: the same program without it
 *)
val par = ForkJoin.par
val doWrite = (case CommandLine.arguments () of ["nowrite"] => false | _ => true)

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
    val ((x, ()), ()) =
      par (fn () => par (fn () => (spin 40; Array.array (big, 0)), fn () => ()),
           fn () => ())
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
        if waitFlag (now ()) then () else print "T: timeout (W not stolen?)\n"
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
        | SOME d => ((if doWrite then Array.update (d, 0, x) else ()); flag := 1)
      end
    val _ = par (fn () => par (taskT, fn () => ()), taskW)
    val () = print "phase 2 done\n"

    (* phase 3 *)
    val n = 60000   (* keeps the sum within a 32-bit Int *)
    val lst = List.tabulate (n, fn i => i)
    val expect = List.foldl op+ 0 lst
    val bad = ref 0
    val rounds = ref 0
    fun reader () =
      let
        val t0 = now ()
        fun loop () =
          if now () - t0 > 1500 then () else
          ( (if List.foldl op+ 0 lst <> expect then bad := !bad + 1 else ())
          ; rounds := !rounds + 1
          ; loop () )
      in loop () end
    fun churn () =
      let
        val () = spin 40
        val t0 = now ()
        fun loop (acc: int) =
          if now () - t0 > 1500 then acc
          else loop (acc + List.length (List.tabulate (50000, fn i => i + acc)))
      in ignore (loop 0) end
    val _ = par (churn, reader)
  in
    print ("phase 3 done: reader rounds " ^ Int.toString (!rounds) ^ ", wrong sums " ^ Int.toString (!bad)
           ^ ", list still sums right: " ^ Bool.toString (List.foldl op+ 0 lst = expect) ^ "\n")
  end

val _ = par (experiment, fn () => ())
val _ = print "end\n"
