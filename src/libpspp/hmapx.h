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

/* Hash table with separate chaining.

   This header (hmapx.h) supplies an "external" implementation of
   a hash table that uses linked lists to resolve collisions
   ("separate chaining").  Its companion header (hmap.h) supplies
   a "embedded" implementation that is otherwise similar.  The
   two variants are described briefly here.  The external
   variant, for which this is the header, is described in
   slightly more detail below.  Each function also has a detailed
   usage comment at its point of definition.  (Many of those
   definitions are inline in this file, because they are so
   simple.  Others are in hmapx.c.)

   The "hmap" embedded hash table implementation puts the hash
   table node (which includes the linked list used for resolving
   collisions) within the data structure that the hash table
   contains.  This makes allocation efficient, in space and time,
   because no additional call into an allocator is needed to
   obtain memory for the hash table node.  It also makes it easy
   to find the hash table node associated with a given object.
   However, it's difficult to include a given object in an
   arbitrary number of hash tables.

   The "hmapx" external hash table implementation allocates hash
   table nodes separately from the objects in the hash table.
   Inserting and removing hash table elements requires dynamic
   allocation, so it is normally slower and takes more memory
   than the embedded implementation.  It also requires searching
   the table to find the node associated with a given object.
   However, it's easy to include a given object in an arbitrary
   number of hash tables.  It's also possible to create an
   external hash table without adding a member to the data
   structure that the hash table contains. */

#ifndef LIBPSPP_HMAPX_H
#define LIBPSPP_HMAPX_H 1

/* External hash table with separate chaining.

   To create an external hash table, declare an instance of
   struct hmapx, then initialize it with hmapx_init():
     struct hmapx map;
     hmapx_init (&map);
   or, alternatively:
     struct hmapx map = HMAPX_INITIALIZER (map);

   An hmapx data structure contains data represented as void *.
   The hmapx_insert() function inserts such a datum and returns
   the address of a newly created struct hmapx_node that
   represents the new element:
     struct foo {
       const char *key;
       const char *value;
     };
     struct foo foo = {"key", "value"};
     struct hmapx_node *node;
     node = hmapx_insert (&map, &foo, hsh_hash_string (foo.key));
   The element's hash value must be passed as one of
   hmapx_insert()'s arguments.  The hash table saves this hash
   value for use later to speed searches and to rehash as the
   hash table grows.

   hmapx_insert() does not check whether the newly inserted
   element duplicates an element already in the hash table.  The
   client is responsible for doing so, if this is desirable.

   Use hmapx_delete() to delete an element from the hash table,
   passing in its hmapx_node:
     hmapx_delete (&map, node);
   Deleting an element frees its node.

   The hash table does not provide a direct way to search for an
   existing element.  Instead, it provides the means to iterate
   over all the elements in the hash table with a given hash
   value.  It is easy to compose a search function from such a
   building block.  For example:
     struct hmapx_node *
     find_node (const struct hmapx *map, const char *target)
     {
       struct hmapx_node *node;
       struct foo *foo;
       HMAPX_FOR_EACH_WITH_HASH (foo, node, hsh_hash_string (target), map)
         if (!strcmp (foo->key, target))
           break;
       return node;
     }
   This function's client can extract the data item from the
   returned hmapx_node using the hmapx_node_data() function.  The
   hmapx_node can also be useful directly as an argument to other
   hmapx functions, such as hmapx_delete().

   Here is how to iterate through the elements currently in the
   hash table:
     struct hmapx_node *node;
     const char *string;
     HMAPX_FOR_EACH (data, node, &map)
       {
         ...do something with string...
       }
   */

#include "libpspp/hmap.h"
#include <stdlib.h>

/* Hash table node. */
struct hmapx_node
  {
    struct hmap_node hmap_node; /* Underlying hash node. */
    void *data;                 /* User data. */
  };

static inline void *hmapx_node_data (const struct hmapx_node *);
static inline size_t hmapx_node_hash (const struct hmapx_node *);

/* Hash table. */
struct hmapx
  {
    struct hmap hmap;
  };

/* Suitable for use as the initializer for a struct hmapx named
   MAP.  Typical usage:
       struct hmap map = HMAPX_INITIALIZER (map);
   HMAPX_INITIALIZER() is an alternative to hmapx_init(). */
#define HMAPX_INITIALIZER(MAP) { HMAP_INITIALIZER (MAP.hmap) }

/* Creation and destruction. */
static inline void hmapx_init (struct hmapx *);
static inline void hmapx_swap (struct hmapx *, struct hmapx *);
void hmapx_clear (struct hmapx *);
void hmapx_destroy (struct hmapx *);

/* Storage management. */
static inline void hmapx_reserve (struct hmapx *, size_t capacity);
static inline void hmapx_shrink (struct hmapx *);

/* Search. */
static inline struct hmapx_node *hmapx_first_with_hash (struct hmapx *,
                                                        size_t hash);
static inline struct hmapx_node *hmapx_next_with_hash (struct hmapx_node *);

/* Insertion and deletion. */
struct hmapx_node *hmapx_insert (struct hmapx *, void *, size_t hash);
struct hmapx_node *hmapx_insert_fast (struct hmapx *, void *, size_t hash);
static inline void hmapx_delete (struct hmapx *, struct hmapx_node *);

/* Iteration. */
static inline struct hmapx_node *hmapx_first (const struct hmapx *);
static inline struct hmapx_node *hmapx_next (const struct hmapx *,
                                             const struct hmapx_node *);

/* Counting. */
static inline bool hmapx_is_empty (const struct hmapx *);
static inline size_t hmapx_count (const struct hmapx *);
static inline size_t hmapx_capacity (const struct hmapx *);

/* Updating data elements. */
static inline void hmapx_change (struct hmapx *,
                                 struct hmapx_node *, void *, size_t new_hash);
static inline void hmapx_changed (struct hmapx *, struct hmapx_node *,
                                  size_t new_hash);
static inline void hmapx_move (struct hmapx_node *, void *);

/* Convenience macros for search.

   These macros automatically use hmapx_node_data() to obtain the
   data elements that encapsulate hmap nodes, which often saves
   typing and can make code easier to read.  Refer to the large
   comment near the top of this file for an example.

   These macros evaluate HASH only once.  They evaluate their
   other arguments many times. */
#define HMAPX_FOR_EACH_WITH_HASH(DATA, NODE, HASH, HMAPX)               \
  for ((NODE) = hmapx_first_with_hash (HMAPX, HASH);                    \
       (NODE) != NULL ? ((DATA) = hmapx_node_data (NODE), 1) : 0;       \
       (NODE) = hmapx_next_with_hash (NODE))
#define HMAPX_FOR_EACH_WITH_HASH_SAFE(DATA, NODE, NEXT, HASH, HMAPX)    \
  for ((NODE) = hmapx_first_with_hash (HMAPX, HASH);                    \
       ((NODE) != NULL                                                  \
        ? ((DATA) = hmapx_node_data (NODE),                             \
           (NEXT) = hmapx_next_with_hash (NODE),                        \
           1)                                                           \
        : 0);                                                           \
       (NODE) = (NEXT))

/* Convenience macros for iteration.

   These macros automatically use hmapx_node_data() to obtain the
   data elements that encapsulate hmap nodes, which often saves
   typing and can make code easier to read.  Refer to the large
   comment near the top of this file for an example. 

   These macros evaluate their arguments many times. */
#define HMAPX_FOR_EACH(DATA, NODE, HMAPX)                               \
  for ((NODE) = hmapx_first (HMAPX);                                    \
       (NODE) != NULL ? ((DATA) = hmapx_node_data (NODE), 1) : 0;       \
       (NODE) = hmapx_next (HMAPX, NODE))
#define HMAPX_FOR_EACH_SAFE(DATA, NODE, NEXT, HMAPX)                    \
  for ((NODE) = hmapx_first (HMAPX);                                    \
       ((NODE) != NULL                                                  \
        ? ((DATA) = hmapx_node_data (NODE),                             \
           (NEXT) = hmapx_next (HMAPX, NODE),                           \
           1)                                                           \
        : 0);                                                           \
       (NODE) = (NEXT))

/* Inline definitions. */

/* Returns the data stored in NODE. */
static inline void *
hmapx_node_data (const struct hmapx_node *node)
{
  return node->data;
}

/* Returns the hash value stored in NODE */
static inline size_t
hmapx_node_hash (const struct hmapx_node *node)
{
  return hmap_node_hash (&node->hmap_node);
}

/* Initializes MAP as a new hash map that is initially empty. */
static inline void
hmapx_init (struct hmapx *map) 
{
  hmap_init (&map->hmap);
}

/* Exchanges the contents of hash maps A and B. */
static inline void
hmapx_swap (struct hmapx *a, struct hmapx *b)
{
  hmap_swap (&a->hmap, &b->hmap);
}

/* Ensures that MAP has sufficient space to store at least
   CAPACITY data elements, allocating a new set of buckets and
   rehashing if necessary. */
static inline void
hmapx_reserve (struct hmapx *map, size_t capacity)
{
  hmap_reserve (&map->hmap, capacity);
}

/* Shrinks MAP's set of buckets to the minimum number needed to
   store its current number of elements, allocating a new set of
   buckets and rehashing if that would save space. */
static inline void
hmapx_shrink (struct hmapx *map) 
{
  hmap_shrink (&map->hmap);
}

/* Returns the first node in MAP that has hash value HASH, or a
   null pointer if MAP does not contain any node with that hash
   value.

   Assuming uniform hashing and no duplicate data items in MAP,
   this function runs in constant time.  (Amortized over an
   iteration over all data items with a given HASH, its runtime
   is proportional to the length of the hash chain for HASH, so
   given a pathological hash function, e.g. one that returns a
   constant value, its runtime degenerates to linear in the
   length of NODE's hash chain.)

   Nodes are returned in arbitrary order that may change whenever
   the hash table's current capacity changes, as reported by
   hmapx_capacity().  Calls to hmapx_insert(), hmapx_reserve(),
   and hmapx_shrink() can change the capacity of a hash map.
   Inserting a node with hmapx_insert_fast() or deleting one with
   hmapx_delete() will not change the relative ordering of nodes.

   The HMAPX_FOR_EACH_WITH_HASH and HMAPX_FOR_EACH_WITH_HASH_SAFE
   macros provide convenient ways to iterate over all the nodes
   with a given hash. */
static inline struct hmapx_node *
hmapx_first_with_hash (struct hmapx *map, size_t hash) 
{
  return HMAP_FIRST_WITH_HASH (struct hmapx_node, hmap_node, &map->hmap, hash);
}

/* Returns the next node in MAP after NODE that has the same hash
   value as NODE, or a null pointer if MAP does not contain any
   more nodes with that hash value.

   Assuming uniform hashing and no duplicate data items in MAP,
   this function runs in constant time.  (Amortized over an
   iteration over all data items with a given HASH, its runtime
   is proportional to the length of the hash chain for HASH, so
   given a pathological hash function, e.g. one that returns a
   constant value, its runtime degenerates to linear in the
   length of NODE's hash chain.)

   Nodes are returned in arbitrary order that may change whenever
   the hash table's current capacity changes, as reported by
   hmapx_capacity().  Calls to hmapx_insert(), hmapx_reserve(),
   and hmapx_shrink() can change the capacity of a hash map.
   Inserting a node with hmapx_insert_fast() or deleting one with
   hmapx_delete() will not change the relative ordering of nodes.

   The HMAPX_FOR_EACH_WITH_HASH and HMAPX_FOR_EACH_WITH_HASH_SAFE
   macros provide convenient ways to iterate over all the nodes
   with a given hash. */
static inline struct hmapx_node *
hmapx_next_with_hash (struct hmapx_node *node) 
{
  return HMAP_NEXT_WITH_HASH (node, struct hmapx_node, hmap_node);
}

/* Removes NODE from MAP and frees NODE.  The client is
   responsible for freeing the user data associated with NODE, if
   appropriate.

   Assuming uniform hashing, this function runs in constant time.
   (Its runtime is proportional to the position of NODE in its
   hash chain, so given a pathological hash function, e.g. one
   that returns a constant value, its runtime degenerates to
   linear in the length of NODE's hash chain.)

   This function never reduces the number of buckets in MAP.
   When one deletes a large number of nodes from a hash table,
   calling hmapx_shrink() afterward may therefore save a small
   amount of memory.  It is also more expensive to iterate
   through a very sparse hash table than a denser one, so
   shrinking the hash table could also save some time.  However,
   rehashing has an immediate cost that must be weighed against
   these benefits.

   hmapx_delete() does not change NODE's hash value reported by
   hmapx_node_hash(). */
static inline void
hmapx_delete (struct hmapx *map, struct hmapx_node *node) 
{
  hmap_delete (&map->hmap, &node->hmap_node);
  free (node);
}

/* Returns the first node in MAP, or a null pointer if MAP is
   empty.

   Amortized over iterating through every data element in MAP,
   this function runs in constant time.  However, this assumes
   that MAP is not excessively sparse, that is, that
   hmapx_capacity(MAP) is at most a constant factor greater than
   hmapx_count(MAP).  This will always be true unless many nodes
   have been inserted into MAP and then most or all of them
   deleted; in such a case, calling hmapx_shrink() is advised.

   Nodes are returned in arbitrary order that may change whenever
   the hash table's current capacity changes, as reported by
   hmapx_capacity().  Calls to hmapx_insert(), hmapx_reserve(),
   and hmapx_shrink() can change the capacity of a hash map.
   Inserting a node with hmapx_insert_fast() or deleting one with
   hmapx_delete() will not change the relative ordering of nodes.

   The HMAPX_FOR_EACH and HMAPX_FOR_EACH_SAFE macros provide
   convenient ways to iterate over all the nodes in a hash
   map. */
static inline struct hmapx_node *
hmapx_first (const struct hmapx *map) 
{
  return HMAP_FIRST (struct hmapx_node, hmap_node, &map->hmap);
}

/* Returns the next node in MAP following NODE, or a null pointer
   if NODE is the last node in MAP.

   Amortized over iterating through every data element in MAP,
   this function runs in constant time.  However, this assumes
   that MAP is not excessively sparse, that is, that
   hmapx_capacity(MAP) is at most a constant factor greater than
   hmapx_count(MAP).  This will always be true unless many nodes
   have been inserted into MAP and then most or all of them
   deleted; in such a case, calling hmapx_shrink() is advised.

   Nodes are returned in arbitrary order that may change whenever
   the hash table's current capacity changes, as reported by
   hmapx_capacity().  Calls to hmapx_insert(), hmapx_reserve(),
   and hmapx_shrink() can change the capacity of a hash map.
   Inserting a node with hmapx_insert_fast() or deleting one with
   hmapx_delete() will not change the relative ordering of nodes.

   The HMAPX_FOR_EACH and HMAPX_FOR_EACH_SAFE macros provide
   convenient ways to iterate over all the nodes in a hash
   map. */
static inline struct hmapx_node *
hmapx_next (const struct hmapx *map, const struct hmapx_node *node) 
{
  return HMAP_NEXT (node, struct hmapx_node, hmap_node, &map->hmap);
}

/* Returns true if MAP currently contains no data items, false
   otherwise. */
static inline bool
hmapx_is_empty (const struct hmapx *map)
{
  return hmap_is_empty (&map->hmap);
}

/* Returns the number of data items currently in MAP. */
static inline size_t
hmapx_count (const struct hmapx *map) 
{
  return hmap_count (&map->hmap);
}

/* Returns the current capacity of MAP, that is, the maximum
   number of data elements that MAP may hold before it becomes
   advisable to rehash.

   The capacity is advisory only: it is possible to insert any
   number of data elements into a hash map regardless of its
   capacity.  However, inserting many more elements than the
   map's capacity will degrade search performance. */
static inline size_t
hmapx_capacity (const struct hmapx *map) 
{
  return hmap_capacity (&map->hmap);
}

/* Changes NODE's data to DATA and its hash value to NEW_HASH.
   NODE must reside in MAP.

   This function does not verify that MAP does not already
   contain a data item that duplicates DATA.  If duplicates
   should be disallowed (which is the usual case), then the
   client must check for duplicates before changing NODE's
   value. */
static inline void
hmapx_change (struct hmapx *map,
              struct hmapx_node *node, void *data, size_t new_hash) 
{
  hmapx_move (node, data);
  hmapx_changed (map, node, new_hash);
}

/* Moves NODE around in MAP to compensate for its hash value
   having changed to NEW_HASH.

   This function does not verify that MAP does not already
   contain a data item that duplicates the new value of NODE's
   data.  If duplicates should be disallowed (which is the usual
   case), then the client must check for duplicates before
   changing NODE's value. */
static inline void
hmapx_changed (struct hmapx *map, struct hmapx_node *node, size_t new_hash) 
{
  hmap_changed (&map->hmap, &node->hmap_node, new_hash);
}

/* Updates NODE to compensate for its data item having moved
   around in memory to new location DATA.  The data item's value
   and hash value should not have changed.  (If they have
   changed, call hmapx_change() instead.) */
static inline void
hmapx_move (struct hmapx_node *node, void *data)
{
  node->data = data;
}

#endif /* libpspp/hmapx.h */
