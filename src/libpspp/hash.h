/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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

#if !hash_h
#define hash_h 1

#include <stddef.h>
#include <stdbool.h>

typedef int hsh_compare_func (const void *, const void *, const void *aux);
typedef unsigned hsh_hash_func (const void *, const void *aux);
typedef void hsh_free_func (void *, const void *aux);

/* Hash table iterator (opaque). */
struct hsh_iterator
  {
    size_t next;		/* Index of next entry. */
  };

/* Hash functions. */
unsigned hsh_hash_bytes (const void *, size_t);
unsigned hsh_hash_string (const char *);
unsigned hsh_hash_case_string (const char *);
unsigned hsh_hash_int (int);
unsigned hsh_hash_double (double);

/* Hash tables. */
struct hsh_table *hsh_create (int m, hsh_compare_func *,
                              hsh_hash_func *, hsh_free_func *,
			      const void *aux);

struct pool;
struct hsh_table *hsh_create_pool (struct pool *pool, int m,
				   hsh_compare_func *,
				   hsh_hash_func *, hsh_free_func *,
				   const void *aux);

void hsh_clear (struct hsh_table *);
void hsh_destroy (struct hsh_table *);
void *const *hsh_sort (struct hsh_table *);
void *const *hsh_data (struct hsh_table *);
void **hsh_sort_copy (struct hsh_table *);
void **hsh_data_copy (struct hsh_table *);

/* Search and insertion. */
void **hsh_probe (struct hsh_table *, const void *);
void *hsh_insert (struct hsh_table *, void *);
void *hsh_replace (struct hsh_table *, void *);
void *hsh_find (struct hsh_table *, const void *);
bool hsh_delete (struct hsh_table *, const void *);

/* Iteration. */
void *hsh_first (struct hsh_table *, struct hsh_iterator *);
void *hsh_next (struct hsh_table *, struct hsh_iterator *);

/* Search and insertion with assertion. */
#if DEBUGGING
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
#if DEBUGGING
void hsh_dump (struct hsh_table *);
#endif


/* Const Wrappers for the above */

static inline struct const_hsh_table *
const_hsh_create (int m,
		  hsh_compare_func *hcf,
		  hsh_hash_func *hhf, hsh_free_func *hff,
		  const void *aux)
{
  return (struct const_hsh_table *) hsh_create (m, hcf, hhf, hff, aux);
}



static inline struct const_hsh_table *
const_hsh_create_pool (struct pool *pool, int m,
		       hsh_compare_func *cf,
		       hsh_hash_func *hf, hsh_free_func *ff,
		       const void *aux)
{
  return (struct const_hsh_table *) hsh_create_pool (pool, m, cf, hf, ff, aux);
}


static inline void
const_hsh_clear (struct const_hsh_table *h)
{
  hsh_clear ( (struct hsh_table *) h);
}

static inline void
const_hsh_destroy (struct const_hsh_table *h)
{
  hsh_destroy ( (struct hsh_table *) h);
}

static inline void *const *
const_hsh_sort (struct const_hsh_table *h)
{
  return hsh_sort ( (struct hsh_table *) h);
}

static inline void *const *
const_hsh_data (struct const_hsh_table *h)
{
  return hsh_data ( (struct hsh_table *) h);
}

static inline void **
const_hsh_sort_copy (struct const_hsh_table *h)
{
  return hsh_sort_copy ( (struct hsh_table *) h);
}

static inline void **
const_hsh_data_copy (struct const_hsh_table *h)
{
  return hsh_data_copy ( (struct hsh_table *) h);
}


static inline size_t
const_hsh_count (struct const_hsh_table *h)
{
  return hsh_count ( (struct hsh_table *) h);
}

static inline void *
const_hsh_insert (struct const_hsh_table *h, const void *item)
{
  return hsh_insert ( (struct hsh_table *) h, (void *) item);
}

static inline void *
const_hsh_replace (struct const_hsh_table *h, const void *item)
{
  return hsh_replace ( (struct hsh_table *) h, (void *) item);
}

static inline void *
const_hsh_find (struct const_hsh_table *h, const void *item)
{
  return hsh_find ( (struct hsh_table *) h, (void *) item);
}

static inline bool
const_hsh_delete (struct const_hsh_table *h, const void *item)
{
  return hsh_delete ( (struct hsh_table *)h, (void *) item);
}


static inline void *
const_hsh_first (struct const_hsh_table *h, struct hsh_iterator *i)
{
  return hsh_first ( (struct hsh_table *) h, i);
}

static inline void *
const_hsh_next (struct const_hsh_table *h, struct hsh_iterator *i)
{
  return hsh_next ( (struct hsh_table *) h, i);
}


#endif /* hash_h */
