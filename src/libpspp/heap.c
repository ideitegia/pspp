/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2011 Free Software Foundation, Inc.

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "libpspp/heap.h"
#include "libpspp/pool.h"
#include "libpspp/assertion.h"

#include "gl/xalloc.h"

/* A heap. */
struct heap
  {
    /* Comparison function and auxiliary data. */
    heap_compare_func *compare;
    const void *aux;

    /* Contents. */
    struct heap_node **nodes;   /* Element 0 unused, 1...CNT are the heap. */
    size_t cnt;                 /* Number of elements in heap. */
    size_t cap;                 /* Max CNT without allocating more memory. */
  };

static inline void set_node (struct heap *, size_t idx, struct heap_node *);
static inline bool less (const struct heap *, size_t a, size_t b);
static bool UNUSED is_heap (const struct heap *);
static inline void propagate_down (struct heap *, size_t idx);
static inline bool propagate_up (struct heap *, size_t idx);

/* Creates and return a new min-heap.  COMPARE is used as
   comparison function and passed AUX as auxiliary data.

   To obtain a max-heap, negate the return value of the
   comparison function. */
struct heap *
heap_create (heap_compare_func *compare, const void *aux)
{
  struct heap *h = xmalloc (sizeof *h);
  h->compare = compare;
  h->aux = aux;
  h->nodes = NULL;
  h->cap = 0;
  h->cnt = 0;
  return h;
}

/* Destroys H (callback for pool). */
static void
destroy_callback (void *h)
{
  heap_destroy (h);
}

/* Creates and return a new min-heap and registers for its
   destruction with POOL.  COMPARE is used as comparison function
   and passed AUX as auxiliary data.

   To obtain a max-heap, negate the return value of the
   comparison function. */
struct heap *
heap_create_pool (struct pool *pool,
                  heap_compare_func *compare, const void *aux)
{
  struct heap *h = heap_create (compare, aux);
  pool_register (pool, destroy_callback, h);
  return h;
}

/* Destroys heap H. */
void
heap_destroy (struct heap *h)
{
  if (h != NULL)
    {
      free (h->nodes);
      free (h);
    }
}

/* Returns true if H is empty, false if it contains at least one
   element. */
bool
heap_is_empty (const struct heap *h)
{
  return h->cnt == 0;
}

/* Returns the number of elements in H. */
size_t
heap_count (const struct heap *h)
{
  return h->cnt;
}

/* Heap nodes may be moved around in memory as necessary, e.g. as
   the result of an realloc operation on a block that contains a
   heap node.  Once this is done, call this function passing the
   NODE that was moved and its heap H before attempting any other
   operation on H. */
void
heap_moved (struct heap *h, struct heap_node *node)
{
  assert (node->idx <= h->cnt);
  h->nodes[node->idx] = node;
}

/* Returns the node with the minimum value in H, which must not
   be empty. */
struct heap_node *
heap_minimum (const struct heap *h)
{
  assert (!heap_is_empty (h));
  return h->nodes[1];
}

/* Inserts the given NODE into H. */
void
heap_insert (struct heap *h, struct heap_node *node)
{
  if (h->cnt >= h->cap)
    {
      h->cap = 2 * (h->cap + 8);
      h->nodes = xnrealloc (h->nodes, h->cap + 1, sizeof *h->nodes);
    }

  h->cnt++;
  set_node (h, h->cnt, node);
  propagate_up (h, h->cnt);

  expensive_assert (is_heap (h));
}

/* Deletes the given NODE from H. */
void
heap_delete (struct heap *h, struct heap_node *node)
{
  assert (node->idx <= h->cnt);
  assert (h->nodes[node->idx] == node);

  if (node->idx < h->cnt)
    {
      set_node (h, node->idx, h->nodes[h->cnt--]);
      heap_changed (h, h->nodes[node->idx]);
    }
  else
    h->cnt--;
}

/* After client code changes the value represented by a heap
   node, it must use this function to update the heap structure.
   It is also safe (but not useful) to call this function if
   NODE's value has not changed.

   It is not safe to update more than one node's value in the
   heap, then to call this function for each node.  Instead,
   update a single node's value, call this function, update
   another node's value, and so on.  Alternatively, remove all
   the nodes from the heap, update their values, then re-insert
   all of them. */
void
heap_changed (struct heap *h, struct heap_node *node)
{
  assert (node->idx <= h->cnt);
  assert (h->nodes[node->idx] == node);

  if (!propagate_up (h, node->idx))
    propagate_down (h, node->idx);

  expensive_assert (is_heap (h));
}

static inline size_t lesser_node (const struct heap *, size_t a, size_t b);
static inline void swap_nodes (struct heap *, size_t a, size_t b);

/* Sets h->nodes[IDX] and updates NODE's 'idx' field
   accordingly. */
static void
set_node (struct heap *h, size_t idx, struct heap_node *node)
{
  h->nodes[idx] = node;
  h->nodes[idx]->idx = idx;
}

/* Moves the node with the given IDX down the heap as necessary
   to restore the heap property. */
static void
propagate_down (struct heap *h, size_t idx)
{
  for (;;)
    {
      size_t least;
      least = lesser_node (h, idx, 2 * idx);
      least = lesser_node (h, least, 2 * idx + 1);
      if (least == idx)
        break;

      swap_nodes (h, least, idx);
      idx = least;
    }
}

/* Moves the node with the given IDX up the heap as necessary
   to restore the heap property.  Returns true if the node was
   moved, false otherwise.*/
static bool
propagate_up (struct heap *h, size_t idx)
{
  bool moved = false;
  for (; idx > 1 && less (h, idx, idx / 2); idx /= 2)
    {
      swap_nodes (h, idx, idx / 2);
      moved = true;
    }
  return moved;
}

/* Returns true if, in H, the node with index A has value less
   than the node with index B.  */
static bool
less (const struct heap *h, size_t a, size_t b)
{
  return h->compare (h->nodes[a], h->nodes[b], h->aux) < 0;
}

/* Returns A or B according to which is the index of the node
   with the lesser value.  B is allowed to be out of the range of
   valid node indexes, in which case A is returned. */
static size_t
lesser_node (const struct heap *h, size_t a, size_t b)
{
  assert (a <= h->cnt);
  return b > h->cnt || less (h, a, b) ? a : b;
}

/* Swaps, in H, the nodes with indexes A and B. */
static void
swap_nodes (struct heap *h, size_t a, size_t b)
{
  struct heap_node *t;

  assert (a <= h->cnt);
  assert (b <= h->cnt);

  t = h->nodes[a];
  set_node (h, a, h->nodes[b]);
  set_node (h, b, t);
}

/* Returns true if H is a valid heap,
   false otherwise. */
static bool UNUSED
is_heap (const struct heap *h)
{
  size_t i;

  for (i = 2; i <= h->cnt; i++)
    if (less (h, i, i / 2))
      return false;

  for (i = 1; i <= h->cnt; i++)
    if (h->nodes[i]->idx != i)
      return false;

  return true;
}
