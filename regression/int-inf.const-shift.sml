(* IntInf operations with constant arguments, whose results are too large
 * to be folded into constants.  Their bytesNeeded arguments are folded, so
 * the C calls allocate a statically known amount; above 3,072 bytes (from
 * a shift of 378 limbs on 64-bit targets) the compiler used to fail with
 * "LimitCheck.restrictAllocInBlock: groups empty".  The amounts here fit in
 * one heap block of 4K.
 *)
val () = print (Int.toString (IntInf.log2 (IntInf.<< (1, 0w24191))) ^ "\n")
val () = print (Int.toString (IntInf.log2 (IntInf.<< (1, 0w24192))) ^ "\n")
val () = print (Int.toString (IntInf.log2 (IntInf.<< (1, 0w24223))) ^ "\n")
val () = print (Int.toString (IntInf.log2 (IntInf.<< (1, 0w25600))) ^ "\n")
