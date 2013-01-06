/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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

   This header (hmap.h) supplies an "embedded" implementation of
   a hash table that uses linked lists to resolve collisions
   ("separate chaining").  Its companion header (hmapx.h)
   supplies a "external" implementation that is otherwise
   similar.  The two variants are described briefly here.  The
   embedded variant, for which this is the header, is described
   in slightly more detail below.  Each function also has a
   detailed usage comment at its point of definition.  (Many of
   those definitions are inline in this file, because they are so
   simple.  Others are in hmap.c.)

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

#ifndef LIBPSPP_HMAP_H
#define LIBPSPP_HMAP_H 1

/* Embedded hash table with separate chaining.

   To create an embedded hash table, declare an instance of
   struct hmap, then initialize it with hmap_init():
     struct hmap map;
     hmap_init (&map);
   or, alternatively:
     struct hmap map = HMAP_INITIALIZER (map);
   
   Each node in the hash table, presumably a structure type, must
   include a struct hmap_node member.  Here's an example:
     struct foo
       {
         struct hmap_node node;   // hmap_node member.
         const char *string;      // Another member.
       };
   The hash table functions work with pointers to struct
   hmap_node.  To obtain a pointer to your structure type given a
   pointer to struct hmap_node, use the HMAP_DATA macro.

   Inserting and deleting elements is straightforward.  Use
   hmap_insert() to insert an element and hmap_delete() to delete
   an element, e.g.:
     struct foo my_foo;
     my_foo.string = "My string";
     hmap_insert (&map, &my_foo.node, hsh_hash_string (my_foo.string));
     ...
     hmap_delete (&map, &my_foo.node);
   You must pass the element's hash value as one of
   hmap_insert()'s arguments.  The hash table saves this hash
   value for use later to speed searches and to rehash as the
   hash table grows.

   hmap_insert() does not check whether the newly inserted
   element duplicates an element already in the hash table.  The
   client is responsible for doing so, if this is desirable.

   The hash table does not provide a direct way to search for an
   existing element.  Instead, it provides the means to iterate
   over all the elements in the hash table with a given hash
   value.  It is easy to compose a search function from such a
   building block.  For example:
     const struct foo *
     find_foo (const struct hmap *map, const char *name)
     {
       const struct foo *foo;
       size_t hash;

       hash = hsh_hash_string (name);
       HMAP_FOR_EACH_WITH_HASH (foo, struct foo, node, hash, map)
         if (!strcmp (foo->name, name))
           break;
       return foo;
     }

   Here is how to iterate through the elements currently in the
   hash table:
     struct foo *foo;
     HMAP_FOR_EACH (foo, struct foo, node, &map)
       {
         ...do something with foo...
       }
   */

#include <stdbool.h>
#include <stddef.h>
#include "libpspp/cast.h"

/* Returns the data structure corresponding to the given NODE,
   assuming that NODE is embedded as the given MEMBER name in
   data type STRUCT.  NODE must not be a null pointer. */
#define HMAP_DATA(NODE, STRUCT, MEMBER)                         \
        (CHECK_POINTER_HAS_TYPE (NODE, struct hmap_node *),     \
         UP_CAST (NODE, STRUCT, MEMBER))

/* Like HMAP_DATA, except that a null NODE yields a null pointer
   result. */
#define HMAP_NULLABLE_DATA(NODE, STRUCT, MEMBER)        \
  hmap_nullable_data__ (NODE, offsetof (STRUCT, MEMBER))

/* Hash table node. */
struct hmap_node
  {
    struct hmap_node *next;     /* Next in chain. */
    size_t hash;                /* Hash value. */
  };

static inline size_t hmap_node_hash (const struct hmap_node *);

/* Hash table. */
struct hmap
  {
    size_t count;               /* Number of inserted nodes. */
    size_t mask;                /* Number of buckets (power of 2), minus 1. */
    struct hmap_node **buckets; /* Array of buckets. */
    struct hmap_node *one;      /* One bucket, to eliminate corner cases. */
  };

/* Suitable for use as the initializer for a struct hmap named
   MAP.  Typical usage:
       struct hmap map = HMAP_INITIALIZER (map);
   HMAP_INITIALIZER() is an alternative to hmap_init(). */
#define HMAP_INITIALIZER(MAP) { 0, 0, &(MAP).one, NULL }

/* Creation and destruction. */
void hmap_init (struct hmap *);
void hmap_swap (struct hmap *, struct hmap *);
void hmap_clear (struct hmap *);
void hmap_destroy (struct hmap *);

/* Storage management. */
void hmap_reserve (struct hmap *, size_t capacity);
void hmap_shrink (struct hmap *);

/* Search.  Refer to the large comment near the top of this file
   for an example.*/
static inline struct hmap_node *hmap_first_with_hash (const struct hmap *,
                                                      size_t hash);
static inline struct hmap_node *hmap_next_with_hash (const struct hmap_node *);

/* Insertion and deletion. */
static inline void hmap_insert (struct hmap *, struct hmap_node *,
                                size_t hash);
static inline void hmap_insert_fast (struct hmap *, struct hmap_node *,
                                     size_t hash);
static inline void hmap_delete (struct hmap *, struct hmap_node *);

/* Iteration. */
static inline struct hmap_node *hmap_first (const struct hmap *);
static inline struct hmap_node *hmap_next (const struct hmap *,
                                           const struct hmap_node *);

/* Counting. */
static bool hmap_is_empty (const struct hmap *);
static inline size_t hmap_count (const struct hmap *);
static inline size_t hmap_capacity (const struct hmap *);

/* Updating data elements. */
void hmap_changed (struct hmap *, struct hmap_node *, size_t new_hash);
void hmap_moved (struct hmap *,
                 struct hmap_node *, const struct hmap_node *old);

/* Convenience macros for search.

   These macros automatically use HMAP_DATA to obtain the data
   elements that encapsulate hmap nodes, which often saves typing
   and can make code easier to read.  Refer to the large comment
   near the top of this file for an example.

   These macros evaluate HASH only once.  They evaluate their
   other arguments many times. */
#define HMAP_FIRST_WITH_HASH(STRUCT, MEMBER, HMAP, HASH)                \
  HMAP_NULLABLE_DATA (hmap_first_with_hash (HMAP, HASH), STRUCT, MEMBER)
#define HMAP_NEXT_WITH_HASH(DATA, STRUCT, MEMBER)                       \
  HMAP_NULLABLE_DATA (hmap_next_with_hash (&(DATA)->MEMBER), STRUCT, MEMBER)
#define HMAP_FOR_EACH_WITH_HASH(DATA, STRUCT, MEMBER, HASH, HMAP)       \
  for ((DATA) = HMAP_FIRST_WITH_HASH (STRUCT, MEMBER, HMAP, HASH);      \
       (DATA) != NULL;                                                  \
       (DATA) = HMAP_NEXT_WITH_HASH (DATA, STRUCT, MEMBER))
#define HMAP_FOR_EACH_WITH_HASH_SAFE(DATA, NEXT, STRUCT, MEMBER, HASH, HMAP) \
  for ((DATA) = HMAP_FIRST_WITH_HASH (STRUCT, MEMBER, HMAP, HASH);      \
       ((DATA) != NULL                                                  \
        ? ((NEXT) = HMAP_NEXT_WITH_HASH (DATA, STRUCT, MEMBER), 1)      \
        : 0);                                                           \
       (DATA) = (NEXT))

/* These macros are like the *_WITH_HASH macros above, except that they don't
   skip data elements that are in the same hash bucket but have different hash
   values.  This is a small optimization in code where comparing keys is just
   as fast as comparing hashes (e.g. the key is an "int") or comparing keys
   would duplicate comparing the hashes (e.g. the hash is the first word of a
   multi-word random key).

   These macros evaluate HASH only once.  They evaluate their
   other arguments many times. */
#define HMAP_FIRST_IN_BUCKET(STRUCT, MEMBER, HMAP, HASH)                \
  HMAP_NULLABLE_DATA (hmap_first_in_bucket (HMAP, HASH), STRUCT, MEMBER)
#define HMAP_NEXT_IN_BUCKET(DATA, STRUCT, MEMBER)                       \
  HMAP_NULLABLE_DATA (hmap_next_in_bucket (&(DATA)->MEMBER), STRUCT, MEMBER)
#define HMAP_FOR_EACH_IN_BUCKET(DATA, STRUCT, MEMBER, HASH, HMAP)       \
  for ((DATA) = HMAP_FIRST_IN_BUCKET (STRUCT, MEMBER, HMAP, HASH);      \
       (DATA) != NULL;                                                  \
       (DATA) = HMAP_NEXT_IN_BUCKET (DATA, STRUCT, MEMBER))
#define HMAP_FOR_EACH_IN_BUCKET_SAFE(DATA, NEXT, STRUCT, MEMBER, HASH, HMAP) \
  for ((DATA) = HMAP_FIRST_IN_BUCKET (STRUCT, MEMBER, HMAP, HASH);      \
       ((DATA) != NULL                                                  \
        ? ((NEXT) = HMAP_NEXT_IN_BUCKET (DATA, STRUCT, MEMBER), 1)      \
        : 0);                                                           \
       (DATA) = (NEXT))

/* Convenience macros for iteration.

   These macros automatically use HMAP_DATA to obtain the data
   elements that encapsulate hmap nodes, which often saves typing
   and can make code easier to read.  Refer to the large comment
   near the top of this file for an example.

   These macros evaluate their arguments many times. */
#define HMAP_FIRST(STRUCT, MEMBER, HMAP)                        \
  HMAP_NULLABLE_DATA (hmap_first (HMAP), STRUCT, MEMBER)
#define HMAP_NEXT(DATA, STRUCT, MEMBER, HMAP)                           \
  HMAP_NULLABLE_DATA (hmap_next (HMAP, &(DATA)->MEMBER), STRUCT, MEMBER)
#define HMAP_FOR_EACH(DATA, STRUCT, MEMBER, HMAP)       \
  for ((DATA) = HMAP_FIRST (STRUCT, MEMBER, HMAP);      \
       (DATA) != NULL;                                  \
       (DATA) = HMAP_NEXT (DATA, STRUCT, MEMBER, HMAP))
#define HMAP_FOR_EACH_SAFE(DATA, NEXT, STRUCT, MEMBER, HMAP)    \
  for ((DATA) = HMAP_FIRST (STRUCT, MEMBER, HMAP);              \
       ((DATA) != NULL                                          \
        ? ((NEXT) = HMAP_NEXT (DATA, STRUCT, MEMBER, HMAP), 1)  \
        : 0);                                                   \
       (DATA) = (NEXT))

/* Inline definitions. */

static inline struct hmap_node *hmap_find_hash__ (struct hmap_node *, size_t);
static inline struct hmap_node *hmap_first_nonempty_bucket__ (
  const struct hmap *, size_t start);
static inline size_t hmap_mask_to_capacity__ (size_t mask);

/* Returns the hash value associated with NODE. */
static inline size_t
hmap_node_hash (const struct hmap_node *node) 
{
  return node->hash;
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
   hmap_capacity().  Calls to hmap_insert(), hmap_reserve(), and
   hmap_shrink() can change the capacity of a hash map.
   Inserting a node with hmap_insert_fast() or deleting one with
   hmap_delete() will not change the relative ordering of nodes.

   The HMAP_FOR_EACH_WITH_HASH and HMAP_FOR_EACH_WITH_HASH_SAFE
   macros provide convenient ways to iterate over all the nodes
   with a given hash.  The HMAP_FIRST_WITH_HASH macro is an
   interface to this particular function that is often more
   convenient. */
static inline struct hmap_node *
hmap_first_with_hash (const struct hmap *map, size_t hash)
{
  return hmap_find_hash__ (map->buckets[hash & map->mask], hash);
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
   hmap_capacity().  Calls to hmap_insert(), hmap_reserve(), and
   hmap_shrink() can change the capacity of a hash map.
   Inserting a node with hmap_insert_fast() or deleting one with
   hmap_delete() will not change the relative ordering of nodes.

   The HMAP_FOR_EACH_WITH_HASH and HMAP_FOR_EACH_WITH_HASH_SAFE
   macros provide convenient ways to iterate over all the nodes
   with a given hash.  The HMAP_NEXT_WITH_HASH macro is an
   interface to this particular function that is often more
   convenient. */
static inline struct hmap_node *
hmap_next_with_hash (const struct hmap_node *node) 
{
  return hmap_find_hash__ (node->next, node->hash);
}

/* Inserts NODE into MAP with hash value HASH.  If the insertion
   causes MAP's current capacity, as reported by hmap_capacity(),
   to be exceeded, rehashes MAP with an increased number of hash
   buckets.

   This function runs in constant time amortized over all the
   insertions into MAP.

   This function does not verify that MAP does not already
   contain a data item with the same value as NODE.  If
   duplicates should be disallowed (which is the usual case),
   then the client must check for duplicates itself before
   inserting the new node. */
static inline void
hmap_insert (struct hmap *map, struct hmap_node *node, size_t hash)
{
  hmap_insert_fast (map, node, hash);
  if (map->count > hmap_capacity (map))
    hmap_reserve (map, map->count);
}

/* Inserts NODE into MAP with hash value HASH.  Does not check
   whether this causes MAP's current capacity to be exceeded.
   The caller must take responsibility for that (or use
   hmap_insert() instead).

   This function runs in constant time.

   This function does not verify that MAP does not already
   contain a data item with the same value as NODE.  If
   duplicates should be disallowed (which is the usual case),
   then the client must check for duplicates itself before
   inserting the new node. */
static inline void
hmap_insert_fast (struct hmap *map, struct hmap_node *node, size_t hash) 
{
  struct hmap_node **bucket = &map->buckets[hash & map->mask];
  node->hash = hash;
  node->next = *bucket;
  *bucket = node;
  map->count++;
}

/* Returns the first node in MAP in the bucket for HASH, or a null pointer if
   that bucket in HASH is empty.

   This function runs in constant time.

   Nodes are returned in arbitrary order that may change whenever the hash
   table's current capacity changes, as reported by hmap_capacity().  Calls to
   hmap_insert(), hmap_reserve(), and hmap_shrink() can change the capacity of
   a hash map.  Inserting a node with hmap_insert_fast() or deleting one with
   hmap_delete() will not change the relative ordering of nodes.

   The HMAP_FOR_EACH_IN_BUCKET and HMAP_FOR_EACH_IN_BUCKET_SAFE macros provide
   convenient ways to iterate over all the nodes with a given hash.  The
   HMAP_FIRST_IN_BUCKET macro is an interface to this particular function that
   is often more convenient. */
static inline struct hmap_node *
hmap_first_in_bucket (const struct hmap *map, size_t hash)
{
  return map->buckets[hash & map->mask];
}

/* Returns the next node following NODE within the same bucket, or a null
   pointer if NODE is the last node in its bucket.

   This function runs in constant time.

   Nodes are returned in arbitrary order that may change whenever the hash
   table's current capacity changes, as reported by hmap_capacity().  Calls to
   hmap_insert(), hmap_reserve(), and hmap_shrink() can change the capacity of
   a hash map.  Inserting a node with hmap_insert_fast() or deleting one with
   hmap_delete() will not change the relative ordering of nodes.

   The HMAP_FOR_EACH_IN_BUCKET and HMAP_FOR_EACH_IN_BUCKET_SAFE macros provide
   convenient ways to iterate over all the nodes with a given hash.  The
   HMAP_NEXT_IN_BUCKET macro is an interface to this particular function that
   is often more convenient. */
static inline struct hmap_node *
hmap_next_in_bucket (const struct hmap_node *node)
{
  return node->next;
}

/* Removes NODE from MAP.  The client is responsible for freeing
   any data associated with NODE, if necessary.

   Assuming uniform hashing, this function runs in constant time.
   (Its runtime is proportional to the position of NODE in its
   hash chain, so given a pathological hash function, e.g. one
   that returns a constant value, its runtime degenerates to
   linear in the length of NODE's hash chain.)

   This function never reduces the number of buckets in MAP.
   When one deletes a large number of nodes from a hash table,
   calling hmap_shrink() afterward may therefore save a small
   amount of memory.  It is also more expensive to iterate
   through a very sparse hash table than a denser one, so
   shrinking the hash table could also save some time.  However,
   rehashing has an immediate cost that must be weighed against
   these benefits.

   hmap_delete() does not change NODE's hash value reported by
   hmap_node_hash(). */
static inline void
hmap_delete (struct hmap *map, struct hmap_node *node)
{
  struct hmap_node **bucket = &map->buckets[node->hash & map->mask];
  while (*bucket != node)
    bucket = &(*bucket)->next;
  *bucket = (*bucket)->next;
  map->count--;
}

/* Returns the first node in MAP, or a null pointer if MAP is
   empty.

   Amortized over iterating through every data element in MAP,
   this function runs in constant time.  However, this assumes
   that MAP is not excessively sparse, that is, that
   hmap_capacity(MAP) is at most a constant factor greater than
   hmap_count(MAP).  This will always be true unless many nodes
   have been inserted into MAP and then most or all of them
   deleted; in such a case, calling hmap_shrink() is advised.

   Nodes are returned in arbitrary order that may change whenever
   the hash table's current capacity changes, as reported by
   hmap_capacity().  Calls to hmap_insert(), hmap_reserve(), and
   hmap_shrink() can change the capacity of a hash map.
   Inserting a node with hmap_insert_fast() or deleting one with
   hmap_delete() will not change the relative ordering of nodes.

   The HMAP_FOR_EACH and HMAP_FOR_EACH_SAFE macros provide
   convenient ways to iterate over all the nodes in a hash map.
   The HMAP_FIRST macro is an interface to this particular
   function that is often more convenient. */
static inline struct hmap_node *
hmap_first (const struct hmap *map) 
{
  return hmap_first_nonempty_bucket__ (map, 0);
}

/* Returns the next node in MAP following NODE, or a null pointer
   if NODE is the last node in MAP.

   Amortized over iterating through every data element in MAP,
   this function runs in constant time.  However, this assumes
   that MAP is not excessively sparse, that is, that
   hmap_capacity(MAP) is at most a constant factor greater than
   hmap_count(MAP).  This will always be true unless many nodes
   have been inserted into MAP and then most or all of them
   deleted; in such a case, calling hmap_shrink() is advised.

   Nodes are returned in arbitrary order that may change whenever
   the hash table's current capacity changes, as reported by
   hmap_capacity().  Calls to hmap_insert(), hmap_reserve(), and
   hmap_shrink() can change the capacity of a hash map.
   Inserting a node with hmap_insert_fast() or deleting one with
   hmap_delete() will not change the relative ordering of nodes.

   The HMAP_FOR_EACH and HMAP_FOR_EACH_SAFE macros provide
   convenient ways to iterate over all the nodes in a hash map.
   The HMAP_NEXT macro is an interface to this particular
   function that is often more convenient. */
static inline struct hmap_node *
hmap_next (const struct hmap *map, const struct hmap_node *node) 
{
  return (node->next != NULL
          ? node->next
          : hmap_first_nonempty_bucket__ (map, (node->hash & map->mask) + 1));
}

/* Returns true if MAP currently contains no data items, false
   otherwise. */
static inline bool
hmap_is_empty (const struct hmap *map)
{
  return map->count == 0;
}

/* Returns the number of data items currently in MAP. */
static inline size_t
hmap_count (const struct hmap *map) 
{
  return map->count;
}

/* Returns the current capacity of MAP, that is, the maximum
   number of data elements that MAP may hold before it becomes
   advisable to rehash.

   The capacity is advisory only: it is possible to insert any
   number of data elements into a hash map regardless of its
   capacity.  However, inserting many more elements than the
   map's capacity will degrade search performance. */
static inline size_t
hmap_capacity (const struct hmap *map) 
{
  return hmap_mask_to_capacity__ (map->mask);
}

/* Implementation details. */

/* Returns the first node at or after NODE in NODE's chain that
   has hash value HASH. */
static inline struct hmap_node *
hmap_find_hash__ (struct hmap_node *node, size_t hash) 
{
  for (; node != NULL; node = node->next) 
    if (node->hash == hash)
      break;
  return node;
}

/* Returns the first node in the lowest-numbered nonempty bucket
   in MAP whose index is START or higher, or a null pointer if
   all such buckets are empty. */
static inline struct hmap_node *
hmap_first_nonempty_bucket__ (const struct hmap *map, size_t start)
{
  size_t i;

  for (i = start; i <= map->mask; i++)
    if (map->buckets[i] != NULL)
      return map->buckets[i];
  return NULL;
}

/* Returns the hash table capacity associated with a given MASK,
   which should be a value for the "mask" member of struct hmap.
   MASK must be a power of 2 minus 1 (including 0), that is, its
   value in binary must be all 1-bits.  */
static inline size_t
hmap_mask_to_capacity__ (size_t mask) 
{
  return (mask + 1) * 2;
}

/* Helper for HMAP_NULLABLE_DATA (to avoid evaluating its NODE
   argument more than once).  */
static inline void *
hmap_nullable_data__ (struct hmap_node *node, size_t member_offset)
{ 
  return node != NULL ? (char *) node - member_offset : NULL;
}

#endif /* libpspp/hmap.h */
