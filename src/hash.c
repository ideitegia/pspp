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

static int hsh_prime_tab[] =
{
  13, 31, 47, 67, 131, 257, 521, 1031, 2053, 4099, 8209, 16411,
  32771, 65537, 131101, 262147, 524309, 1048583, 2097169, 4194319,
  8388617, 16777259, 33554467, 67108879, 134217757, 268435459,
  536870923, 1073741827, INT_MAX,
};

/* Returns pointer into hsh_prime_tab[], pointing to the first prime
   in the table greater than X. */
int *
hsh_next_prime (int x)
{
  int *p;

  assert (x >= 0);

  for (p = hsh_prime_tab; *p < x; p++)
    ;

  assert (*p != INT_MAX);

  return p;
}

/* P.J. Weinberger's hash function, recommended by the Red Dragon
   Book.  Hashes the d-string between S1 and S2.  Returns unbounded
   nonnegative result. */
int
hashpjw_d (const char *s1, const char *s2)
{
  const char *p;
  unsigned g, h;

  for (h = 0, p = s1; p < s2; p++)
    {
      h = (h << 4) + *(unsigned char *) p;
      g = h & 0xf0000000;
      h ^= (g >> 24) | g;
    }
  return abs ((int) h);
}

/* Alternate entry point for hashpjw_d() that takes an s-string. */
int
hashpjw (const char *s)
{
  return hashpjw_d (s, &s[strlen (s)]);
}

/*hash tables. */

/* Creates a hash table with at least M entries.  COMPARE is a
   function that compares two entries and returns 0 if they are
   identical, nonzero otherwise; HASH returns a nonnegative hash value
   for an entry; FREE destroys an entry. */
struct hsh_table *
hsh_create (int m,
	    int (*compare) (const void *, const void *, void *param),
	    unsigned (*hash) (const void *, void *param),
	    void (*free) (void *, void *param),
	    void *param)
{
  struct hsh_table *h = xmalloc (sizeof *h);
  int i;

  h->n = 0;
  h->mp = hsh_next_prime (m);
  h->m = *h->mp++;
  h->table = xmalloc (sizeof *h->table * h->m);
  for (i = 0; i < h->m; i++)
    h->table[i] = NULL;
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
    for (i = 0; i < h->m; i++)
      h->free (h->table[i], h->param);

  if (h->m >= 128)
    {
      free (h->table);
      h->mp = hsh_next_prime (31);
      h->m = *h->mp++;
      h->table = xmalloc (sizeof *h->table * h->m);
    }

  for (i = 0; i < h->m; i++)
    h->table[i] = NULL;
}

/* Destroys table H and all its contents. */
void
hsh_destroy (struct hsh_table *h)
{
  int i;

  if (h == NULL)
    return;
  if (h->free)
    for (i = 0; i < h->m; i++)
      {
	void *p = h->table[i];
	if (p)
	  h->free (p, h->param);
      }
  free (h->table);
  free (h);
}

/* Increases the capacity of H. */
void
hsh_rehash (struct hsh_table *h)
{
  void **begin = h->table;
  void **end = &h->table[h->m];
  void **table_p;
  int i;

  h->m = *h->mp++;
  h->table = xmalloc (sizeof *h->table * h->m);
  for (i = 0; i < h->m; i++)
    h->table[i] = NULL;
  for (table_p = begin; table_p < end; table_p++)
    {
      void **entry;

      if (*table_p == NULL)
	continue;
      entry = &h->table[h->hash (*table_p, h->param) % h->m];
      while (*entry)
	if (--entry < h->table)
	  entry = &h->table[h->m - 1];
      *entry = *table_p;
    }
  free (begin);
}

/* Static variables for hsh_sort(). */
static void *hsh_param;
static int (*hsh_compare) (const void *, const void *, void *param);

/* hsh_sort() helper function that ensures NULLs are sorted after the
   rest of the table. */
static int
internal_comparison_fn (const void *pa, const void *pb)
{
  void *a = *(void **) pa;
  void *b = *(void **) pb;
  return a == NULL ? 1 : (b == NULL ? -1 : hsh_compare (a, b, hsh_param));
}

/* Sorts hash table H based on function COMPARE.  NULLs are sent to
   the end of the table.  The resultant table is returned (it is
   guaranteed to be NULL-terminated).  H should not be used again as a
   hash table until and unless hsh_clear() called. */
void **
hsh_sort (struct hsh_table *h,
	  int (*compare) (const void *, const void *, void *param))
{
#if GLOBAL_DEBUGGING
  static int reentrant;
  if (reentrant)
    abort ();
  reentrant++;
#endif
  hsh_param = h->param;
  hsh_compare = compare ? compare : h->compare;
  qsort (h->table, h->m, sizeof *h->table, internal_comparison_fn);
#if GLOBAL_DEBUGGING
  reentrant--;
#endif
  return h->table;
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
  if (h->n > h->m / 2)
    hsh_rehash (h);
  entry = &h->table[h->hash (target, h->param) % h->m];

  while (*entry)
    {
      if (!h->compare (*entry, target, h->param))
	return entry;

      if (--entry < h->table)
	entry = &h->table[h->m - 1];
    }
  h->n++;
  return entry;
}

/* Returns the entry in hash table H that matches TARGET, NULL if
   there is none. */
void *
hsh_find (struct hsh_table *h, const void *target)
{
  void **entry = &h->table[h->hash (target, h->param) % h->m];

  while (*entry)
    {
      if (!h->compare (*entry, target, h->param))
	return *entry;
      if (--entry < h->table)
	entry = &h->table[h->m - 1];
    }
  return NULL;
}

/* Iterates throught hash table TABLE with iterator ITER.  Returns the
   next non-NULL entry in TABLE, or NULL after the last non-NULL
   entry.  After NULL is returned, ITER is returned to a condition in
   which hsh_foreach() will return the first non-NULL entry if any on
   the next call.  Do not add entries to TABLE between call to
   hsh_foreach() between NULL returns.

   Before calling hsh_foreach with a particular iterator for the first
   time, you must initialize the iterator with a call to
   hsh_iterator_init.  */
void *
hsh_foreach (struct hsh_table *table, struct hsh_iterator *iter)
{
  int i;

  if (!table)
    return NULL;
  if (!iter->init)
    {
      iter->init = 1;
      iter->next = 0;
    }
  for (i = iter->next; i < table->m; i++)
    if (table->table[i])
      {
	iter->next = i + 1;
	return table->table[i];
      }
  iter->init = 0;
  return NULL;
}

#if GLOBAL_DEBUGGING
#include <stdio.h>

/* Displays contents of hash table H on stdout. */
void
hsh_dump (struct hsh_table *h)
{
  void **entry = h->table;
  int i;

  printf (_("hash table:"));
  for (i = 0; i < h->m; i++)
    printf (" %p", *entry++);
  printf ("\n");
}

/* This wrapper around hsh_probe() assures that it returns a pointer
   to a NULL pointer.  This function is used when it is known that the
   entry to be inserted does not already exist in the table. */
void
force_hsh_insert (struct hsh_table *h, void *p)
{
  void **pp = hsh_probe (h, p);
  if (*pp != NULL)
    assert (0);
  *pp = p;
}

/* This wrapper around hsh_find() assures that it returns non-NULL.
   This function is for use when it is known that the entry being
   searched for must exist in the table. */
void *
force_hsh_find (struct hsh_table *h, const void *p)
{
  p = hsh_find (h, p);
  if (p == NULL)
    assert (0);
  return (void *) p;
}
#endif
