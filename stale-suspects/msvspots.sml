(* msvspots: a stress test for entanglement with pins and suspects.
 *
 * Each task has a table of cells; each cell holds a ref. Children write fresh
 * cells into their parent's table (down-pointers), read the cells their sibling
 * writes there (entangled reads of mutable data), and keep what they read across
 * allocation. Tables are sometimes rebuilt by tabulate, keeping half of their
 * old cells. Every cell is self-describing (cellOk), and every table is checked
 * after each round. Output: "procs P mode safe F sum S bad B"; S is
 * deterministic, B must be 0.
 *
 * Arguments: mode (safe), rounds, max par depth, recursion depth, steps per
 * round, and optionally the features to use (default "ewt"):
 * e = read the sibling's half of the parent's table (entanglement),
 * w = write fresh cells into the parent's table (down-pointers),
 * t = rebuild tables by tabulate. *)

val (mode, rounds, maxD, depthRec, steps, feats) =
  case CommandLine.arguments () of
    [m, a, b, c, d] =>
      (m, valOf (Int.fromString a), valOf (Int.fromString b),
       valOf (Int.fromString c), valOf (Int.fromString d), "ewt")
  | [m, a, b, c, d, f] =>
      (m, valOf (Int.fromString a), valOf (Int.fromString b),
       valOf (Int.fromString c), valOf (Int.fromString d), f)
  | _ => ("safe", 2, 3, 2000, 20000, "ewt")
fun feat c = CharVector.exists (fn x => x = c) feats

val n = 1024
fun next (s: Word.word) = s * 0w1103515245 + 0w12345
fun pick (s: Word.word, k) = Word.toInt (Word.>> (s, 0w8)) mod k

(* a cell for slot i: tag i, a ref to [i, i+1, ..., i+m-1], a vector of i*j *)
datatype cell = C of {tag: int, l: int list ref, v: int vector}
fun mkCell (i, k) =
  C {tag = i,
     l = ref (List.tabulate (k mod 7 + 1, fn j => i + j)),
     v = Vector.tabulate (k mod 5 + 1, fn j => i * j)}
fun cellOk (i, C {tag, l, v}) =
  let
    fun run ([], _) = true
      | run (x :: r, e) = x = e andalso run (r, e + 1)
  in
    tag = i andalso not (List.null (!l)) andalso run (!l, i)
    andalso Vector.foldli (fn (j, x, b) => b andalso x = i * j) true v
  end
(* rewrite the ref of a cell (a write into an old object, with the barrier) *)
fun touch (C {tag, l, ...}, k) = l := List.tabulate (k mod 9 + 1, fn j => tag + j)

fun badIn t =
  let fun go (i, b) = if i >= n then b else go (i + 1, if cellOk (i, Array.sub (t, i)) then b else b + 1)
  in go (0, 0) end
fun tagSum t = Array.foldl (fn (C {tag, l, ...}, s) => s + tag + List.length (!l)) 0 t

fun churn (0, acc) = acc
  | churn (k, acc) = churn (k - 1, acc + List.length [k, k + 1, k + 2])

(* k random steps on table t: replace a cell, rewrite a cell's ref, or (rarely)
 * rebuild the whole table by tabulate, keeping half of the old cells *)
fun mutate (t, s, 0) = (t, s)
  | mutate (t, s, k) =
      let
        val s = next s
        val i = pick (s, n)
        val r = pick (next s, 64)
        val t =
          if r = 0 andalso feat #"t" then
            Array.tabulate (n, fn j =>
              if j mod 2 = 0 then Array.sub (t, j) else mkCell (j, k + j))
          else if r < 24 then (Array.update (t, i, mkCell (i, k)); t)
          else (touch (Array.sub (t, i), k); t)
      in
        mutate (t, s, k - 1)
      end

(* deep recursion: a cell held in every frame; the bottom frame does the work *)
fun deep (0, work) = work ()
  | deep (d, work) =
      let
        val held = mkCell (d, d)
        val (sum, bad) = deep (d - 1, work)
      in
        (sum, if cellOk (d, held) then bad else bad + 1)
      end

(* read some slots of the parent's table (a sibling writes them), keep them
 * across allocation, check them twice *)
fun readSibling (pt, lo, hi, s) =
  let
    fun grab (0, s, acc) = (s, acc)
      | grab (k, s, acc) =
          let val s = next s val i = lo + pick (s, hi - lo)
          in grab (k - 1, s, (i, Array.sub (pt, i)) :: acc) end
    val (s, held) = grab (64, s, [])
    val b1 = List.foldl (fn ((i, c), b) => if cellOk (i, c) then b else b + 1) 0 held
    val _ = churn (steps, 0)
    val b2 = List.foldl (fn ((i, c), b) => if cellOk (i, c) then b else b + 1) 0 held
  in (s, b1 + b2) end

(* write fresh cells into my half of the parent's table *)
fun writeParent (pt, lo, hi, s, 0) = s
  | writeParent (pt, lo, hi, s, k) =
      let val s = next s val i = lo + pick (s, hi - lo)
      in Array.update (pt, i, mkCell (i, k)); writeParent (pt, lo, hi, s, k - 1) end

fun node (d, s, parent) =
  let
    val t0 = Array.tabulate (n, fn i => mkCell (i, i))
    val other = Array.tabulate (n, fn i => mkCell (i, i + 1))
    fun round (0, t, s, sum, bad) = (t, s, sum, bad)
      | round (r, t, s, sum, bad) =
          let
            (* the bottom of a deep recursion mutates t and the parent's
             * table, and reads the sibling's half of it *)
            val tr = ref t
            val sr = ref s
            fun work () =
              let
                val (t', s') = mutate (!tr, !sr, steps)
                val (s', rb) =
                  case parent of
                    NONE => (s', 0)
                  | SOME (pt, lo, hi, slo, shi) =>
                      let
                        val s' = if feat #"w" then writeParent (pt, lo, hi, s', steps div 8) else s'
                      in
                        if feat #"e" then readSibling (pt, slo, shi, s') else (s', 0)
                      end
                val _ = churn (steps, 0)
              in
                tr := t'; sr := s'; (0, rb)
              end
            val (_, db) = deep (depthRec, work)
            val t = !tr
            val s = !sr
            val (csum, cbad) =
              if d >= maxD then (0, 0)
              else
                let
                  val mid = n div 2
                  val ((s1, b1), (s2, b2)) =
                    ForkJoin.par
                      (fn () => node (d + 1, next (s + 0w1), SOME (t, 0, mid, mid, n)),
                       fn () => node (d + 1, next (s + 0w2), SOME (t, mid, n, 0, mid)))
                in (s1 + s2, b1 + b2) end
            val b = badIn t + badIn other
          in
            round (r - 1, t, next s, sum + csum, bad + db + cbad + b)
          end
    val (t, s, sum, bad) = round (rounds, t0, s, 0, 0)
    (* tiny pars let the runtime merge finished CCs back, so later local
     * collections copy out of chunks a CC kept *)
    fun tail (0, t, s) = (t, s)
      | tail (k, t, s) =
          let
            val _ = ForkJoin.par (fn () => churn (100, 0), fn () => churn (100, 0))
            val _ = ForkJoin.par (fn () => churn (100, 0), fn () => churn (100, 0))
            val (t, s) = mutate (t, s, steps div 2)
            val _ = churn (steps, 0)
          in tail (k - 1, t, next s) end
    val (t, _) = tail (3, t, s)
    val bad = bad + badIn t + badIn other
  in
    (* the sum only uses this node's own table; the parent's table is
     * written by both children, in disjoint halves, deterministically *)
    (sum + tagSum t, bad)
  end

val (sum, bad) = node (0, 0w12345, NONE)
val () = print ("procs " ^ Int.toString (MLton.Parallel.numberOfProcessors)
                ^ " mode " ^ mode ^ " " ^ feats ^ " sum " ^ Int.toString sum ^ " bad " ^ Int.toString bad ^ "\n")
val () = TextIO.output (TextIO.stdErr,
  "stats entanglements " ^ IntInf.toString (MPL.GC.numberEntanglements ())
  ^ " CCs " ^ IntInf.toString (MPL.GC.numCCs ())
  ^ " LGCs " ^ IntInf.toString (MPL.GC.numLocalGCs ()) ^ "\n")
