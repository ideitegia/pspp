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

#if !hash_h
#define hash_h 1

#include <stddef.h>

typedef int hsh_compare_func (const void *, const void *, void *param);
typedef unsigned hsh_hash_func (const void *, void *param);
typedef void hsh_free_func (void *, void *param);

/* Hash table iterator (opaque). */
struct hsh_iterator
  {
    size_t next;		/* Index of next entry. */
  };

/* Prime numbers and hash functions. */
unsigned hsh_hash_bytes (const void *, size_t);
unsigned hsh_hash_string (const char *);
unsigned hsh_hash_int (int);

/* Hash tables. */
struct hsh_table *hsh_create (int m, hsh_compare_func *,
                              hsh_hash_func *, hsh_free_func *,
			      void *param);
void hsh_clear (struct hsh_table *);
void hsh_destroy (struct hsh_table *);
void **hsh_sort (struct hsh_table *);

/* Search and insertion. */
void **hsh_probe (struct hsh_table *, const void *);
void *hsh_find (struct hsh_table *, const void *);
int hsh_delete (struct hsh_table *, const void *);

/* Iteration. */
void *hsh_first (struct hsh_table *, struct hsh_iterator *);
void *hsh_next (struct hsh_table *, struct hsh_iterator *);

/* Search and insertion with assertion. */
#if GLOBAL_DEBUGGING
void hsh_force_insert (struct hsh_table *, void *);
void *hsh_force_find (struct hsh_table *, const void *);
void hsh_force_delete (struct hsh_table *, const void *);
#else
#define hsh_force_insert(A, B)  ((void) (*hsh_probe (A, B) = B))
#define hsh_force_find(A, B)    (hsh_find (A, B))
#define hsh_force_delete(A, B)  ((void) hsh_delete (A, B))
#endif

/* Number of entries in hash table H. */
size_t hsh_count (struct hsh_table *);

/* Debugging. */
#if GLOBAL_DEBUGGING
void hsh_dump (struct hsh_table *);
#endif

#endif /* hash_h */
