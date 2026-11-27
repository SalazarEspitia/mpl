/* Copyright (C) 2020 Lawrence Wang, Sam Westrick
 *
 * MLton is released under a HPND-style license.
 * See the file MLton-LICENSE for details.
 */

// #define MAX(x, y) ((x) > (y) ? (x) : (y))

#define MAX_FORK_DEPTH 31
#define MAX_PATHS ((unsigned int) 1 << (MAX_FORK_DEPTH))
#define MAX_DEPTH (1 << 27)

#define SYNCH_DEPTHS_BASE ((void *) 0x100000000000)
#define SYNCH_DEPTHS_LEN (sizeof(uint32_t) * (MAX_PATHS))

#ifdef DETECT_ENTANGLEMENT
#if ASSERT
static uint32_t *synch_depths = SYNCH_DEPTHS_BASE;
#endif
#endif


Bool GC_HH_decheckMaxDepth(ARG_USED_FOR_DETECT_ENTANGLEMENT objptr resultRef) {
#ifdef DETECT_ENTANGLEMENT
  uint32_t *r = (uint32_t*)objptrToPointer(resultRef, NULL);
  *r = MAX_FORK_DEPTH;
  return TRUE;
#else
  return FALSE;
#endif
}


#ifdef DETECT_ENTANGLEMENT
void decheckInit(GC_state s) {
#if ASSERT
  /* Anonymous mappings are zero-filled, so the table needs no memset.
   * (A memset here would touch all SYNCH_DEPTHS_LEN bytes, 8 GB with the
   * default MAX_FORK_DEPTH, and make every debug run that large.) */
  if (mmap(SYNCH_DEPTHS_BASE, SYNCH_DEPTHS_LEN, PROT_READ | PROT_WRITE,
          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0, 0) == MAP_FAILED) {
      perror("mmap error");
      exit(-1);
  }
  synch_depths[1] = 0;
#endif

  GC_thread thread = getThreadCurrent(s);
  thread->decheckState.internal.path = 1;
  thread->decheckState.internal.depth = 0;

  // LOG(LM_THREAD, LL_FORCE,
  //   "thread %p got decheckState %lu",
  //   (void*)thread,
  //   thread->decheckState.bits);
}
#else
inline void decheckInit(GC_state s) {
  (void)s;
  return;
}
#endif

static inline unsigned int tree_depth(decheck_tid_t tid) {
  return tid.internal.depth & 0x1f;
}

static inline unsigned int dag_depth(decheck_tid_t tid) {
  return tid.internal.depth >> 5;
}

static inline uint32_t norm_path(decheck_tid_t tid) {
  unsigned int td = tree_depth(tid);
  return tid.internal.path & ((1 << (td+1)) - 1);
}


#ifdef DETECT_ENTANGLEMENT
static inline void decheckSetSyncDepth(GC_thread thread, uint32_t pathLen, uint32_t syncDepth) {
  if (pathLen >= DECHECK_DEPTHS_LEN) {
    DIE("attempted to set syncDepths[%"PRIu32"] := %"PRIu32, pathLen, syncDepth);
  }
  thread->decheckSyncDepths[pathLen] = syncDepth;
}
static inline uint32_t decheckGetSyncDepth(GC_thread thread, uint32_t pathLen) {
  if (pathLen >= DECHECK_DEPTHS_LEN) {
    DIE("attempted to get syncDepths[%"PRIu32"]", pathLen);
  }
  return thread->decheckSyncDepths[pathLen];
}
#endif


#ifdef DETECT_ENTANGLEMENT
/* SAM_NOTE: TODO: implement this in SML and avoid needing to allocate
 * refs just to pass values by destination-passing through the FFI.
 */
void GC_HH_decheckFork(GC_state s, uint64_t *left, uint64_t *right) {
  GC_thread thread = getThreadCurrent(s);
  decheck_tid_t tid = thread->decheckState;
  assert(tid.bits != DECHECK_BOGUS_BITS);
  unsigned int h = tree_depth(tid);
  assert(h < MAX_FORK_DEPTH);

  decheck_tid_t t1;
  t1.internal.path = (tid.internal.path & ~(1 << h)) | (1 << (h+1));
  t1.internal.depth = tid.internal.depth + (1 << 5) + 1;
  *left = t1.bits;

  decheck_tid_t t2;
  t2.internal.path = (tid.internal.path | (1 << h)) | (1 << (h+1));
  t2.internal.depth = tid.internal.depth + (1 << 5) + 1;
  *right = t2.bits;

  assert(tree_depth(t1) == tree_depth(tid)+1);
  assert(tree_depth(t2) == tree_depth(tid)+1);
  assert(dag_depth(t1) == dag_depth(tid)+1);
  assert(dag_depth(t2) == dag_depth(tid)+1);
  assert((norm_path(t1) ^ norm_path(t2)) == (uint32_t)(1 << h));

#if ASSERT
  synch_depths[norm_path(t1)] = dag_depth(t1);
  synch_depths[norm_path(t2)] = dag_depth(t2);
#endif
}
#else
void GC_HH_decheckFork(GC_state s, uint64_t *left, uint64_t *right) {
  (void)s;
  (void)left;
  (void)right;
  return;
}
#endif


#ifdef DETECT_ENTANGLEMENT

void setStateIfBogus(HM_chunk chunk, decheck_tid_t tid) {
  if ((chunk->decheckState).bits == DECHECK_BOGUS_BITS)
  {
    chunk->decheckState = tid;
  }
}

void GC_HH_decheckSetTid(GC_state s, uint64_t bits) {
  decheck_tid_t tid;
  tid.bits = bits;

  GC_thread thread = getThreadCurrent(s);
  thread->decheckState = tid;

  // setStateIfBogus(HM_getChunkOf((pointer)thread), tid);
  // setStateIfBogus(HM_getChunkOf((pointer)thread->stack), tid);

  decheckSetSyncDepth(thread, tree_depth(tid), dag_depth(tid));

  assert(decheckGetSyncDepth(thread, tree_depth(tid)) == synch_depths[norm_path(tid)]);
}
#else
void GC_HH_decheckSetTid(GC_state s, uint64_t bits) {
  (void)s;
  (void)bits;
}
#endif


#ifdef DETECT_ENTANGLEMENT
uint64_t GC_HH_decheckGetTid(GC_state s, objptr threadp) {
  GC_thread thread = threadObjptrToStruct(s, threadp);
  return thread->decheckState.bits;
}
#else
uint64_t GC_HH_decheckGetTid(GC_state s, objptr threadp) {
  (void)s;
  (void)threadp;
  return DECHECK_BOGUS_BITS;
}
#endif


#ifdef DETECT_ENTANGLEMENT
void GC_HH_decheckJoin(GC_state s, uint64_t left, uint64_t right) {
  decheck_tid_t t1;
  t1.bits = left;
  decheck_tid_t t2;
  t2.bits = right;

  assert(tree_depth(t1) == tree_depth(t2));
  assert(tree_depth(t1) >= 1);

  GC_thread thread = getThreadCurrent(s);
  unsigned int td = tree_depth(t1) - 1;
  unsigned int dd = MAX(dag_depth(t1), dag_depth(t2)) + 1;
  assert(dag_depth(t1) == synch_depths[norm_path(t1)]);
  assert(dag_depth(t2) == synch_depths[norm_path(t2)]);
  decheck_tid_t tid;
  tid.internal.path = t1.internal.path | (1 << td);
  tid.internal.depth = (dd << 5) + td;
  thread->decheckState = tid;

  assert(tree_depth(tid) == tree_depth(t1)-1);

#if ASSERT
  synch_depths[norm_path(tid)] = dd;
#endif
  decheckSetSyncDepth(thread, tree_depth(tid), dd);

  assert(decheckGetSyncDepth(thread, tree_depth(tid)) == synch_depths[norm_path(tid)]);
}
#else
void GC_HH_decheckJoin(GC_state s, uint64_t left, uint64_t right) {
  (void)s;
  (void)left;
  (void)right;
  return;
}
#endif


#if ASSERT

static inline int msbPosition(uint32_t x) {
  assert(x != 0);
  int i = 0;
  while (x != 1) {
    x = x >> 1;
    i++;
  }
  return i;
}

static inline int lcaLen(uint32_t p1, uint32_t p2) {
  int i = 0;
  int p1len = msbPosition(p1);
  int p2len = msbPosition(p2);
  int mlen = (p1len < p2len) ? p1len : p2len;
  while (i < mlen && ((p1 & 1) == (p2 & 1))) {
    p1 = p1 >> 1;
    p2 = p2 >> 1;
    i++;
  }
  return i;
}

#endif

/* Very fancy bit hack. Finds the index of one set bit. From
 *   https://graphics.stanford.edu/~seander/bithacks.html#IntegerLogDeBruijn
 * Based on info from the paper:
 *   Using de Bruijn Sequences to Index 1 in a Computer Word
 *   by Charles E. Leiserson, Harald Prokof, and Keith H. Randall.
 */
// static const int DeBruijnBitPosition[32] =
// {
//   0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
//   31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
// };
// static inline int bitIndex(uint32_t v) {
//   return DeBruijnBitPosition[(v * 0x077CB531U) >> 27];
// }

// static inline int bitIndex(uint32_t x) {
//   return 31 - __builtin_clz(x);
// }

static inline int bitIndex(uint32_t x) {
  return __builtin_ctz(x);
}

/** The heap depth of the LCA. Recall that heap depths are off-by-one; the
  * "root" of the hierarchy is at depth 1.
  */
int lcaHeapDepth(decheck_tid_t t1, decheck_tid_t t2)
{
  /** This code is copied from isOrdered... */
  uint32_t p1 = norm_path(t1);
  uint32_t p1mask = (1 << tree_depth(t1)) - 1;
  uint32_t p2 = norm_path(t2);
  uint32_t p2mask = (1 << tree_depth(t2)) - 1;
  uint32_t shared_mask = p1mask & p2mask;
  uint32_t shared_upper_bit = shared_mask+1;
  uint32_t x = ((p1 ^ p2) & shared_mask) | shared_upper_bit;
  uint32_t lca_bit = x & -x;
  // uint32_t lca_mask = lca_bit-1;
  int llen = bitIndex(lca_bit);
  if (p1 == p2) {
    return tree_depth(t1) + 1;
  }
  assert(llen == lcaLen(p1, p2));
  return llen+1;
}


#ifdef DETECT_ENTANGLEMENT
bool decheckIsOrdered(GC_thread thread, decheck_tid_t t1)
{
  uint32_t p1 = norm_path(t1);
  uint32_t p1mask = (1 << tree_depth(t1)) - 1;
  uint32_t p2 = norm_path(thread->decheckState);
  uint32_t p2mask = (1 << tree_depth(thread->decheckState)) - 1;

  if (p1 == p2)
    return TRUE;

  uint32_t shared_mask = p1mask & p2mask;
  uint32_t shared_upper_bit = shared_mask+1;

  uint32_t x = ((p1 ^ p2) & shared_mask) | shared_upper_bit;
  uint32_t lca_bit = x & -x;
  uint32_t lca_mask = lca_bit-1;
  int llen = bitIndex(lca_bit);
  assert(llen == lcaLen(p1, p2));

  uint32_t lca_path = (p1 & lca_mask) | (lca_mask + 1);
  assert(lca_path < MAX_PATHS);
  assert(decheckGetSyncDepth(thread, llen) == synch_depths[lca_path]);
  return p1 == lca_path || dag_depth(t1) <= decheckGetSyncDepth(thread, llen);
}
#else
bool decheckIsOrdered(GC_thread thread, decheck_tid_t t1) {
  (void)thread;
  (void)t1;
  return TRUE;
}
#endif

#ifdef DETECT_ENTANGLEMENT

/* An explicit stack of objects still to visit, for the traversals below
 * (make_entangled, and traverseAndCheck in the debug runtime): a recursive
 * traversal of a long immutable structure, e.g. a list, overflows a worker
 * thread's C stack, one call per pointer hop. The stack lives on the C stack
 * while it's small, else on the C heap.
 *
 * A frame is an object to visit, or an object being expanded: its fields are
 * visited one at a time, in the order foreachObjptrInObject visits them, from
 * cur up to end. So the visits are in the recursion's order, and the stack
 * holds one frame per object on the current path, as the recursion did (a
 * stack object's fields are the exception: they are pushed all at once).
 */
struct ME_frame {
  objptr *opp;          /* where the pointer to the object is */
  objptr ptr;           /* expanded: the object, as first read */
  pointer cur;          /* expanded: the next field to visit */
  pointer end;          /* expanded: just past the last field */
  uint32_t unpinDepth;  /* the unpin depth the object must have */
  uint32_t childDepth;  /* expanded: the unpin depth its fields must have */
  uint16_t bytesNonObjptrs; /* expanded: its layout (normal and weak objects, */
  uint16_t numObjptrs;      /* and each sequence element) */
  uint16_t fieldIndex;  /* expanded: cur's index among its element's objptrs */
  bool expanded;        /* immutable: its fields are being visited */
  bool addToRemSet;     /* the root of this manage_entangled call */
};

#define ME_INLINE_FRAMES 64

struct ME_stack {
  struct ME_frame *frames;
  size_t size;
  size_t capacity;
  struct ME_frame inlineFrames[ME_INLINE_FRAMES];
};

static void ME_init(struct ME_stack *st) {
  st->frames = st->inlineFrames;
  st->size = 0;
  st->capacity = ME_INLINE_FRAMES;
}

static void ME_free(struct ME_stack *st) {
  if (st->frames != st->inlineFrames)
    free(st->frames);
}

static void ME_push(struct ME_stack *st, struct ME_frame f) {
  if (st->size == st->capacity) {
    size_t newCapacity = 2 * st->capacity;
    struct ME_frame *newFrames =
      (struct ME_frame *) malloc_safe(newCapacity * sizeof(struct ME_frame));
    memcpy(newFrames, st->frames, st->size * sizeof(struct ME_frame));
    if (st->frames != st->inlineFrames)
      free(st->frames);
    st->frames = newFrames;
    st->capacity = newCapacity;
  }
  st->frames[st->size++] = f;
}

static void ME_pushVisit(struct ME_stack *st, objptr *opp, uint32_t unpinDepth) {
  struct ME_frame f =
    {.opp = opp, .ptr = BOGUS_OBJPTR, .cur = NULL, .end = NULL,
     .unpinDepth = unpinDepth, .childDepth = 0,
     .bytesNonObjptrs = 0, .numObjptrs = 0, .fieldIndex = 0,
     .expanded = false, .addToRemSet = false};
  ME_push(st, f);
}

struct ME_pushArgs {
  struct ME_stack *stack;
  uint32_t unpinDepth;
};

static void ME_pushField(
  __attribute__((unused)) GC_state s,
  objptr *opp,
  __attribute__((unused)) objptr op,
  void *rawArgs)
{
  struct ME_pushArgs *args = (struct ME_pushArgs *) rawArgs;
  ME_pushVisit(args->stack, opp, args->unpinDepth);
}

/* Push f, the object at p, as expanded: its fields are to be visited at
 * childDepth. Normal and weak objects and sequences are stepped through one
 * field at a time (ME_nextField), as foreachObjptrInObject walks them. A
 * stack's fields are all pushed now, in reverse, so they are visited in
 * foreachObjptrInObject's order too. */
static void ME_pushExpanded(
  GC_state s,
  struct ME_stack *st,
  struct ME_frame f,
  pointer p,
  uint32_t childDepth)
{
  GC_objectTypeTag tag;
  uint16_t bytesNonObjptrs, numObjptrs;
  splitHeader(s, getRacyHeader(p), &tag, NULL, &bytesNonObjptrs, &numObjptrs);

  f.expanded = true;
  f.childDepth = childDepth;
  f.bytesNonObjptrs = bytesNonObjptrs;
  f.numObjptrs = numObjptrs;
  f.fieldIndex = 0;
  f.cur = p + bytesNonObjptrs;
  if (NORMAL_TAG == tag || WEAK_TAG == tag) {
    f.end = f.cur + numObjptrs * OBJPTR_SIZE;
  }
  else if (SEQUENCE_TAG == tag && numObjptrs > 0) {
    size_t bytesPerElement = bytesNonObjptrs + numObjptrs * OBJPTR_SIZE;
    f.end = p + getSequenceLength(p) * bytesPerElement;
  }
  else {
    f.end = f.cur;  /* nothing to step through */
  }
  ME_push(st, f);

  /* Not expected: a stack is reached only through its thread, which has
   * identity, so the traversal stops there. Kept for what
   * foreachObjptrInObject would have visited. */
  if (STACK_TAG == tag) {
    size_t first = st->size;
    struct ME_pushArgs args = {.stack = st, .unpinDepth = childDepth};
    struct GC_foreachObjptrClosure pushClosure =
      {.fun = ME_pushField, .env = &args};
    foreachObjptrInObject(s, p, &trueObjptrPredicateClosure, &pushClosure, FALSE);
    for (size_t i = first, j = st->size - 1; i < j; i++, j--) {
      struct ME_frame t = st->frames[i];
      st->frames[i] = st->frames[j];
      st->frames[j] = t;
    }
  }
}

/* The next field of an expanded frame that holds an objptr, or NULL when
 * there are no more. */
static objptr *ME_nextField(struct ME_frame *f) {
  while (f->cur < f->end) {
    objptr *opp = (objptr *) f->cur;
    if (++(f->fieldIndex) < f->numObjptrs) {
      f->cur += OBJPTR_SIZE;
    }
    else {
      /* on to the next sequence element's objptrs */
      f->fieldIndex = 0;
      f->cur += OBJPTR_SIZE + f->bytesNonObjptrs;
    }
    if (isObjptr(*opp))
      return opp;
  }
  return NULL;
}

#if ASSERT
void traverseAndCheck(
  GC_state s,
  objptr *opp,
  __attribute__((unused)) objptr op,
  void *rawArgs)
{
  struct ME_stack st;
  ME_init(&st);
  ME_pushVisit(&st, opp, *(uint32_t*)rawArgs);

  while (st.size > 0) {
    struct ME_frame *top = &(st.frames[st.size - 1]);
    if (top->expanded) {
      objptr *fieldp = ME_nextField(top);
      if (NULL == fieldp)
        st.size--;
      else
        ME_pushVisit(&st, fieldp, top->childDepth);
      continue;
    }

    struct ME_frame f = st.frames[--st.size];
    objptr obj = *(f.opp);
    GC_header header = getHeader(objptrToPointer(obj, NULL));
    pointer p = objptrToPointer (obj, NULL);
    assert (pinType(header) == PIN_ANY);
    assert (!isFwdHeader(header));

    uint32_t requiredUnpinDepth = f.unpinDepth;
    uint32_t actualUnpinDepth = unpinDepthOfH(header);
    uint32_t currentDepth = HM_getObjptrDepth(obj);

    assert (actualUnpinDepth <= requiredUnpinDepth);

    if (currentDepth <= requiredUnpinDepth) {
      /* This object is an ancestor of the current thread, so the object is
       * disentangled from the current thread's perspective. No further checks
       * are required. (If this object is mutable and has a down-pointer, it might
       * be a suspect, but I don't need it to be a suspect for entanglement.)
       */
      continue;
    }
    else if (isMutableH(s, header)) {
      assert (ES_contains(NULL, obj));
    }
    else {
      ME_pushExpanded(s, &st, f, p, actualUnpinDepth);
    }
  }

  ME_free(&st);
}
#else
inline void traverseAndCheck(
  __attribute__((unused)) GC_state s,
  __attribute__((unused)) objptr *opp,
  __attribute__((unused)) objptr op,
  __attribute__((unused)) void *rawArgs)
{
  return;
}
#endif

static inline objptr getRacyFwdPtr(pointer p) {
  while (isFwdHeader(getHeader(p))) {
    p = objptrToPointer(getFwdPtr(p), NULL);
  }
  return pointerToObjptr(p, NULL);
}

/* make_entangled's last steps for each object it visits: fix the pointer to
 * the object if it moved, make a mutable object a suspect, and (debug
 * runtime) check. */
static void ME_finish(
  GC_state s,
  struct ME_frame *f,
  objptr ptr,
  objptr new_ptr,
  bool mutable)
{
  assert(!hasFwdPtr(objptrToPointer(new_ptr, NULL)));
  assert(isPinned(new_ptr));

  if (ptr != new_ptr) {
    // Help LGC move along--because this reader might traverse this pointer
    // and it shouldn't see the forwarded one
    assert(hasFwdPtr(objptrToPointer(ptr, NULL)));
    *(f->opp) = new_ptr;
  }

  if (mutable && !ES_contains(NULL, new_ptr)) {
    HM_HierarchicalHeap lcaHeap =
      HM_HH_getHeapAtDepth(s, getThreadCurrent(s), f->unpinDepth);
    ES_add(s, HM_HH_getSuspects(lcaHeap), new_ptr);
    assert(ES_contains(NULL, new_ptr));
  }

  uint32_t requiredUnpinDepth = f->unpinDepth;
  traverseAndCheck(s, &new_ptr, new_ptr, &requiredUnpinDepth);

  assert (!mutable || ES_contains(NULL, new_ptr));
}

/* make_entangled pins everything reachable from an entangled object, up to
 * mutable objects and objects that are already pinned deeply enough. An
 * immutable object is pinned only after everything reachable from it is,
 * since a reader that finds it pinned doesn't look further.
 *
 * The traversal keeps its pending objects in an explicit stack (ME_stack),
 * not on the C stack. It visits the objects in the order the recursion did,
 * with the same unpin depths, and holds one frame per object on the current
 * path.
 */
static void make_entangled(
  GC_state s,
  objptr *rootp,
  struct ManageEntangledArgs *mea)
{
  struct ME_stack st;
  ME_init(&st);
  ME_pushVisit(&st, rootp, mea->unpinDepth);
  st.frames[0].addToRemSet = mea->firstCall;

  while (st.size > 0) {
    struct ME_frame *top = &(st.frames[st.size - 1]);
    bool headerChange = false, pinChange = false;
    objptr new_ptr;

    if (top->expanded) {
      objptr *fieldp = ME_nextField(top);
      if (NULL != fieldp) {
        ME_pushVisit(&st, fieldp, top->childDepth);
        continue;
      }

      /* Everything reachable from this immutable object is pinned. */
      struct ME_frame f = st.frames[--st.size];
      objptr ptr = f.ptr;
      HM_chunk chunk = HM_getChunkOf(objptrToPointer(ptr, NULL));
      new_ptr = pinObjectInfo(s, ptr, f.unpinDepth, PIN_ANY, &headerChange, &pinChange);
      assert(pinType(getHeader(objptrToPointer(new_ptr, NULL))) == PIN_ANY);
      if (pinChange && f.addToRemSet) {
        struct HM_remembered remElem_ = {.object = new_ptr, .from = BOGUS_OBJPTR};
        HM_HH_rememberAtLevel(s, HM_getLevelHead(chunk), &(remElem_), true);
        assert (HM_HH_getDepth(HM_getLevelHead(chunk)) != 1);
      }
      ME_finish(s, &f, ptr, new_ptr, false);
      continue;
    }

    struct ME_frame f = st.frames[--st.size];
    objptr ptr = *(f.opp);
    pointer p_ptr = objptrToPointer(ptr, NULL);
    HM_chunk chunk = HM_getChunkOf(p_ptr);
    GC_header header = getRacyHeader(p_ptr);
    assert(!isFwdHeader(header));
    bool mutable = isMutableH(s, header);

    if (pinType(header) != PIN_ANY || unpinDepthOfH(header) > f.unpinDepth) {
      if (mutable) {
        new_ptr = pinObjectInfo(s, ptr, f.unpinDepth, PIN_ANY, &headerChange, &pinChange);
        if (pinChange && f.addToRemSet) {
          struct HM_remembered remElem_ = {.object = new_ptr, .from = BOGUS_OBJPTR};
          HM_HH_rememberAtLevel(s, HM_getLevelHead(chunk), &(remElem_), true);
          assert (HM_HH_getDepth(HM_getLevelHead(chunk)) != 1);
        }
        ME_finish(s, &f, ptr, new_ptr, true);
      }
      else {
        /* Pin the fields first, then (when this frame has no more fields)
         * the object. The unpin depth of what's reachable may be smaller. */
        f.ptr = ptr;
        ME_pushExpanded(s, &st, f, p_ptr,
                        pinType(header) == PIN_NONE
                        ? f.unpinDepth
                        : min(f.unpinDepth, unpinDepthOfH(header)));
      }
    }
    else {
      new_ptr = getRacyFwdPtr(p_ptr);
      ME_finish(s, &f, ptr, new_ptr, mutable);
    }
  }

  ME_free(&st);
}


objptr manage_entangled(
  GC_state s,
  objptr ptr,
  decheck_tid_t reader)
{

  // GC_thread thread = getThreadCurrent(s);
  // decheck_tid_t tid = thread->decheckState;
  HM_chunk chunk = HM_getChunkOf(objptrToPointer(ptr, NULL));
  decheck_tid_t allocator = chunk->decheckState;

  if (!s->controls->manageEntanglement && false)
  {
    printf("Entanglement detected: object at %p\n", (void *)ptr);
    printf("Allocator tree depth: %d\n", tree_depth(allocator));
    printf("Allocator path: 0x%x\n", allocator.internal.path);
    printf("Allocator dag depth: %d\n", dag_depth(allocator));
    printf("Reader tree depth: %d\n", tree_depth(allocator));
    printf("Reader path: 0x%x\n", allocator.internal.path);
    printf("Reader dag depth: %d\n", dag_depth(allocator));
    exit(-1);
  }

  uint32_t unpinDepth = lcaHeapDepth(reader, allocator);
  GC_header header = getHeader(objptrToPointer (ptr, NULL));


  uint32_t current_ud = unpinDepthOfH(header);
  enum PinType current_pt = pinType(header);
  bool manage = isFwdHeader(header) ||
    current_pt != PIN_ANY ||
    current_ud > unpinDepth;

  if (current_pt != PIN_NONE && current_ud == 0)
  {
    return ptr;
  }

  if (manage) {
    uint32_t newUnpinDepth = current_pt == PIN_NONE ? unpinDepth : min(current_ud, unpinDepth);
    struct ManageEntangledArgs mea = {
      .reader = reader,
      .root = allocator,
      .unpinDepth = newUnpinDepth,
      .firstCall = !(current_pt == PIN_DOWN && current_ud == 1)
    };
    Trace0(EVENT_MANAGE_ENTANGLED_ENTER);
    make_entangled(s, &ptr, &mea);
    Trace0(EVENT_MANAGE_ENTANGLED_LEAVE);

    uint32_t requiredUnpinDepth = newUnpinDepth;
    traverseAndCheck(s, &ptr, ptr, &requiredUnpinDepth);
  }
  else {
    if (isMutableH(s, header) && !ES_contains(NULL, ptr)) {
      HM_HierarchicalHeap lcaHeap = HM_HH_getHeapAtDepth(s, getThreadCurrent(s), unpinDepth);
      ES_add(s, HM_HH_getSuspects(lcaHeap), ptr);
      assert(ES_contains(NULL, ptr));
    }

    uint32_t requiredUnpinDepth = unpinDepth;
    traverseAndCheck(s, &ptr, ptr, &requiredUnpinDepth);
  }


  return ptr;
  // GC_header header = getRacyHeader(objptrToPointer(ptr, NULL));
  // bool mutable = isMutableH(s, header);
  // bool headerChange = false, pinChange = false;
  // objptr new_ptr = ptr;
  // if (pinType(header) != PIN_ANY || unpinDepthOfH(header) > unpinDepth)
  // {
  //   if (mutable)
  //   {
  //     new_ptr = pinObjectInfo(ptr, unpinDepth, PIN_ANY, &headerChange, &pinChange);
  //     if (!ES_contains(NULL, new_ptr)) {
  //       HM_HierarchicalHeap lcaHeap = HM_HH_getHeapAtDepth(s, thread, unpinDepth);
  //       ES_add(s, HM_HH_getSuspects(lcaHeap), new_ptr);
  //     }
  //   }
  //   else
  //   {
  //     struct GC_foreachObjptrClosure emanageClosure =
  //         {.fun = manage_entangled, .env = NULL};
  //     foreachObjptrInObject(s, ptr, &trueObjptrPredicateClosure, &emanageClosure, FALSE);
  //     new_ptr = pinObjectInfo(ptr, unpinDepth, PIN_ANY, &headerChange, &pinChange);
  //   }
  //   if (pinChange)
  //   {
  //     struct HM_remembered remElem_ = {.object = new_ptr, .from = BOGUS_OBJPTR};
  //     HM_HH_rememberAtLevel(s, HM_getLevelHeadPathCompress(chunk), &(remElem_), true);
  //   }
  // }
  // else
  // {
  //   if (!mutable)
  //   {
  //     traverseAndCheck(s, &new_ptr, new_ptr, NULL);
  //   }
  // }

  // traverseAndCheck(s, &new_ptr, new_ptr, NULL);
  // return new_ptr;
}

#else
objptr manage_entangled(GC_state s, objptr ptr, decheck_tid_t reader) {
  (void)s;
  (void)ptr;
  (void)reader;
  return ptr;
}
#endif

#ifdef DETECT_ENTANGLEMENT

bool decheck(GC_state s, objptr ptr) {
  if (!s->controls->manageEntanglement) {
    return true;
  }
  GC_thread thread = getThreadCurrent(s);
  if (thread == NULL)
      return true;
  decheck_tid_t tid = thread->decheckState;
  if (tid.bits == DECHECK_BOGUS_BITS)
      return true;
  if (!isObjptr(ptr))
      return true;
  HM_chunk chunk = HM_getChunkOf(objptrToPointer(ptr, NULL));
  if (chunk == NULL)
      return true;
  decheck_tid_t allocator = chunk->decheckState;
  if (allocator.bits == DECHECK_BOGUS_BITS) {
      // assert (false);
    return true;
  }
  if (decheckIsOrdered(thread, allocator))
      return true;

  return false;
}
#else
bool decheck(GC_state s, objptr ptr)
{
  (void)s;
  (void)ptr;
  return true;
}
#endif


#ifdef DETECT_ENTANGLEMENT
void GC_HH_copySyncDepthsFromThread(GC_state s, objptr fromThreadp, objptr toThreadp, uint32_t stealDepth) {
  if (stealDepth > DECHECK_DEPTHS_LEN) {
    DIE("attempted to copy sync depths up to %"PRIu32, stealDepth);
  }

  GC_thread fromThread = threadObjptrToStruct(s, fromThreadp);
  GC_thread toThread = threadObjptrToStruct(s, toThreadp);

  uint32_t* from = &(fromThread->decheckSyncDepths[0]);
  uint32_t* to = &(toThread->decheckSyncDepths[0]);

  /* SAM_NOTE: TODO:
   *
   * I thought it should be possible to just do the first
   * few items, i.e.
   *   memcpy(to, from, stealDepth * sizeof(uint32_t))
   * but this trips the assertions that the thread-local syncDepths match
   * the global syncDepths?? Not sure what's going on.
   *
   * It's safe to copy the whole thing, so I'll do that as a bandaid for
   * now. But really should figure out what's going on here. Some sort of
   * off-by-one error...
   */
  memcpy(to, from, DECHECK_DEPTHS_LEN * sizeof(uint32_t));
}

#else
void GC_HH_copySyncDepthsFromThread(GC_state s, objptr fromThreadp, objptr toThreadp, uint32_t stealDepth) {
  (void)s;
  (void)fromThreadp;
  (void)toThreadp;
  (void)stealDepth;
  return;
}
#endif

