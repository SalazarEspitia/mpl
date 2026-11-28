(* refFlatten and compareAndSwap: a CAS reads and writes a pointer field, so
 * the contents of a CAS'd ref (or array element) must stay unflattened, and the
 * expected and new values must share the contents' representation.
 * Each case used to fail the SSA2 type check after refFlatten.
 *)

fun show l = print (String.concatWith " " (List.map Int.toString l) ^ "\n")

(* the inner ref is read after the CAS *)
val r = ref (ref 0)
val old = MLton.Parallel.compareAndSwap r (!r, ref 1)
val () = show [!old, ! (!r)]

(* the expected value isn't read from the ref: the CAS fails *)
val r = ref (ref 0)
val old = MLton.Parallel.compareAndSwap r (ref 5, ref 1)
val () = show [!old, ! (!r)]

(* three levels: only the contents' inner ref is read elsewhere *)
val z = ref 0
val r = ref (ref z)
val old = MLton.Parallel.compareAndSwap r (!r, ref (ref 1))
val () = show [! (!old), ! (! (!r)), !z]

(* three levels: only the new value's inner ref is read elsewhere *)
val z = ref 1
val r = ref (ref (ref 0))
val old = MLton.Parallel.compareAndSwap r (!r, ref z)
val () = show [! (!old), ! (! (!r)), !z]

(* a ref that is an array element *)
val a = Array.tabulate (2, fn i => ref (ref i))
val e = Array.sub (a, 0)
val old = MLton.Parallel.compareAndSwap e (!e, ref 7)
val () = show [!old, ! (!e), ! (! (Array.sub (a, 1)))]

(* a ref holding a tuple with a ref in it *)
val x = ref 0
val r = ref (x, 1)
val old = MLton.Parallel.compareAndSwap r (!r, (ref 5, 2))
val () = show [! (#1 old), #2 old, ! (#1 (!r)), #2 (!r), !x]

(* Array_cas on an 'a ref ref array *)
val z = ref 0
val a = Array.array (1, ref z)
val old = MLton.Parallel.arrayCompareAndSwap (a, 0) (Array.sub (a, 0), ref (ref 1))
val () = show [! (!old), ! (! (Array.sub (a, 0))), !z]

(* Array_cas whose expected value isn't read from the array: the CAS fails *)
val a = Array.array (1, ref (ref 0))
val old = MLton.Parallel.arrayCompareAndSwap (a, 0) (ref (ref 5), ref (ref 1))
val () = show [! (!old), ! (! (Array.sub (a, 0)))]

(* a retry loop comparing the result with MLton.eq *)
val cell = ref (ref 0)
fun incr () =
  let val cur = !cell
      val nxt = ref (!cur + 1)
  in if MLton.eq (MLton.Parallel.compareAndSwap cell (cur, nxt), cur) then () else incr ()
  end
fun loop 0 = () | loop k = (incr (); loop (k - 1))
val () = loop 10
val () = show [! (!cell)]

(* inside a function *)
fun go n =
  let val r = ref (ref n)
      val old = MLton.Parallel.compareAndSwap r (!r, ref (n + 1))
  in [!old, ! (!r)]
  end
val () = show (go (valOf (Int.fromString "3")))
