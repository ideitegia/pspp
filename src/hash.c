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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include "alloc.h"
#include "hash.h"
#include "quicksort.h"
#include "str.h"

/* Hash table. */
struct hsh_table
  {
    size_t used;                /* Number of filled entries. */
    size_t size;                /* Number of entries (a power of 2). */
    void **entries;		/* Hash table proper. */

    void *param;
    hsh_compare_func *compare;
    hsh_hash_func *hash;
    hsh_free_func *free;
  };

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

/* Colin Plumb's "one-at-a-time" hash, for bytes. */
unsigned
hsh_hash_bytes (const void *buf_, size_t size)
{
  const unsigned char *buf = buf_;
  unsigned hash = 0;
  while (size-- > 0) 
    {
      hash += *buf++;
      hash += (hash << 10);
      hash ^= (hash >> 6);
    } 
  hash += (hash << 3);
  hash ^= (hash >> 11);
  hash += (hash << 15);
  return hash;
} 

/* Colin Plumb's "one-at-a-time" hash, for strings. */
unsigned
hsh_hash_string (const char *s_) 
{
  const unsigned char *s = s_;
  unsigned hash = 0;
  while (*s != '\0') 
    {
      hash += *s++;
      hash += (hash << 10);
      hash ^= (hash >> 6);
    } 
  hash += (hash << 3);
  hash ^= (hash >> 11);
  hash += (hash << 15);
  return hash;
}

/* Hash for ints. */
unsigned
hsh_hash_int (int i) 
{
  return hsh_hash_bytes (&i, sizeof i);
}

/* Hash tables. */

/* Creates a hash table with at least M entries.  COMPARE is a
   function that compares two entries and returns 0 if they are
   identical, nonzero otherwise; HASH returns a nonnegative hash value
   for an entry; FREE destroys an entry. */
struct hsh_table *
hsh_create (int size, hsh_compare_func *compare, hsh_hash_func *hash,
            hsh_free_func *free, void *param)
{
  struct hsh_table *h = xmalloc (sizeof *h);
  int i;

  h->used = 0;
  h->size = next_power_of_2 (size);
  h->entries = xmalloc (sizeof *h->entries * h->size);
  for (i = 0; i < h->size; i++)
    h->entries[i] = NULL;
  h->param = param;
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

  if (h->free)
    for (i = 0; i < h->size; i++)
      if (h->entries[i] != NULL)
        h->free (h->entries[i], h->param);

  for (i = 0; i < h->size; i++)
    h->entries[i] = NULL;
}

/* Destroys table H and all its contents. */
void
hsh_destroy (struct hsh_table *h)
{
  int i;

  if (h == NULL)
    return;
  if (h->free)
    for (i = 0; i < h->size; i++)
      if (h->entries[i] != NULL)
        h->free (h->entries[i], h->param);
  free (h->entries);
  free (h);
}

/* Changes the capacity of H to NEW_SIZE. */
static void
hsh_rehash (struct hsh_table *h, size_t new_size)
{
  void **begin = h->entries;
  void **end = &h->entries[h->size];
  void **table_p;
  int i;

  h->size = new_size;
  h->entries = xmalloc (sizeof *h->entries * h->size);
  for (i = 0; i < h->size; i++)
    h->entries[i] = NULL;
  for (table_p = begin; table_p < end; table_p++)
    {
      void **entry;

      if (*table_p == NULL)
	continue;
      entry = &h->entries[h->hash (*table_p, h->param) & (h->size - 1)];
      while (*entry)
	if (--entry < h->entries)
	  entry = &h->entries[h->size - 1];
      *entry = *table_p;
    }
  free (begin);
}

/* hsh_sort() helper function that ensures NULLs are sorted after the
   rest of the table. */
static int
sort_nulls_last (const void *a_, const void *b_, void *h_)
{
  void *a = *(void **) a_;
  void *b = *(void **) b_;
  struct hsh_table *h = h_;

  if (a != NULL) 
    {
      if (b != NULL)
        return h->compare (a, b, h->param);
      else
        return -1;
    }
  else
    {
      if (b != NULL)
        return +1;
      else
        return 0;
    }
}

/* Sorts hash table H based on hash comparison function.  NULLs
   are sent to the end of the table.  The resultant table is
   returned (it is guaranteed to be NULL-terminated).  H should
   not be used again as a hash table until and unless hsh_clear()
   called. */
void **
hsh_sort (struct hsh_table *h)
{
  quicksort (h->entries, h->size, sizeof *h->entries, sort_nulls_last, h);
  return h->entries;
}

/* Hash entries. */

/* Searches hash table H for TARGET.  If found, returns a pointer to a
   pointer to that entry; otherwise returns a pointer to a NULL entry
   which _must_ be used to insert a new entry having the same key
   data.  */
inline void **
hsh_probe (struct hsh_table *h, const void *target)
{
  void **entry;

  /* Order of these statements is important! */
  if (h->used > h->size / 2)
    hsh_rehash (h, h->size * 2);
  entry = &h->entries[h->hash (target, h->param) & (h->size - 1)];

  while (*entry)
    {
      if (!h->compare (*entry, target, h->param))
	return entry;
      if (--entry < h->entries)
	entry = &h->entries[h->size - 1];
    }
  h->used++;
  return entry;
}

/* Locates an entry matching TARGET.  Returns a pointer to the
   entry, or a null pointer on failure. */
static inline void **
locate_matching_entry (struct hsh_table *h, const void *target) 
{
  void **entry = &h->entries[h->hash (target, h->param) & (h->size - 1)];

  while (*entry)
    {
      if (!h->compare (*entry, target, h->param))
	return entry;
      if (--entry < h->entries)
	entry = &h->entries[h->size - 1];
    }
  return NULL;
}

/* Returns the entry in hash table H that matches TARGET, or NULL
   if there is none. */
void *
hsh_find (struct hsh_table *h, const void *target)
{
  void **entry = locate_matching_entry (h, target);
  return entry != NULL ? *entry : NULL;
}

/* Deletes the entry in hash table H that matches TARGET.
   Returns nonzero if an entry was deleted.

   Note: this function is very slow because it rehashes the
   entire table.  Don't use this hash table implementation if
   deletion is a common operation. */
int
hsh_delete (struct hsh_table *h, const void *target) 
{
  void **entry = locate_matching_entry (h, target);
  if (h->free != NULL) 
    {
      h->free (*entry, h->param);
      *entry = 0;
      hsh_rehash (h, h->size);
      return 1;
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
#include <assert.h>
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
