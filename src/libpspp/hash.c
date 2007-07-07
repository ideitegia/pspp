/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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
#include <stdbool.h>
#include "hash.h"
#include "message.h"
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include "array.h"
#include "alloc.h"
#include "compiler.h"
#include "misc.h"
#include "str.h"
#include "pool.h"


#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Note for constructing hash functions:

   You can store the hash values in the records, then compare hash
   values (in the compare function) before bothering to compare keys.
   Hash values can simply be returned from the records instead of
   recalculating when rehashing. */

/* Debugging note:

   Since hash_probe and hash_find take void * pointers, it's easy to
   pass a void ** to your data by accidentally inserting an `&'
   reference operator where one shouldn't go.  It took me an hour to
   hunt down a bug like that once. */

/* Prime numbers and hash functions. */

/* Returns smallest power of 2 greater than X. */
static size_t
next_power_of_2 (size_t x)
{
  assert (x != 0);

  for (;;)
    {
      /* Turn off rightmost 1-bit in x. */
      size_t y = x & (x - 1);

      /* If y is 0 then x only had a single 1-bit. */
      if (y == 0)
        return 2 * x;

      /* Otherwise turn off the next. */
      x = y;
    }
}

/* Fowler-Noll-Vo hash constants, for 32-bit word sizes. */
#define FNV_32_PRIME 16777619u
#define FNV_32_BASIS 2166136261u

/* Fowler-Noll-Vo 32-bit hash, for bytes. */
unsigned
hsh_hash_bytes (const void *buf_, size_t size)
{
  const unsigned char *buf = (const unsigned char *) buf_;
  unsigned hash;

  assert (buf != NULL);

  hash = FNV_32_BASIS;
  while (size-- > 0)
    hash = (hash * FNV_32_PRIME) ^ *buf++;

  return hash;
}

/* Fowler-Noll-Vo 32-bit hash, for strings. */
unsigned
hsh_hash_string (const char *s_)
{
  const unsigned char *s = (const unsigned char *) s_;
  unsigned hash;

  assert (s != NULL);

  hash = FNV_32_BASIS;
  while (*s != '\0')
    hash = (hash * FNV_32_PRIME) ^ *s++;

  return hash;
}

/* Fowler-Noll-Vo 32-bit hash, for case-insensitive strings. */
unsigned
hsh_hash_case_string (const char *s_)
{
  const unsigned char *s = (const unsigned char *) s_;
  unsigned hash;

  assert (s != NULL);

  hash = FNV_32_BASIS;
  while (*s != '\0')
    hash = (hash * FNV_32_PRIME) ^ toupper (*s++);

  return hash;
}

/* Hash for ints. */
unsigned
hsh_hash_int (int i)
{
  return hsh_hash_bytes (&i, sizeof i);
}

/* Hash for double. */
unsigned
hsh_hash_double (double d)
{
  if (!isnan (d))
    return hsh_hash_bytes (&d, sizeof d);
  else
    return 0;
}

/* Hash tables. */

/* Hash table. */
struct hsh_table
  {
    size_t used;                /* Number of filled entries. */
    size_t size;                /* Number of entries (a power of 2). */
    void **entries;		/* Hash table proper. */

    const void *aux;            /* Auxiliary data for comparison functions. */
    hsh_compare_func *compare;
    hsh_hash_func *hash;
    hsh_free_func *free;

#ifndef NDEBUG
    /* Set to false if hsh_data() or hsh_sort() has been called,
       so that most hsh_*() functions may no longer be called. */
    bool hash_ordered;
#endif

    struct pool *pool;         /* The pool used for this hash table */
  };

struct hsh_table *
hsh_create (int size, hsh_compare_func *compare, hsh_hash_func *hash,
            hsh_free_func *free, const void *aux)
{
  return hsh_create_pool (NULL, size, compare, hash, free, aux);
}



/* Creates a hash table with at least M entries.  COMPARE is a
   function that compares two entries and returns 0 if they are
   identical, nonzero otherwise; HASH returns a nonnegative hash value
   for an entry; FREE destroys an entry. */
struct hsh_table *
hsh_create_pool (struct pool *pool, int size,
		 hsh_compare_func *compare, hsh_hash_func *hash,
		 hsh_free_func *free, const void *aux)
{
  struct hsh_table *h;
  int i;

  assert (compare != NULL);
  assert (hash != NULL);

  h = pool_malloc (pool, sizeof *h);
  h->pool = pool;
  h->used = 0;
  if (size < 4)
    size = 4;
  h->size = next_power_of_2 (size);
  h->entries = pool_nmalloc (pool, h->size, sizeof *h->entries);
  for (i = 0; i < h->size; i++)
    h->entries[i] = NULL;
  h->aux = aux;
  h->compare = compare;
  h->hash = hash;
  h->free = free;
#ifndef NDEBUG
  h->hash_ordered = true;
#endif
  return h;
}

/* Destroys the contents of table H. */
void
hsh_clear (struct hsh_table *h)
{
  int i;

  assert (h != NULL);
  if (h->free)
    for (i = 0; i < h->size; i++)
      if (h->entries[i] != NULL)
        h->free (h->entries[i], h->aux);

  for (i = 0; i < h->size; i++)
    h->entries[i] = NULL;

  h->used = 0;

#ifndef NDEBUG
  h->hash_ordered = true;
#endif
}

/* Destroys table H and all its contents. */
void
hsh_destroy (struct hsh_table *h)
{
  int i;

  if (h != NULL)
    {
      if (h->free)
        for (i = 0; i < h->size; i++)
          if (h->entries[i] != NULL)
            h->free (h->entries[i], h->aux);
      pool_free (h->pool, h->entries);
      pool_free (h->pool, h);
    }
}

/* Locates an entry matching TARGET.  Returns the index for the
   entry, if found, or the index of an empty entry that indicates
   where TARGET should go, otherwise. */
static inline unsigned
locate_matching_entry (struct hsh_table *h, const void *target)
{
  unsigned i = h->hash (target, h->aux);

  assert (h->hash_ordered);
  for (;;)
    {
      void *entry;
      i &= h->size - 1;
      entry = h->entries[i];
      if (entry == NULL || !h->compare (entry, target, h->aux))
	return i;
      i--;
    }
}

/* Returns the index of an empty entry that indicates
   where TARGET should go, assuming that TARGET is not equal to
   any item already in the hash table. */
static inline unsigned
locate_empty_entry (struct hsh_table *h, const void *target)
{
  unsigned i = h->hash (target, h->aux);

  assert (h->hash_ordered);
  for (;;)
    {
      i &= h->size - 1;
      if (h->entries[i] == NULL)
	return i;
      i--;
    }
}

/* Changes the capacity of H to NEW_SIZE, which must be a
   positive power of 2 at least as large as the number of
   elements in H. */
static void
rehash (struct hsh_table *h, size_t new_size)
{
  void **begin, **end, **table_p;
  int i;

  assert (h != NULL);
  assert (new_size >= h->used);

  /* Verify that NEW_SIZE is a positive power of 2. */
  assert (new_size > 0 && (new_size & (new_size - 1)) == 0);

  begin = h->entries;
  end = begin + h->size;

  h->size = new_size;
  h->entries = pool_nmalloc (h->pool, h->size, sizeof *h->entries);
  for (i = 0; i < h->size; i++)
    h->entries[i] = NULL;
  for (table_p = begin; table_p < end; table_p++)
    {
      void *entry = *table_p;
      if (entry != NULL)
        h->entries[locate_empty_entry (h, entry)] = entry;
    }
  pool_free (h->pool, begin);

#ifndef NDEBUG
  h->hash_ordered = true;
#endif
}

/* A "algo_predicate_func" that returns true if DATA points
   to a non-null void. */
static bool
not_null (const void *data_, const void *aux UNUSED)
{
  void *const *data = data_;

  return *data != NULL;
}

/* Compacts hash table H and returns a pointer to its data.  The
   returned data consists of hsh_count(H) non-null pointers, in
   no particular order, followed by a null pointer.

   After calling this function, only hsh_destroy() and
   hsh_count() should be applied to H.  hsh_first() and
   hsh_next() could also be used, but you're better off just
   iterating through the returned array.

   This function is intended for use in situations where data
   processing occurs in two phases.  In the first phase, data is
   added, removed, and searched for within a hash table.  In the
   second phase, the contents of the hash table are output and
   the hash property itself is no longer of interest.

   Use hsh_sort() instead, if the second phase wants data in
   sorted order.  Use hsh_data_copy() or hsh_sort_copy() instead,
   if the second phase still needs to search the hash table. */
void *const *
hsh_data (struct hsh_table *h)
{
  size_t n;

  assert (h != NULL);
  n = partition (h->entries, h->size, sizeof *h->entries, not_null, NULL);
  assert (n == h->used);
#ifndef NDEBUG
  h->hash_ordered = false;
#endif
  return h->entries;
}

/* Dereferences void ** pointers and passes them to the hash
   comparison function. */
static int
comparison_helper (const void *a_, const void *b_, const void *h_)
{
  void *const *a = a_;
  void *const *b = b_;
  const struct hsh_table *h = h_;

  assert(a);
  assert(b);

  return h->compare (*a, *b, h->aux);
}

/* Sorts hash table H based on hash comparison function.  The
   returned data consists of hsh_count(H) non-null pointers,
   sorted in order of the hash comparison function, followed by a
   null pointer.

   After calling this function, only hsh_destroy() and
   hsh_count() should be applied to H.  hsh_first() and
   hsh_next() could also be used, but you're better off just
   iterating through the returned array.

   This function is intended for use in situations where data
   processing occurs in two phases.  In the first phase, data is
   added, removed, and searched for within a hash table.  In the
   second phase, the contents of the hash table are output and
   the hash property itself is no longer of interest.

   Use hsh_data() instead, if the second phase doesn't need the
   data in any particular order.  Use hsh_data_copy() or
   hsh_sort_copy() instead, if the second phase still needs to
   search the hash table. */
void *const *
hsh_sort (struct hsh_table *h)
{
  assert (h != NULL);

  hsh_data (h);
  sort (h->entries, h->used, sizeof *h->entries, comparison_helper, h);
  return h->entries;
}

/* Makes and returns a copy of the pointers to the data in H.
   The returned data consists of hsh_count(H) non-null pointers,
   in no particular order, followed by a null pointer.  The hash
   table is not modified.  The caller is responsible for freeing
   the allocated data.

   If you don't need to search or modify the hash table, then
   hsh_data() is a more efficient choice. */
void **
hsh_data_copy (struct hsh_table *h)
{
  void **copy;

  assert (h != NULL);
  copy = pool_nmalloc (h->pool, (h->used + 1), sizeof *copy);
  copy_if (h->entries, h->size, sizeof *h->entries, copy, not_null, NULL);
  copy[h->used] = NULL;
  return copy;
}

/* Makes and returns a copy of the pointers to the data in H.
   The returned data consists of hsh_count(H) non-null pointers,
   sorted in order of the hash comparison function, followed by a
   null pointer.  The hash table is not modified.  The caller is
   responsible for freeing the allocated data.

   If you don't need to search or modify the hash table, then
   hsh_sort() is a more efficient choice. */
void **
hsh_sort_copy (struct hsh_table *h)
{
  void **copy;

  assert (h != NULL);
  copy = hsh_data_copy (h);
  sort (copy, h->used, sizeof *copy, comparison_helper, h);
  return copy;
}

/* Hash entries. */

/* Searches hash table H for TARGET.  If found, returns a pointer
   to a pointer to that entry; otherwise returns a pointer to a
   NULL entry which *must* be used to insert a new entry having
   the same key data.  */
inline void **
hsh_probe (struct hsh_table *h, const void *target)
{
  unsigned i;

  assert (h != NULL);
  assert (target != NULL);
  assert (h->hash_ordered);

  if (h->used > h->size / 2)
    rehash (h, h->size * 2);
  i = locate_matching_entry (h, target);
  if (h->entries[i] == NULL)
    h->used++;
  return &h->entries[i];
}

/* Searches hash table H for TARGET.  If not found, inserts
   TARGET and returns a null pointer.  If found, returns the
   match, without replacing it in the table. */
void *
hsh_insert (struct hsh_table *h, void *target)
{
  void **entry;

  assert (h != NULL);
  assert (target != NULL);

  entry = hsh_probe (h, target);
  if (*entry == NULL)
    {
      *entry = target;
      return NULL;
    }
  else
    return *entry;
}

/* Searches hash table H for TARGET.  If not found, inserts
   TARGET and returns a null pointer.  If found, returns the
   match, after replacing it in the table by TARGET. */
void *
hsh_replace (struct hsh_table *h, void *target)
{
  void **entry = hsh_probe (h, target);
  void *old = *entry;
  *entry = target;
  return old;
}

/* Returns the entry in hash table H that matches TARGET, or NULL
   if there is none. */
void *
hsh_find (struct hsh_table *h, const void *target)
{
  return h->entries[locate_matching_entry (h, target)];
}

/* Deletes the entry in hash table H that matches TARGET.
   Returns true if an entry was deleted.

   Uses Knuth's Algorithm 6.4R (Deletion with linear probing).
   Because our load factor is at most 1/2, the average number of
   moves that this algorithm makes should be at most 2 - ln 2 ~=
   1.65. */
bool
hsh_delete (struct hsh_table *h, const void *target)
{
  unsigned i = locate_matching_entry (h, target);
  if (h->entries[i] != NULL)
    {
      h->used--;
      if (h->free != NULL)
        h->free (h->entries[i], h->aux);

      for (;;)
        {
          unsigned r;
          ptrdiff_t j;

          h->entries[i] = NULL;
          j = i;
          do
            {
              i = (i - 1) & (h->size - 1);
              if (h->entries[i] == NULL)
                return true;

              r = h->hash (h->entries[i], h->aux) & (h->size - 1);
            }
          while ((i <= r && r < j) || (r < j && j < i) || (j < i && i <= r));
          h->entries[j] = h->entries[i];
        }
    }
  else
    return false;
}

/* Iteration. */

/* Finds and returns an entry in TABLE, and initializes ITER for
   use with hsh_next().  If TABLE is empty, returns a null
   pointer. */
void *
hsh_first (struct hsh_table *h, struct hsh_iterator *iter)
{
  assert (h != NULL);
  assert (iter != NULL);

  iter->next = 0;
  return hsh_next (h, iter);
}

/* Iterates through TABLE with iterator ITER.  Returns the next
   entry in TABLE, or a null pointer after the last entry.

   Entries are returned in an undefined order.  Modifying TABLE
   during iteration may cause some entries to be returned
   multiple times or not at all. */
void *
hsh_next (struct hsh_table *h, struct hsh_iterator *iter)
{
  size_t i;

  assert (h != NULL);
  assert (iter != NULL);
  assert (iter->next <= h->size);

  for (i = iter->next; i < h->size; i++)
    if (h->entries[i])
      {
	iter->next = i + 1;
	return h->entries[i];
      }

  iter->next = h->size;
  return NULL;
}

/* Returns the number of items in H. */
size_t
hsh_count (struct hsh_table *h)
{
  assert (h != NULL);

  return h->used;
}

/* Debug helpers. */

#if DEBUGGING
#undef NDEBUG
#include "message.h"
#include <stdio.h>

/* Displays contents of hash table H on stdout. */
void
hsh_dump (struct hsh_table *h)
{
  void **entry = h->entries;
  int i;

  printf (_("hash table:"));
  for (i = 0; i < h->size; i++)
    printf (" %p", *entry++);
  printf ("\n");
}

/* This wrapper around hsh_probe() assures that it returns a pointer
   to a NULL pointer.  This function is used when it is known that the
   entry to be inserted does not already exist in the table. */
void
hsh_force_insert (struct hsh_table *h, void *p)
{
  void **pp = hsh_probe (h, p);
  assert (*pp == NULL);
  *pp = p;
}

/* This wrapper around hsh_find() assures that it returns non-NULL.
   This function is for use when it is known that the entry being
   searched for must exist in the table. */
void *
hsh_force_find (struct hsh_table *h, const void *target)
{
  void *found = hsh_find (h, target);
  assert (found != NULL);
  return found;
}

/* This wrapper for hsh_delete() verifies that an item really was
   deleted. */
void
hsh_force_delete (struct hsh_table *h, const void *target)
{
  int found = hsh_delete (h, target);
  assert (found != 0);
}
#endif
