/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2011 Free Software Foundation, Inc.

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

/* Embedded priority queue, implemented as a heap.

   Operations have the following cost, where N is the number of
   nodes already in the heap:

        - Insert: O(lg N).

        - Find minimum: O(1).

        - Delete any node in the heap: O(lg N).

        - Change value of an node: O(lg N) in general; O(1) in
          the typically common case where the node does not
          change its position relative to other nodes.

        - Search for a node: O(N).  (Not implemented; if you need
          such a routine, use a different data structure or
          maintain a separate index.)

   A heap data structure is structured as a packed array.  If an
   array is a natural data structure for your application, then
   use the push_heap, pop_heap, make_heap, sort_heap, and is_heap
   functions declared in libpspp/array.h.  Otherwise, if your
   data structure is more dynamic, this implementation may be
   easier to use.

   An embedded heap is represented as `struct heap'.  Each node
   in the heap, presumably a structure type, must include a
   `struct heap_node' member.

   Here's an example of a structure type that includes a `struct
   heap_node':

     struct foo
       {
         struct heap_node node;   // Heap node member.
         int x;                   // Another member.
       };

   Here's an example of how to find the minimum value in such a
   heap and delete it:

     struct heap *h = ...;
     if (!heap_is_empty (h))
       {
         struct foo *foo = heap_data (heap_minimum (h), struct foo, node);
         printf ("Minimum is %d.\n", foo->x);
         heap_delete (h, &foo->node);
       }
     else
       printf ("Heap is empty.\n");
*/

#ifndef LIBPSPP_HEAP_H
#define LIBPSPP_HEAP_H 1

#include "libpspp/cast.h"
#include <stdbool.h>
#include <stddef.h>

struct pool;

/* Returns the data structure corresponding to the given heap
   NODE, assuming that NODE is embedded as the given MEMBER name
   in data type STRUCT. */
#define heap_data(NODE, STRUCT, MEMBER)                         \
        (CHECK_POINTER_HAS_TYPE (NODE, struct heap_node *),     \
         UP_CAST (NODE, STRUCT, MEMBER))

/* A node in a heap.  Opaque.
   One of these structures must be embedded in your heap node. */
struct heap_node
  {
    size_t idx;
  };

/* Returns negative if A < B, zero if A == B, positive if A > B. */
typedef int heap_compare_func (const struct heap_node *a,
                               const struct heap_node *b, const void *aux);

struct heap *heap_create (heap_compare_func *, const void *aux);
struct heap *heap_create_pool (struct pool *,
                               heap_compare_func *, const void *aux);
void heap_destroy (struct heap *);
bool heap_is_empty (const struct heap *);
size_t heap_count (const struct heap *);
void heap_moved (struct heap *, struct heap_node *);
struct heap_node *heap_minimum (const struct heap *);
void heap_insert (struct heap *, struct heap_node *);
void heap_delete (struct heap *, struct heap_node *);
void heap_changed (struct heap *, struct heap_node *);

#endif /* libpspp/heap.h */
