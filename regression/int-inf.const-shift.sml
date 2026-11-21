(* An IntInf operation with constant arguments, whose result is too large to
 * be folded into a constant.  With a single call site, IntInf.<< is inlined
 * and its bytesNeeded argument is folded, so the C call allocates a
 * statically known amount: 3,079 bytes here, over the 3,072 that one
 * statically sized limit check may cover.  The compiler used to fail with
 * "LimitCheck.restrictAllocInBlock: groups empty".  The amount fits in one
 * heap block of 4K.  (With four call sites the program compiled even without
 * the fix, presumably because << was not inlined.)
 *)
val () = print (Int.toString (IntInf.log2 (IntInf.<< (1, 0w24223))) ^ "\n")
