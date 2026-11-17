/* Copyright (C) 2012 Matthew Fluet.
 * Copyright (C) 1999-2008 Henry Cejtin, Matthew Fluet, Suresh
 *    Jagannathan, and Stephen Weeks.
 * Copyright (C) 1997-2000 NEC Research Institute.
 *
 * MLton is released under a HPND-style license.
 * See the file MLton-LICENSE for details.
 */

#if (defined (MLTON_GC_INTERNAL_TYPES))

#define GC_HEAP_LIMIT_SLOP 512
/* The largest request a limit check normally makes, slop included: the
 * compiler limits the allocation covered by one check to 512*7 bytes minus
 * the slop (maxBytesAlloc in mlton/backend/insert-checks.fun), though a single
 * very large statement can exceed it. GC_collectIntInf reduces an IntInf
 * primitive's request that doesn't fit in a block to this. */
#define GC_MAX_STATIC_BYTES_REQUESTED (512 * 7)

#endif /* (defined (MLTON_GC_INTERNAL_TYPES)) */


#if (defined (MLTON_GC_INTERNAL_FUNCS))

static inline bool isPointerInRootHeap(GC_state s, pointer p);
static inline bool isObjptrInRootHeap(GC_state s, objptr p);

#if ASSERT
static inline bool threadAndHeapOkay(GC_state s);
#endif

#endif /* (defined (MLTON_GC_INTERNAL_FUNCS)) */
