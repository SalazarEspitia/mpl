(* t3: more than 10,000 suspects at one join, then collection pressure while
 * the scheduler clears them in parallel (maybeParClearSuspectsAtDepth).
 *
 * A task at depth >= 1 allocates n mutable cells; its children, under one par
 * tree, store a freshly allocated value into every cell. Each such store is a
 * down-pointer, so the write barrier marks the cell as a suspect in the list
 * of the cells' depth; the join of the par tree's root clears that list (in
 * parallel when it holds more than 10,000 entries). After the join the task
 * allocates (post) and checks every cell.
 *
 * Arguments: variant n rounds post grain shape
 *   variant: plain  cells are refs
 *            half   as plain, and the children drop every odd cell from the
 *                   table after writing it (dead suspects)
 *            big    cells are arrays of 2500 words (single-object chunks)
 *   n       cells per round            (default 200000)
 *   rounds  joins                      (default 20)
 *   post    list cells allocated after each join (default 100000)
 *   grain   leaf size of the par tree  (default 1000)
 *   shape   tree  the children are a par tree over the cells (default)
 *           left  the left branch of one par writes every cell (after a spin
 *                 that gives the heartbeat time to promote the par), the right
 *                 branch does nothing. With one processor an unstolen right
 *                 branch runs after the join, at the parent's depth, so only
 *                 the left branch's stores make suspects; and the left branch
 *                 allocates a little more each round (r mod 61 * 512 bytes),
 *                 so the collector's trigger moves against the join.
 * Output: "t3 VARIANT n N rounds R sum S bad B"; S is deterministic, B must be 0. *)

val args = CommandLine.arguments ()
fun argi (i, d) = if i < List.length args then valOf (Int.fromString (List.nth (args, i))) else d
val variant = if null args then "plain" else hd args
val n = argi (1, 200000)
val rounds = argi (2, 20)
val post = argi (3, 100000)
val grain = argi (4, 1000)
val shape = if List.length args > 5 then List.nth (args, 5) else "tree"
val bigK = 2500

(* time without allocation, so the heartbeat can promote the enclosing par *)
fun spin (0, w: Word.word) = w
  | spin (k, w) = spin (k - 1, w * 0w1103515245 + 0w12345)

fun pad (0, acc) = acc
  | pad (k, acc) = pad (k - 1, List.length [k, acc] + acc - 2)   (* 48 bytes each *)

fun parTree0 (lo, hi, leaf: int * int -> unit) : unit =
  if hi - lo <= grain then leaf (lo, hi)
  else
    let val mid = lo + (hi - lo) div 2
    in ignore (ForkJoin.par (fn () => parTree0 (lo, mid, leaf), fn () => parTree0 (mid, hi, leaf))) end

val roundNo = ref 0
fun parTree (lo, hi, leaf: int * int -> unit) : unit =
  if shape = "left" then
    let val r = !roundNo before roundNo := !roundNo + 1
    in
      ignore (ForkJoin.par
        (fn () => (ignore (spin (2000000, 0w1)); leaf (lo, hi); ignore (pad ((r mod 61) * 11, 0))),
         fn () => ()))
    end
  else parTree0 (lo, hi, leaf)

(* allocation after the join: k list cells, some kept *)
fun churn (0, keep: int list list, acc) = acc + List.length keep
  | churn (k, keep, acc) =
      let val l = [k, k + 1, k + 2]
      in churn (k - 1, if k mod 64 = 0 then l :: keep else keep, acc + List.length l) end

fun okVal (r, i, l) = (case l of [a, b] => a = r andalso b = i | _ => false)

fun roundRefs (half, r) : int * int =
  let
    val cells = Array.tabulate (n, fn i => ref [i])
    val dummy = ref [~1]
    val _ = spin (2000000, 0w1)
    fun leaf (lo, hi) =
      let
        fun go i =
          if i >= hi then ()
          else
            ( Array.sub (cells, i) := [r, i]
            ; if half andalso i mod 2 = 1 then Array.update (cells, i, dummy) else ()
            ; go (i + 1) )
      in go lo end
    val _ = parTree (0, n, leaf)
    val c = churn (post, [], 0)
    fun check (i, s, b) =
      if i >= n then (s, b)
      else
        let val cell = Array.sub (cells, i)
        in
          if half andalso i mod 2 = 1 then check (i + 1, s + 1, if cell = dummy then b else b + 1)
          else check (i + 1, s + List.length (!cell), if okVal (r, i, !cell) then b else b + 1)
        end
    val (s, b) = check (0, 0, 0)
  in (s + c, b) end

fun roundBig r : int * int =
  let
    val cells = Array.tabulate (n, fn _ => Array.array (bigK, []: int list))
    val _ = spin (2000000, 0w1)
    val slot = r mod bigK
    fun leaf (lo, hi) =
      let fun go i = if i >= hi then () else (Array.update (Array.sub (cells, i), slot, [r, i]); go (i + 1))
      in go lo end
    val _ = parTree (0, n, leaf)
    val c = churn (post, [], 0)
    fun check (i, s, b) =
      if i >= n then (s, b)
      else
        let val l = Array.sub (Array.sub (cells, i), slot)
        in check (i + 1, s + List.length l, if okVal (r, i, l) then b else b + 1) end
    val (s, b) = check (0, 0, 0)
  in (s + c, b) end

fun loop (r, s, b) =
  if r >= rounds then (s, b)
  else
    let
      val (s', b') =
        case variant of
          "plain" => roundRefs (false, r)
        | "half" => roundRefs (true, r)
        | "big" => roundBig r
        | _ => raise Fail "variant: plain, half or big"
    in loop (r + 1, s + s', b + b') end

(* the rounds run in the left branch of a par, so the cells are at depth >= 1
 * once the heartbeat has promoted it (spin gives it the time) *)
val ((s, b), _) =
  ForkJoin.par (fn () => (ignore (spin (20000000, 0w1)); loop (0, 0, 0)), fn () => ())
val _ = print ("t3 " ^ variant ^ " n " ^ Int.toString n ^ " rounds " ^ Int.toString rounds
               ^ " sum " ^ Int.toString s ^ " bad " ^ Int.toString b ^ "\n")
