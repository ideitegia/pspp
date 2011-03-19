/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "libpspp/hmap.h"

#include <assert.h>
#include <stdlib.h>

#include "gl/xalloc.h"

static size_t capacity_to_mask (size_t capacity);

/* Initializes MAP as a new hash map that is initially empty. */
void
hmap_init (struct hmap *map)
{
  map->count = 0;
  map->mask = 0;
  map->buckets = &map->one;
  map->one = NULL;
}

/* Exchanges the contents of hash maps A and B. */
void
hmap_swap (struct hmap *a, struct hmap *b)
{
  struct hmap tmp = *a;
  *a = *b;
  *b = tmp;
  if (!a->mask)
    a->buckets = &a->one;
  if (!b->mask)
    b->buckets = &b->one;
}

/* Removes all of the elements from MAP, without destroying MAP itself and
   without accessing the existing elements (if any). */
void
hmap_clear (struct hmap *map)
{
  size_t i;

  for (i = 0; i <= map->mask; i++)
    map->buckets[i] = NULL;
  map->count = 0;
}

/* Frees the memory, if any, allocated by hash map MAP.  This has
   no effect on the actual data items in MAP, if any, because the
   client is responsible for allocating and freeing them.  It
   could, however, render them inaccessible if the only pointers
   to them were from MAP itself, so in such a situation one
   should iterate through the map and free the data items before
   destroying it. */
void
hmap_destroy (struct hmap *map) 
{
  if (map != NULL && map->buckets != &map->one) 
    free (map->buckets);
}

/* Reallocates MAP's hash buckets so that NEW_MASK becomes the
   hash value bit-mask used to choose a hash bucket, then
   rehashes any data elements in MAP into the new hash buckets.

   NEW_MASK must be a power of 2 minus 1 (including 0), that is,
   its value in binary must be all 1-bits.  */
static void
hmap_rehash (struct hmap *map, size_t new_mask) 
{
  struct hmap_node **new_buckets;
  struct hmap_node *node, *next;

  assert ((new_mask & (new_mask + 1)) == 0);
  if (new_mask)
    new_buckets = xcalloc (new_mask + 1, sizeof *new_buckets);
  else 
    {
      new_buckets = &map->one;
      new_buckets[0] = NULL;
    }
      
  if (map->count > 0)
    {
      for (node = hmap_first (map); node != NULL; node = next)
        {
          size_t new_idx = node->hash & new_mask;
          struct hmap_node **new_bucket = &new_buckets[new_idx];
          next = hmap_next (map, node);
          node->next = *new_bucket;
          *new_bucket = node;
        } 
    }
  if (map->buckets != &map->one)
    free (map->buckets);
  map->buckets = new_buckets;
  map->mask = new_mask;
}

/* Ensures that MAP has sufficient space to store at least
   CAPACITY data elements, allocating a new set of buckets and
   rehashing if necessary. */
void
hmap_reserve (struct hmap *map, size_t capacity)
{
  if (capacity > hmap_capacity (map))
    hmap_rehash (map, capacity_to_mask (capacity));
}

/* Shrinks MAP's set of buckets to the minimum number needed to
   store its current number of elements, allocating a new set of
   buckets and rehashing if that would save space. */
void
hmap_shrink (struct hmap *map) 
{
  size_t new_mask = capacity_to_mask (map->count);
  if (new_mask < map->mask) 
    hmap_rehash (map, new_mask); 
}

/* Moves NODE around in MAP to compensate for its hash value
   having changed to NEW_HASH.

   This function does not verify that MAP does not already
   contain a data item that duplicates NODE's new value.  If
   duplicates should be disallowed (which is the usual case),
   then the client must check for duplicates before changing
   NODE's value. */
void
hmap_changed (struct hmap *map, struct hmap_node *node, size_t new_hash)
{
  if ((new_hash ^ node->hash) & map->mask) 
    {
      hmap_delete (map, node);
      hmap_insert_fast (map, node, new_hash);
    }
  else
    node->hash = new_hash;
}

/* Hash map nodes may be moved around in memory as necessary,
   e.g. as the result of an realloc operation on a block that
   contains a node.  Once this is done, call this function
   passing NODE that was moved, its former location in memory
   OLD, and its hash map MAP before attempting any other
   operation on MAP, NODE, or any other node in MAP.

   It is not safe to move more than one node, then to call this
   function for each node.  Instead, move a single node, call
   this function, move another node, and so on.  Alternatively,
   remove all affected nodes from the hash map, move them, then
   re-insert all of them.

   Assuming uniform hashing and no duplicate data items in MAP,
   this function runs in constant time. */
void
hmap_moved (struct hmap *map,
            struct hmap_node *node, const struct hmap_node *old) 
{
  struct hmap_node **p = &map->buckets[node->hash & map->mask];
  while (*p != old)
    p = &(*p)->next;
  *p = node;
}

/* Returns the minimum-value mask required to allow for a hash
   table capacity of at least CAPACITY.  The return value will be
   a bit-mask suitable for use as the "mask" member of struct
   hmap, that is, a power of 2 minus 1 (including 0). */
static size_t
capacity_to_mask (size_t capacity) 
{
  /* Calculate the minimum mask necesary to support the given
     capacity. */
  size_t mask = 0;
  while (hmap_mask_to_capacity__ (mask) < capacity)
    mask = (mask << 1) | 1;

  /* If the mask is nonzero, make it at least 3, because there is
     little point in allocating an array of just 2 pointers. */
  mask |= (mask & 1) << 1;

  return mask;
}
