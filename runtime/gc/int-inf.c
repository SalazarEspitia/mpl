/* Copyright (C) 2012,2014,2016 Matthew Fluet.
 * Copyright (C) 1999-2005, 2007-2008 Henry Cejtin, Matthew Fluet,
 *    Suresh Jagannathan, and Stephen Weeks.
 * Copyright (C) 1997-2000 NEC Research Institute.
 *
 * MLton is released under a HPND-style license.
 * See the file MLton-LICENSE for details.
 */

/*
 * Test if a intInf is a fixnum.
 */
static inline bool isSmall (objptr arg) {
  return (arg & 1);
}

static inline bool areSmall (objptr arg1, objptr arg2) {
  return ((arg1 & arg2) & (objptr)1);
}

/*
 * Convert a bignum intInf to a bignum pointer.
 */
static inline GC_intInf toBignum (__attribute__((unused)) GC_state s,
                                  objptr arg)
{
  GC_intInf bp;

  assert (not isSmall(arg));
  bp = (GC_intInf)(objptrToPointer(arg, NULL)
                   - (offsetof(struct GC_intInf, obj)
                      + offsetof(struct GC_intInf_obj, isneg)));
  if (DEBUG_INT_INF)
    fprintf (stderr, "bp->header = "FMTHDR"\n", bp->header);
  assert (bp->header == GC_INTINF_HEADER);
  return bp;
}

/*
 * Given an intInf, a pointer to an __mpz_struct and space large
 * enough to contain LIMBS_PER_OBJPTR + 1 limbs, fill in the
 * __mpz_struct.
 */
void fillIntInfArg (GC_state s, objptr arg, __mpz_struct *res,
                    mp_limb_t space[LIMBS_PER_OBJPTR + 1]) {
  GC_intInf bp;

  if (DEBUG_INT_INF)
    fprintf (stderr, "fillIntInfArg ("FMTOBJPTR", "FMTPTR", "FMTPTR")\n",
             arg, (uintptr_t)res, (uintptr_t)space);
  if (isSmall(arg)) {
    res->_mp_alloc = LIMBS_PER_OBJPTR + 1;
    res->_mp_d = space;
    if (arg == (objptr)1) {
      res->_mp_size = 0;
    } else {
      const objptr highBitMask = (objptr)1 << (CHAR_BIT * OBJPTR_SIZE - 1);
      bool neg = (arg & highBitMask) != (objptr)0;
      if (neg) {
        arg = -((arg >> 1) | highBitMask);
      } else {
        arg = (arg >> 1);
      }
      int size;
      if (sizeof(objptr) <= sizeof(mp_limb_t)) {
        space[0] = (mp_limb_t)arg;
        size = 1;
      } else {
        size = 0;
        while (arg != 0) {
          space[size] = (mp_limb_t)arg;
          // The conditional below is to quell a gcc warning:
          //   right shift count >= width of type
          // When (sizeof(objptr) <= sizeof(mp_limb_t)),
          // this branch is unreachable,
          // so the shift doesn't matter.
          arg = arg >> (sizeof(objptr) <= sizeof(mp_limb_t) ?
                        0 : CHAR_BIT * sizeof(mp_limb_t));
          size++;
        }
      }
      if (neg)
        size = - size;
      res->_mp_size = size;
    }
  } else {
    bp = toBignum (s, arg);
    /* The _mp_alloc field is declared as int.
     * No possibility of an overflowing assignment, as all *huge*
     * intInfs must have come from some previous GnuMP evaluation.
     */
    res->_mp_alloc = (int)(bp->length - 1);
    res->_mp_d = (mp_limb_t*)(bp->obj.limbs);
    res->_mp_size = bp->obj.isneg ? - res->_mp_alloc : res->_mp_alloc;
  }
  assert ((res->_mp_size == 0)
          or (res->_mp_d[(res->_mp_size < 0
                          ? - res->_mp_size
                          : res->_mp_size) - 1] != 0));
  if (DEBUG_INT_INF_DETAILED)
    fprintf (stderr, "arg --> %s\n",
             mpz_get_str (NULL, 10, res));
}

/*
 * An IntInf primitive is called with `bytes`, an upper bound on the size of
 * its result, and normally builds the result in place at the frontier: the
 * limit check before the call made room for it. But `bytes` is known only at
 * run time and can exceed a block, while a chunk that holds several objects
 * may only have objects in its first block (see HM_getChunkOf), so GC_collect
 * does not make room for such a request. When the bound does not fit in the
 * room left, the result is built in a temporary buffer and then copied: to
 * the frontier if the actual result fits there after all, and otherwise into
 * a chunk of its own, like a large sequence.
 */
static inline bool resultFitsAtFrontier (GC_state s, size_t bytes) {
  return bytes <= (size_t)(s->limitPlusSlop - s->frontier);
}

static pointer resultSpace (GC_state s, size_t bytes) {
  if (resultFitsAtFrontier (s, bytes))
    return s->frontier;
  pointer buffer = malloc (bytes);
  if (NULL == buffer)
    DIE ("Out of memory. Unable to allocate %zu bytes for an IntInf result.", bytes);
  return buffer;
}

static inline bool isTemporaryResultSpace (GC_state s, pointer space) {
  return space != s->frontier;
}

/* Allocate a chunk holding exactly one object of objectSize bytes, in the
 * heap of the current chunk (where the result would otherwise have gone).
 * The mutator's current chunk and frontier are unchanged. */
static pointer allocateResultChunk (GC_state s, size_t objectSize) {
  GC_thread thread = getThreadCurrent(s);
  HM_chunk current = thread->currentChunk;
  assert (NULL != current);
  HM_HierarchicalHeap hh = HM_getLevelHead(current);

  HM_chunk chunk = HM_allocateChunkWithPurpose(
    HM_HH_getChunkList(hh), objectSize, BLOCK_FOR_HEAP_CHUNK);
  chunk->levelHead = HM_HH_getUFNode(hh);
  chunk->decheckState = current->decheckState;
  chunk->mightContainMultipleObjects = FALSE;
  HM_HH_addRecentBytesAllocated(thread, HM_getChunkSize(chunk));

  pointer start = HM_getChunkStart(chunk);
  HM_updateChunkFrontierInList(
    HM_HH_getChunkList(hh), chunk, alignFrontier (s, start + objectSize));
  return start;
}

/* Put a finished result object of objectSize bytes, built at space, into the
 * heap, and return where it now starts. */
static pointer commitResult (GC_state s, pointer space, size_t objectSize,
                             size_t bytes) {
  if (not isTemporaryResultSpace (s, space)) {
    setFrontier (s, space + objectSize, bytes);
    return space;
  }
  pointer dst;
  pointer end = alignFrontier (s, s->frontier + objectSize);
  if (end <= s->limitPlusSlop) {
    dst = s->frontier;
    memcpy (dst, space, objectSize);
    setFrontier (s, end, (size_t)(end - dst));
  } else {
    dst = allocateResultChunk (s, objectSize);
    memcpy (dst, space, objectSize);
  }
  free (space);
  return dst;
}

/* Discard the space of a result that turned out to need no heap object. */
static inline void discardResultSpace (GC_state s, pointer space) {
  if (isTemporaryResultSpace (s, space))
    free (space);
}

/*
 * Initialize an __mpz_struct to use the space for the result (see above).
 */
void initIntInfRes (GC_state s, __mpz_struct *res, size_t bytes) {
  GC_intInf bp;
  size_t nlimbs;

  bp = (GC_intInf)resultSpace (s, bytes);
  /* We have as much space for the limbs as there is to the end of the
   * heap (or of the temporary buffer).  Divide by (sizeof(mp_limb_t)) to get
   * number of limbs.
   */
  pointer end = isTemporaryResultSpace (s, (pointer)bp)
                ? (pointer)bp + bytes
                : s->limitPlusSlop;
  nlimbs = ((size_t)(end - (pointer)bp->obj.limbs)) / (sizeof(mp_limb_t));
  /* The _mp_alloc field is declared as int.
   * Avoid an overflowing assignment, which could happen with huge
   * heaps.
   */
  res->_mp_alloc = (int)(min(nlimbs,(size_t)INT_MAX));
  res->_mp_d = (mp_limb_t*)(bp->obj.limbs);
  res->_mp_size = 0; /* is this necessary? */
}

/*
 * Given an __mpz_struct pointer which reflects the answer, set
 * gcState.frontier and return the answer.
 * If the answer fits in a fixnum, we return that, with the frontier
 * rolled back.
 * If the answer doesn't need all of the space allocated, we adjust
 * the sequence size and roll the frontier slightly back.
 */
objptr finiIntInfRes (GC_state s, __mpz_struct *res, size_t bytes) {
  GC_intInf bp;
  int size;

  assert ((res->_mp_size == 0)
          or (res->_mp_d[(res->_mp_size < 0
                          ? - res->_mp_size
                          : res->_mp_size) - 1] != 0));
  if (DEBUG_INT_INF)
    fprintf (stderr, "finiIntInfRes ("FMTPTR", %"PRIuMAX")\n",
             (uintptr_t)res, (uintmax_t)bytes);
  if (DEBUG_INT_INF_DETAILED)
    fprintf (stderr, "res --> %s\n",
             mpz_get_str (NULL, 10, res));
  bp = (GC_intInf)((pointer)res->_mp_d
                   - (offsetof(struct GC_intInf, obj)
                      + offsetof(struct GC_intInf_obj, limbs)));
  assert (res->_mp_d == (mp_limb_t*)(bp->obj.limbs));
  size = res->_mp_size;
  if (size < 0) {
    bp->obj.isneg = TRUE;
    size = - size;
  } else
    bp->obj.isneg = FALSE;
  assert (size >= 0);
  if (size == 0) {
    discardResultSpace (s, (pointer)bp);
    return (objptr)1;
  }
  if (size <= LIMBS_PER_OBJPTR) {
    if (sizeof(objptr) <= sizeof(mp_limb_t)) {
      objptr ans;
      mp_limb_t val = bp->obj.limbs[0];
      if (bp->obj.isneg) {
        /*
         * We only fit if val in [1, 2^(CHAR_BIT * OBJPTR_SIZE - 2)].
         */
        ans = (objptr)(- val);
        val = val - 1;
      } else
        /*
         * We only fit if val in [0, 2^(CHAR_BIT * OBJPTR_SIZE - 2) - 1].
         */
        ans = (objptr)val;
      // The conditional below is to quell a gcc warning:
      //   right shift count >= width of type
      // When (sizeof(objptr) > sizeof(mp_limb_t)),
      // this branch is unreachable,
      // so the shift doesn't matter.
      if (val < (mp_limb_t)1<<(sizeof(objptr) > sizeof(mp_limb_t) ?
                               0 : CHAR_BIT * OBJPTR_SIZE - 2))
        {
          discardResultSpace (s, (pointer)bp);
          return (ans<<1 | 1);
        }
    } else {
      objptr ans, val;
      val = (objptr)(bp->obj.limbs[0]);
      for (int i = 1; i < size; i++) {
        // The conditional below is to quell a gcc warning:
        //   left shift count >= width of type
        // When (sizeof(objptr) <= sizeof(mp_limb_t)),
        // this branch is unreachable,
        // so the shift doesn't matter.
        val = val << (sizeof(objptr) <= sizeof(mp_limb_t) ?
                      0 : CHAR_BIT * sizeof(mp_limb_t));
        val = val & (objptr)(bp->obj.limbs[i]);
      }
      if (bp->obj.isneg) {
        /*
         * We only fit if val in [1, 2^(CHAR_BIT * OBJPTR_SIZE - 2)].
         */
        ans = - val;
        val = val - 1;
      } else
        /*
         * We only fit if val in [0, 2^(CHAR_BIT * OBJPTR_SIZE - 2) - 1].
         */
        ans = val;
      if (val < (objptr)1<<(CHAR_BIT * OBJPTR_SIZE - 2)) {
        discardResultSpace (s, (pointer)bp);
        return (ans<<1 | 1);
      }
    }
  }
  bp->counter = (GC_sequenceCounter)0;
  bp->length = (GC_sequenceLength)(size + 1); /* +1 for isneg field */
  bp->header = GC_INTINF_HEADER;
  bp = (GC_intInf)commitResult (s, (pointer)bp,
                                (size_t)((pointer)(&bp->obj.limbs[size]) - (pointer)bp),
                                bytes);
  // bp->fwdptr = BOGUS_OBJPTR;
  return pointerToObjptr ((pointer)&bp->obj, NULL);
}

objptr IntInf_binop (GC_state s,
                     objptr lhs, objptr rhs, size_t bytes,
                     void(*binop)(__mpz_struct *resmpz,
                                  const __mpz_struct *lhsspace,
                                  const __mpz_struct *rhsspace)) {
  __mpz_struct lhsmpz, rhsmpz, resmpz;
  mp_limb_t lhsspace[LIMBS_PER_OBJPTR + 1], rhsspace[LIMBS_PER_OBJPTR + 1];

  if (DEBUG_INT_INF)
    fprintf (stderr, "IntInf_binop ("FMTOBJPTR", "FMTOBJPTR", %"PRIuMAX")\n",
             lhs, rhs, (uintmax_t)bytes);
  initIntInfRes (s, &resmpz, bytes);
  fillIntInfArg (s, lhs, &lhsmpz, lhsspace);
  fillIntInfArg (s, rhs, &rhsmpz, rhsspace);
  binop (&resmpz, &lhsmpz, &rhsmpz);
  return finiIntInfRes (s, &resmpz, bytes);
}

objptr IntInf_unop (GC_state s,
                    objptr arg, size_t bytes,
                    void(*unop)(__mpz_struct *resmpz,
                                const __mpz_struct *argspace)) {
  __mpz_struct argmpz, resmpz;
 mp_limb_t argspace[LIMBS_PER_OBJPTR + 1];

  if (DEBUG_INT_INF)
    fprintf (stderr, "IntInf_unop ("FMTOBJPTR", %"PRIuMAX")\n",
             arg, (uintmax_t)bytes);

 initIntInfRes (s, &resmpz, bytes);
 fillIntInfArg (s, arg, &argmpz, argspace);
 unop (&resmpz, &argmpz);
 return finiIntInfRes (s, &resmpz, bytes);
}

objptr IntInf_shop (GC_state s,
                    objptr arg, Word32_t shift, size_t bytes,
                    void(*shop)(__mpz_struct *resmpz,
                                const __mpz_struct *argspace,
                                unsigned long shift))
{
  __mpz_struct argmpz, resmpz;
  mp_limb_t argspace[LIMBS_PER_OBJPTR + 1];

  if (DEBUG_INT_INF)
    fprintf (stderr, "IntInf_shop ("FMTOBJPTR", %"PRIu32", %"PRIuMAX")\n",
             arg, shift, (uintmax_t)bytes);

  initIntInfRes (s, &resmpz, bytes);
  fillIntInfArg (s, arg, &argmpz, argspace);
  shop (&resmpz, &argmpz, (unsigned long)shift);
  return finiIntInfRes (s, &resmpz, bytes);
}

Int32_t IntInf_cmpop (GC_state s, objptr lhs, objptr rhs,
                      int(*cmpop)(const __mpz_struct *lhsspace,
                                  const __mpz_struct *rhsspace))
{
  __mpz_struct lhsmpz, rhsmpz;
  mp_limb_t lhsspace[LIMBS_PER_OBJPTR + 1], rhsspace[LIMBS_PER_OBJPTR + 1];
  int res;

  if (DEBUG_INT_INF)
    fprintf (stderr, "IntInf_cmpop ("FMTOBJPTR", "FMTOBJPTR")\n",
             lhs, rhs);
  fillIntInfArg (s, lhs, &lhsmpz, lhsspace);
  fillIntInfArg (s, rhs, &rhsmpz, rhsspace);
  res = cmpop (&lhsmpz, &rhsmpz);
  if (res < 0) return -1;
  if (res > 0) return 1;
  return 0;
}

objptr IntInf_strop (GC_state s, objptr arg, Int32_t base, size_t bytes,
                     char*(*strop)(char *str,
                                   int base,
                                   const __mpz_struct *argspace))
{
  GC_string8 sp;
  __mpz_struct argmpz;
  mp_limb_t argspace[LIMBS_PER_OBJPTR + 1];
  char *str;
  size_t size;

  if (DEBUG_INT_INF)
    fprintf (stderr, "IntInf_strop ("FMTOBJPTR", %"PRId32", %"PRIuMAX")\n",
             arg, base, (uintmax_t)bytes);
  assert (base == 2 || base == 8 || base == 10 || base == 16);
  fillIntInfArg (s, arg, &argmpz, argspace);
  sp = (GC_string8)resultSpace (s, bytes);
  str = strop ((void*)&sp->obj, -base, &argmpz);
  assert (str == (char*)&sp->obj);
  size = strlen(str);
  if (sp->obj.chars[0] == '-')
    sp->obj.chars[0] = '~';
  sp->counter = (GC_sequenceCounter)0;
  sp->length = (GC_sequenceLength)size;
  sp->header = GC_STRING8_HEADER;
  sp = (GC_string8)commitResult (s, (pointer)sp,
                                 (size_t)(((pointer)&sp->obj + size) - (pointer)sp),
                                 bytes);
  // sp->fwdptr = BOGUS_OBJPTR;
  return pointerToObjptr ((pointer)&sp->obj, NULL);
}

/*
static GC_state intInfMemoryFuncsState;

static void * wrap_alloc_func(size_t size) {
  if (DEBUG_INT_INF)
    fprintf (stderr, "alloc_func (size = %"PRIuMAX") = ",
             (uintmax_t)size);
  void * res = (*alloc_func_ptr)(size);
  if (DEBUG_INT_INF)
    fprintf (stderr, FMTPTR"\n", (uintptr_t)res);
  return res;
}

static void * wrap_realloc_func(void *ptr, size_t old_size, size_t new_size) {
  if (DEBUG_INT_INF)
    fprintf (stderr, "realloc_func (ptr = "FMTPTR", "
             "old_size = %"PRIuMAX", new_size = %"PRIuMAX") = ",
             (uintptr_t)ptr, (uintmax_t)old_size, (uintmax_t)new_size);
  assert (! isPointerInHeap(intInfMemoryFuncsState, (pointer)ptr));
  void * res = (*realloc_func_ptr)(ptr, old_size, new_size);
  if (DEBUG_INT_INF)
    fprintf (stderr, FMTPTR"\n", (uintptr_t)res);
  return res;
}

static void wrap_free_func(void *ptr, size_t size) {
  if (DEBUG_INT_INF)
    fprintf (stderr, "free_func (ptr = "FMTPTR", size = %"PRIuMAX")",
             (uintptr_t)ptr, (uintmax_t)size);
  assert (! isPointerInHeap(intInfMemoryFuncsState, (pointer)ptr));
  (*free_func_ptr)(ptr, size);
  if (DEBUG_INT_INF)
    fprintf (stderr, "\n");
  return;
}

void initIntInf (GC_state s) {
  intInfMemoryFuncsState = s;
  mp_get_memory_functions (&alloc_func_ptr, &realloc_func_ptr, &free_func_ptr);
  mp_set_memory_functions (&wrap_alloc_func, &wrap_realloc_func, &wrap_free_func);
  return;
}
*/

void initIntInf (__attribute__ ((unused)) GC_state s) {
  return;
}
