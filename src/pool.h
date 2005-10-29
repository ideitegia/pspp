/* PSPP - computes sample statistics.
   Copyright (C) 2000 Free Software Foundation, Inc.
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

#if !pool_h
#define pool_h 1

#include <stdio.h>

/* Records the state of a pool for later restoration. */
struct pool_mark 
  {
    /* Current block and offset into it. */
    struct pool_block *block;
    size_t ofs;

    /* Current serial number to allow freeing of gizmos. */
    long serial;
  };

/* General routines. */
struct pool *pool_create (void);
void pool_destroy (struct pool *);
void pool_clear (struct pool *);

/* Suballocation routines. */
void *pool_alloc (struct pool *, size_t) MALLOC_LIKE;
void *pool_nalloc (struct pool *, size_t n, size_t s) MALLOC_LIKE;
void *pool_clone (struct pool *, const void *, size_t) MALLOC_LIKE;
char *pool_strdup (struct pool *, const char *) MALLOC_LIKE;
char *pool_strndup (struct pool *, const char *, size_t) MALLOC_LIKE;
char *pool_strcat (struct pool *, const char *, ...) MALLOC_LIKE;

/* Standard allocation routines. */
void *pool_malloc (struct pool *, size_t) MALLOC_LIKE;
void *pool_nmalloc (struct pool *, size_t n, size_t s) MALLOC_LIKE;
void *pool_realloc (struct pool *, void *, size_t);
void *pool_nrealloc (struct pool *, void *, size_t n, size_t s);
void pool_free (struct pool *, void *);

/* Gizmo allocations. */
struct pool *pool_create_subpool (struct pool *);
FILE *pool_fopen (struct pool *, const char *, const char *);
int pool_fclose (struct pool *, FILE *);

/* Custom allocations. */
void pool_register (struct pool *, void (*free) (void *), void *p);
int pool_unregister (struct pool *, void *);

/* Partial freeing. */
void pool_mark (struct pool *, struct pool_mark *);
void pool_release (struct pool *, const struct pool_mark *);

#if GLOBAL_DEBUGGING
void pool_dump (const struct pool *, const char *title);
#endif

#endif /* pool.h */
