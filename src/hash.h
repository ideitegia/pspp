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

/* Hash table (opaque). */
struct hsh_table
  {
    int n;			/* Number of filled entries. */
    int m;			/* Number of entries. */
    int *mp;			/* Pointer into hsh_prime_tab[]. */
    void **table;		/* Hash table proper. */

    void *param;
    int (*compare) (const void *, const void *, void *param);
    unsigned (*hash) (const void *, void *param);
    void (*free) (void *, void *param);
  };

/* Hash table iterator (opaque). */
struct hsh_iterator
  {
    int init;			/* Initialized? */
    int next;			/* Index of next entry. */
  };

#define hsh_iterator_init(ITERATOR) (ITERATOR).init = 0

/* Prime numbers and hash functions. */
int *hsh_next_prime (int) __attribute__ ((const));
int hashpjw_d (const char *s1, const char *s2);

#if __GNUC__>=2 && __OPTIMIZE__
extern inline int
hashpjw (const char *s)
{
  return hashpjw_d (s, &s[strlen (s)]);
}
#else
int hashpjw (const char *s);
#endif

/* Hash tables. */
struct hsh_table *hsh_create (int m,
			      int (*compare) (const void *, const void *,
					      void *param),
			      unsigned (*hash) (const void *, void *param),
			      void (*free) (void *, void *param),
			      void *param);
void hsh_clear (struct hsh_table *);
void hsh_destroy (struct hsh_table *);
void hsh_rehash (struct hsh_table *);
void **hsh_sort (struct hsh_table *,
		 int (*compare) (const void *, const void *, void *param));
#if GLOBAL_DEBUGGING
void hsh_dump (struct hsh_table *);
#endif

/* Hash entries. */
void **hsh_probe (struct hsh_table *, const void *);
void *hsh_find (struct hsh_table *, const void *);
void *hsh_foreach (struct hsh_table *, struct hsh_iterator *);

#if GLOBAL_DEBUGGING
void force_hsh_insert (struct hsh_table *, void *);
void *force_hsh_find (struct hsh_table *, const void *);
#else
#define force_hsh_insert(A, B)			\
	do *hsh_probe (A, B) = B; while (0)
#define force_hsh_find(A, B)			\
	hsh_find (A, B)
#endif

/* Returns number of used elements in hash table H. */
#define hsh_count(H) 				\
	((H)->n)

#endif /* hash_h */
