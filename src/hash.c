/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>
#include "hash.h"
#include "error.h"
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include "algorithm.h"
#include "alloc.h"
#include "misc.h"
#include "str.h"

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
  const unsigned char *buf = buf_;
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
  const unsigned char *s = s_;
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
  const unsigned char *s = s_;
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

    void *aux;                  /* Auxiliary data for comparison functions. */
    hsh_compare_func *compare;
    hsh_hash_func *hash;
    hsh_free_func *free;
  };

/* Creates a hash table with at least M entries.  COMPARE is a
   function that compares two entries and returns 0 if they are
   identical, nonzero otherwise; HASH returns a nonnegative hash value
   for an entry; FREE destroys an entry. */
struct hsh_table *
hsh_create (int size, hsh_compare_func *compare, hsh_hash_func *hash,
            hsh_free_func *free, void *aux)
{
  struct hsh_table *h;
  int i;

  if ( size ==  0 ) 
    return NULL;

  assert (compare != NULL);
  assert (hash != NULL);
  
  h = xmalloc (sizeof *h);
  h->used = 0;
  if (size < 4)
    size = 4;
  h->size = next_power_of_2 (size);
  h->entries = xmalloc (sizeof *h->entries * h->size);
  for (i = 0; i < h->size; i++)
    h->entries[i] = NULL;
  h->aux = aux;
  h->compare = compare;
  h->hash = hash;
  h->free = free;
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
      free (h->entries);
      free (h);
    }
}

/* Locates an entry matching TARGET.  Returns a pointer to the
   entry, or a null pointer on failure. */
static inline unsigned
locate_matching_entry (struct hsh_table *h, const void *target) 
{
  unsigned i = h->hash (target, h->aux);

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

/* Changes the capacity of H to NEW_SIZE. */
static void
hsh_rehash (struct hsh_table *h, size_t new_size)
{
  void **begin, **end, **table_p;
  int i;

  assert (h != NULL);
  assert (new_size >= h->used);

  begin = h->entries;
  end = begin + h->size;

  h->size = new_size;
  h->entries = xmalloc (sizeof *h->entries * h->size);
  for (i = 0; i < h->size; i++)
    h->entries[i] = NULL;
  for (table_p = begin; table_p < end; table_p++) 
    {
      void *entry = *table_p;
      if (entry != NULL)
        h->entries[locate_matching_entry (h, entry)] = entry;
    }
  free (begin);
}

/* A "algo_predicate_func" that returns nonzero if DATA points
   to a non-null void. */
static int
not_null (const void *data_, void *aux UNUSED) 
{
  void *const *data = data_;

  return *data != NULL;
}

/* Compacts hash table H and returns a pointer to its data.  The
   returned data consists of hsh_count(H) non-null pointers, in
   no particular order, followed by a null pointer.  After
   calling this function, only hsh_destroy() and hsh_count() may
   be applied to H. */
void **
hsh_data (struct hsh_table *h) 
{
  size_t n;

  assert (h != NULL);
  n = partition (h->entries, h->size, sizeof *h->entries,
                 not_null, NULL);
  assert (n == h->used);
  return h->entries;
}

/* Dereferences void ** pointers and passes them to the hash
   comparison function. */
static int
comparison_helper (const void *a_, const void *b_, void *h_) 
{
  void *const *a = a_;
  void *const *b = b_;
  struct hsh_table *h = h_;

  assert(a);
  assert(b);

  return h->compare (*a, *b, h->aux);
}

/* Sorts hash table H based on hash comparison function.  The
   returned data consists of hsh_count(H) non-null pointers,
   sorted in order of the hash comparison function, followed by a
   null pointer.  After calling this function, only hsh_destroy()
   and hsh_count() may be applied to H. */
void **
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
   the allocated data. */
void **
hsh_data_copy (struct hsh_table *h) 
{
  void **copy;

  assert (h != NULL);
  copy = xmalloc ((h->used + 1) * sizeof *copy);
  copy_if (h->entries, h->size, sizeof *h->entries, copy,
           not_null, NULL);
  copy[h->used] = NULL;
  return copy;
}

/* Makes and returns a copy of the pointers to the data in H.
   The returned data consists of hsh_count(H) non-null pointers,
   sorted in order of the hash comparison function, followed by a
   null pointer.  The hash table is not modified.  The caller is
   responsible for freeing the allocated data. */
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

  if (h->used > h->size / 2)
    hsh_rehash (h, h->size * 2);
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
   Returns nonzero if an entry was deleted.

   Uses Knuth's Algorithm 6.4R (Deletion with linear probing).
   Because our load factor is at most 1/2, the average number of
   moves that this algorithm makes should be at most 2 - ln 2 ~=
   1.65. */
int
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
                return 1;
              
              r = h->hash (h->entries[i], h->aux) & (h->size - 1);
            }
          while ((i <= r && r < j) || (r < j && j < i) || (j < i && i <= r));
          h->entries[j] = h->entries[i]; 
        }
    }
  else
    return 0;
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

#if GLOBAL_DEBUGGING
#undef NDEBUG
#include "error.h"
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
