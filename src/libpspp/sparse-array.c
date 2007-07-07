/* PSPP - a program for statistical analysis.
   Copyright (C) 2007 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <config.h>

#include <libpspp/sparse-array.h>

#include <limits.h>
#include <string.h>

#include <libpspp/assertion.h>
#include <libpspp/misc.h>
#include <libpspp/pool.h>

/* Sparse array data structure.

   The sparse array is implemented in terms of a "radix tree", a
   multiway tree in which a set of bits drawn from the key
   determine the child chosen at each level during a search.  The
   most-significant bits determine a child of the root, the next
   bits determine a child of that child, and so on, until the
   least-significant bits determine a leaf node.

   In this implementation, the branching factor at each level is
   held constant at 2**BITS_PER_LEVEL.  The tree is only made as
   tall as need be for the currently largest key, and nodes that
   would be entirely empty are not allocated at all.  The
   elements are stored in the leaf nodes. */

/* Number of bits from the key used as the index at each level. */
#define BITS_PER_LEVEL 5

/* Branching factor. */
#define PTRS_PER_LEVEL (1u << BITS_PER_LEVEL)

/* Maximum height. */
#define LONG_BITS (sizeof (unsigned long int) * CHAR_BIT)
#define MAX_HEIGHT DIV_RND_UP (LONG_BITS, BITS_PER_LEVEL)

/* Bit-mask for index. */
#define LEVEL_MASK ((1ul << BITS_PER_LEVEL) - 1)

/* Pointer to an internal node or a leaf node.
   Pointers in internal nodes at level 1 point to leaf nodes;
   other pointers point to internal nodes. */
union pointer
  {
    struct internal_node *internal;
    struct leaf_node *leaf;
  };

/* A sparse array. */
struct sparse_array
  {
    struct pool *pool;          /* Pool used for allocations. */
    size_t elem_size;           /* Element size, rounded for alignment. */
    unsigned long count;        /* Number of elements in tree. */

    /* Radix tree. */
    union pointer root;         /* Root of tree. */
    int height;                 /* 0=empty tree;
                                   1=root points to leaf,
                                   2=root points to internal node
                                     that points to leaves,
                                   and so on. */

    /* Cache for speeding up access. */
    unsigned long int cache_ofs; /* Group of keys that cache points to,
                                    shifted right BITS_PER_LEVEL bits;
                                    ULONG_MAX for empty cache. */
    struct leaf_node *cache;    /* Cached leaf node, or a null
                                   pointer for a negative cache. */
  };

/* An internal node in the radix tree. */
struct internal_node
  {
    int count;                  /* Number of nonnul children. */
    union pointer down[PTRS_PER_LEVEL]; /* Children. */
  };

/* A leaf node in the radix tree. */
struct leaf_node
  {
    /* Bit-vector of elements that are in use. */
    unsigned long int in_use[DIV_RND_UP (PTRS_PER_LEVEL, LONG_BITS)];
    /* element_type elements[PTRS_PER_LEVEL]; */
  };

/* Returns SIZE rounded up to a safe alignment. */
#define ALIGN_SIZE(SIZE) ROUND_UP (SIZE, sizeof (long int))

/* Returns the size of EXPR_OR_TYPE rounded up to a safe
   alignment. */
#define SIZEOF_ALIGNED(EXPR_OR_TYPE) ALIGN_SIZE (sizeof (EXPR_OR_TYPE))

static inline bool index_in_range (const struct sparse_array *,
                                   unsigned long int);
static inline bool is_in_use (const struct leaf_node *, unsigned int);
static inline bool any_in_use (const struct leaf_node *);
static inline void set_in_use (struct leaf_node *, unsigned int);
static inline void unset_in_use (struct leaf_node *, unsigned int);
static inline int scan_in_use (struct leaf_node *, unsigned int);
static inline void *leaf_element (const struct sparse_array *,
                                  struct leaf_node *, unsigned int);
static inline size_t leaf_size (const struct sparse_array *);

static struct leaf_node *find_leaf_node (const struct sparse_array *,
                                         unsigned long int);
static void decrease_height (struct sparse_array *);
static void *scan_leaf (struct sparse_array *, struct leaf_node *,
                        unsigned long int, unsigned long int *);
static void *do_scan (struct sparse_array *, union pointer *, int,
                      unsigned long int, unsigned long int *);

/* Creates and returns a new sparse array that will contain
   elements that are ELEM_SIZE bytes in size. */
struct sparse_array *
sparse_array_create (size_t elem_size)
{
  return sparse_array_create_pool (NULL, elem_size);
}

/* Creates and returns a new sparse array that will contain
   elements that are ELEM_SIZE bytes in size.  Data in the sparse
   array will be allocated from POOL, which may be null. */
struct sparse_array *
sparse_array_create_pool (struct pool *pool, size_t elem_size)
{
  struct sparse_array *spar = pool_malloc (pool, sizeof *spar);
  spar->pool = pool;
  spar->elem_size = ALIGN_SIZE (elem_size);
  spar->height = 0;
  spar->root.leaf = NULL;
  spar->count = 0;
  spar->cache_ofs = ULONG_MAX;
  return spar;
}

/* Destroys SPAR node pointed to by P at the given LEVEL. */
static void
do_destroy (struct sparse_array *spar, union pointer *p, int level)
{
  if (level > 0)
    {
      struct internal_node *node = p->internal;
      int count = node->count;
      int i;

      for (i = 0; ; i++)
        {
          union pointer *q = &p->internal->down[i];
          if (level > 1 ? q->internal != NULL : q->leaf != NULL)
            {
              do_destroy (spar, q, level - 1);
              if (--count == 0)
                break;
            }
        }
      pool_free (spar->pool, p->internal);
    }
  else if (level == 0)
    pool_free (spar->pool, p->leaf);
}

/* Destroys SPAR.
   Any elements in SPAR are deallocated.  Thus, if per-element
   destruction is necessary, it should be done before destroying
   the sparse array. */
void
sparse_array_destroy (struct sparse_array *spar)
{
  do_destroy (spar, &spar->root, spar->height - 1);
  pool_free (spar->pool, spar);
}

/* Returns the number of elements in SPAR. */
unsigned long int
sparse_array_count (const struct sparse_array *spar)
{
  return spar->count;
}

/* Increases SPAR's height by 1, allowing it to hold
   PTRS_PER_LEVEL times more elements. */
static void
increase_height (struct sparse_array *spar)
{
  assert (spar->height < MAX_HEIGHT);
  spar->height++;
  if (spar->height == 1)
    spar->root.leaf = pool_zalloc (spar->pool, leaf_size (spar));
  else
    {
      struct internal_node *new_root;
      new_root = pool_zalloc (spar->pool, sizeof *new_root);
      new_root->count = 1;
      new_root->down[0] = spar->root;
      spar->root.internal = new_root;
    }
}

/* Finds the leaf node in SPAR that contains the element for KEY.
   SPAR must be tall enough to hold KEY.
   Creates the leaf if it doesn't already exist. */
static struct leaf_node *
create_leaf_node (struct sparse_array *spar, unsigned long int key)
{
  union pointer *p;
  int *count = NULL;
  int level;

  assert (index_in_range (spar, key));

  /* Short-circuit everything if KEY is in the leaf cache. */
  if (key >> BITS_PER_LEVEL == spar->cache_ofs && spar->cache != NULL)
    return spar->cache;

  /* Descend through internal nodes. */
  p = &spar->root;
  for (level = spar->height - 1; level > 0; level--)
    {
      if (p->internal == NULL)
        {
          p->internal = pool_zalloc (spar->pool, sizeof *p->internal);
          ++*count;
        }

      count = &p->internal->count;
      p = &p->internal->down[(key >> (level * BITS_PER_LEVEL)) & LEVEL_MASK];
    }

  /* Create leaf if necessary. */
  if (p->leaf == NULL)
    {
      p->leaf = pool_zalloc (spar->pool, leaf_size (spar));
      ++*count;
    }

  /* Update cache. */
  spar->cache = p->leaf;
  spar->cache_ofs = key >> BITS_PER_LEVEL;

  return p->leaf;
}

/* Inserts into SPAR an element with the given KEY, which must not
   already exist in SPAR.
   Returns the new element for the caller to initialize. */
void *
sparse_array_insert (struct sparse_array *spar, unsigned long int key)
{
  struct leaf_node *leaf;

  while (!index_in_range (spar, key))
    increase_height (spar);

  spar->count++;

  leaf = create_leaf_node (spar, key);
  assert (!is_in_use (leaf, key));
  set_in_use (leaf, key);
  return leaf_element (spar, leaf, key);
}

/* Finds and returns the element in SPAR with the given KEY.
   Returns a null pointer if KEY does not exist in SPAR. */
void *
sparse_array_get (const struct sparse_array *spar, unsigned long int key)
{
  if (index_in_range (spar, key))
    {
      struct leaf_node *leaf = find_leaf_node (spar, key);
      if (leaf != NULL && is_in_use (leaf, key))
        return leaf_element (spar, leaf, key);
    }
  return NULL;
}

/* Removes the element with the given KEY from SPAR.
   Returns true if an element was removed, false if SPAR hadn't
   contained an element with the given KEY.

   If elements need to be destructed, then the caller should have
   already taken care of it before calling this function; the
   element's content must be considered freed and of
   indeterminate value after it is removed. */
bool
sparse_array_remove (struct sparse_array *spar, unsigned long int key)
{
  union pointer *path[MAX_HEIGHT], **last;
  struct leaf_node *leaf;
  union pointer *p;
  int level;

  if (!index_in_range (spar, key))
    return false;

  /* Find and free element in leaf. */
  leaf = find_leaf_node (spar, key);
  if (leaf == NULL || !is_in_use (leaf, key))
    return false;
#if ASSERT_LEVEL >= 10
  memset (leaf_element (spar, leaf, key), 0xcc, spar->elem_size);
#endif
  unset_in_use (leaf, key);
  spar->count--;
  if (any_in_use (leaf))
    return true;

  /* The leaf node is empty.
     Retrace the path of internal nodes traversed to the leaf. */
  p = &spar->root;
  last = path;
  for (level = spar->height - 1; level > 0; level--)
    {
      *last++ = p;
      p = &p->internal->down[(key >> (level * BITS_PER_LEVEL)) & LEVEL_MASK];
    }

  /* Free the leaf node and prune it from the tree. */
  spar->cache_ofs = ULONG_MAX;
  pool_free (spar->pool, leaf);
  p->leaf = NULL;

  /* Update counts in the internal nodes above the leaf.
     Free any internal nodes that become empty. */
  while (last > path)
    {
      p = *--last;
      if (--p->internal->count > 0)
        {
          if (p == &spar->root)
            decrease_height (spar);
          return true;
        }

      pool_free (spar->pool, p->internal);
      p->internal = NULL;
    }
  spar->height = 0;
  return true;
}

/* Scans SPAR in increasing order of keys for in-use elements.
   If SKIP is NULL, the scan starts from key 0;
   otherwise, it starts just after key *SKIP.
   If an element is found, returns it and stores the element's
   key into *FOUND; otherwise, returns a null pointer and does
   not modify *FOUND. */
void *
sparse_array_scan (const struct sparse_array *spar_, unsigned long int *skip,
                   unsigned long int *found)
{
  struct sparse_array *spar = (struct sparse_array *) spar_;
  unsigned long int start;

  /* Find our starting point. */
  if (skip != NULL)
    {
      start = *skip + 1;
      if (start == 0)
        return NULL;
    }
  else
    start = 0;

  /* Check the cache. */
  if (start >> BITS_PER_LEVEL == spar->cache_ofs)
    {
      void *p = scan_leaf (spar, spar->cache, start, found);
      if (p)
        return p;
      start &= ~LEVEL_MASK;
      start += PTRS_PER_LEVEL;
      if (start == 0)
        return NULL;
    }

  /* Do the scan. */
  if (!index_in_range (spar, start))
    return NULL;
  return do_scan (spar, &spar->root, spar->height - 1, start, found);
}

/* Returns true iff KEY is in the range of keys currently
   represented by SPAR. */
static inline bool
index_in_range (const struct sparse_array *spar, unsigned long int key)
{
  return (spar->height == 0 ? false
          : spar->height >= MAX_HEIGHT ? true
          : key < (1ul << (spar->height * BITS_PER_LEVEL)));
}

/* Returns true iff LEAF contains an in-use element with the
   given KEY. */
static inline bool
is_in_use (const struct leaf_node *leaf, unsigned int key)
{
  key &= LEVEL_MASK;
  return (leaf->in_use[key / LONG_BITS] & (1ul << (key % LONG_BITS))) != 0;
}

/* Returns true iff LEAF contains any in-use elements. */
static inline bool
any_in_use (const struct leaf_node *leaf)
{
  size_t i;
  for (i = 0; i < sizeof leaf->in_use / sizeof *leaf->in_use; i++)
    if (leaf->in_use[i])
      return true;
  return false;
}

/* Marks element KEY in LEAF as in-use. */
static inline void
set_in_use (struct leaf_node *leaf, unsigned int key)
{
  key &= LEVEL_MASK;
  leaf->in_use[key / LONG_BITS] |= 1ul << (key % LONG_BITS);
}

/* Marks element KEY in LEAF as not in-use. */
static inline void
unset_in_use (struct leaf_node *leaf, unsigned int key)
{
  key &= LEVEL_MASK;
  leaf->in_use[key / LONG_BITS] &= ~(1ul << (key % LONG_BITS));
}

/* Returns the number of trailing 0-bits in X.
   Undefined if X is zero. */
static inline int
count_trailing_zeros (unsigned long int x)
{
  /* This algorithm is from _Hacker's Delight_ section 5.4. */
  int n = 1;

#define COUNT_STEP(BITS)                        \
    if (!(x & ((1ul << (BITS)) - 1)))           \
      {                                         \
        n += BITS;                              \
        x >>= BITS;                             \
      }

#if ULONG_MAX >> 31 >> 31 >> 2
  COUNT_STEP (64);
#endif
#if ULONG_MAX >> 31 >> 1
  COUNT_STEP (32);
#endif
  COUNT_STEP (16);
  COUNT_STEP (8);
  COUNT_STEP (4);
  COUNT_STEP (2);

  return n - (x & 1);
}

/* Returns the least index of the in-use element in LEAF greater
   than or equal to IDX. */
static inline int
scan_in_use (struct leaf_node *leaf, unsigned int idx)
{
  for (; idx < PTRS_PER_LEVEL; idx = (idx & ~(LONG_BITS - 1)) + LONG_BITS)
    {
      int ofs = idx % LONG_BITS;
      unsigned long int in_use = leaf->in_use[idx / LONG_BITS] >> ofs;
      if (!in_use)
        continue;
      return count_trailing_zeros (in_use) + idx;
    }
  return -1;
}

/* Returns the address of element with the given KEY in LEAF,
   which is a node in SPAR. */
static inline void *
leaf_element (const struct sparse_array *spar, struct leaf_node *leaf,
              unsigned int key)
{
  key &= LEVEL_MASK;
  return (char *) leaf + SIZEOF_ALIGNED (*leaf) + (spar->elem_size * key);
}

/* Returns the size of a leaf node in SPAR. */
static inline size_t
leaf_size (const struct sparse_array *spar)
{
  return SIZEOF_ALIGNED (struct leaf_node) + spar->elem_size * PTRS_PER_LEVEL;
}

/* Finds and returns the leaf node in SPAR that contains KEY.
   Returns null if SPAR does not have a leaf node that contains
   KEY. */
static struct leaf_node *
find_leaf_node (const struct sparse_array *spar_, unsigned long int key)
{
  struct sparse_array *spar = (struct sparse_array *) spar_;
  const union pointer *p;
  int level;

  assert (index_in_range (spar, key));

  /* Check the cache first. */
  if (key >> BITS_PER_LEVEL == spar->cache_ofs)
    return spar->cache;

  /* Descend through internal nodes. */
  p = &spar->root;
  for (level = spar->height - 1; level > 0; level--)
    {
      if (p->internal == NULL)
        return NULL;
      p = &p->internal->down[(key >> (level * BITS_PER_LEVEL)) & LEVEL_MASK];
    }

  /* Update cache. */
  spar->cache = p->leaf;
  spar->cache_ofs = key >> BITS_PER_LEVEL;

  return p->leaf;
}

/* Reduces SPAR's height to the minimum needed value by
   eliminating levels that contain only a single entry for all
   0-bits. */
static void
decrease_height (struct sparse_array *spar)
{
  while (spar->height > 1
         && spar->root.internal->count == 1
         && spar->root.internal->down[0].internal)
    {
      struct internal_node *p = spar->root.internal;
      spar->height--;
      spar->root = p->down[0];
      pool_free (spar->pool, p);
    }
}

/* Scans leaf node LEAF, which is in SPAR, for the in-use element
   with the least key greater than or equal to START.  If such an
   element is found, returns a pointer to it and stores its key
   in *FOUND; otherwise, returns a null pointer and does not
   modify *FOUND. */
static void *
scan_leaf (struct sparse_array *spar, struct leaf_node *leaf,
           unsigned long int start, unsigned long int *found)
{
  int idx = scan_in_use (leaf, start & LEVEL_MASK);
  if (idx >= 0)
    {
      *found = (start & ~LEVEL_MASK) | idx;
      spar->cache = leaf;
      spar->cache_ofs = *found >> BITS_PER_LEVEL;
      return leaf_element (spar, leaf, idx);
    }

  return NULL;
}

/* Scans P, which is at LEVEL and within SPAR, and its subnodes,
   for the in-use element with the least key greater than or
   equal to START.  If such an element is found, returns a
   pointer to it and stores its key in *FOUND; otherwise, returns
   a null pointer and does not modify *FOUND. */
static inline void *
scan_internal_node (struct sparse_array *spar, struct internal_node *node,
                    int level, unsigned long int start,
                    unsigned long int *found)
{
  int shift = level * BITS_PER_LEVEL;
  int count = node->count;
  int i;

  for (i = (start >> shift) & LEVEL_MASK; i < PTRS_PER_LEVEL; i++)
    {
      union pointer *q = &node->down[i];
      if (level > 1 ? q->internal != NULL : q->leaf != NULL)
        {
          void *element = do_scan (spar, q, level - 1, start, found);
          if (element)
            return element;
          if (--count == 0)
            return NULL;
        }

      start &= ~((1ul << shift) - 1);
      start += 1ul << shift;
    }
  return NULL;
}

/* Scans P, which is at LEVEL and within SPAR, and its subnodes,
   for the in-use element with the least key greater than or
   equal to START.  If such an element is found, returns a
   pointer to it and stores its key in *FOUND; otherwise, returns
   a null pointer and does not modify *FOUND. */
static void *
do_scan (struct sparse_array *spar, union pointer *p, int level,
         unsigned long int start, unsigned long int *found)
{
  return (level == 0
          ? scan_leaf (spar, p->leaf, start, found)
          : scan_internal_node (spar, p->internal, level, start, found));
}

