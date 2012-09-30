/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2009, 2010, 2012 Free Software Foundation, Inc.

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

#include "libpspp/hmapx.h"
#include <stdlib.h>
#include "gl/xalloc.h"

/* Frees the memory, if any, allocated by hash map MAP, including
   all hmapx_nodes that it contains.  The user-defined data items
   that the hmapx_nodes point to are not affected.  If those
   items should be freed, then it should be done by iterating
   through MAP's contents before destroying MAP. */
void
hmapx_destroy (struct hmapx *map) 
{
  if (map != NULL) 
    {
      if (!(hmapx_is_empty (map)))
        {
          struct hmapx_node *node, *next;
          for (node = hmapx_first (map); node != NULL; node = next)
            {
              next = hmapx_next (map, node);
              free (node); 
            }
        }
      hmap_destroy (&map->hmap);
    }
}

/* Removes all hmapx_nodes from MAP and frees them.  The user-defined data
   items that the hmapx_nodes point to are not affected. */
void
hmapx_clear (struct hmapx *map)
{
  struct hmapx_node *node, *next;

  for (node = hmapx_first (map); node; node = next)
    {
      next = hmapx_next (map, node);
      hmapx_delete (map, node);
    }
}

/* Allocates and returns a new hmapx_node with DATA as its data
   item. */
static struct hmapx_node *
make_hmapx_node (void *data) 
{
  struct hmapx_node *node = xmalloc (sizeof *node);
  node->data = data;
  return node;
}

/* Inserts DATA into MAP with hash value HASH and returns the new
   hmapx_node created to contain DATA.  If the insertion causes
   MAP's current capacity, as reported by hmapx_capacity(), to be
   exceeded, rehashes MAP with an increased number of hash
   buckets.

   This function runs in constant time amortized over all the
   insertions into MAP.

   This function does not verify that MAP does not already
   contain a data item with the same value as DATA.  If
   duplicates should be disallowed (which is the usual case),
   then the client must check for duplicates itself before
   inserting the new item. */
struct hmapx_node *
hmapx_insert (struct hmapx *map, void *data, size_t hash) 
{
  struct hmapx_node *node = make_hmapx_node (data);
  hmap_insert (&map->hmap, &node->hmap_node, hash);
  return node;
}

/* Inserts DATA into MAP with hash value HASH and returns the new
   hmapx_node created to contain DATA.  Does not check whether
   this causes MAP's current capacity to be exceeded.  The caller
   must take responsibility for that (or use hmapx_insert()
   instead).

   This function runs in constant time.

   This function does not verify that MAP does not already
   contain a data item with the same value as DATA.  If
   duplicates should be disallowed (which is the usual case),
   then the client must check for duplicates itself before
   inserting the new node. */
struct hmapx_node *
hmapx_insert_fast (struct hmapx *map, void *data, size_t hash) 
{
  struct hmapx_node *node = make_hmapx_node (data);
  hmap_insert_fast (&map->hmap, &node->hmap_node, hash);
  return node;
}
