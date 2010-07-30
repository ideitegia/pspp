/* PSPP - a program for statistical analysis.
   Copyright (C) 2000, 2006, 2010 Free Software Foundation, Inc.

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

#if !pool_h
#define pool_h 1

#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include "compiler.h"

/* Maximum size of a suballocated block.  Larger blocks are allocated
   directly with malloc() to avoid memory wastage at the end of a
   suballocation block. */
#ifndef MAX_SUBALLOC
#define MAX_SUBALLOC 64
#endif


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

/* Creates a pool, allocates an instance of the given STRUCT
   within it, sets the struct's MEMBER to the pool's address, and
   returns the allocated structure. */
#define pool_create_container(STRUCT, MEMBER)                           \
        ((STRUCT *) pool_create_at_offset (sizeof (STRUCT),             \
                                           offsetof (STRUCT, MEMBER)))
void *pool_create_at_offset (size_t struct_size, size_t pool_member_offset);

/* Suballocation routines. */
void *pool_alloc (struct pool *, size_t) MALLOC_LIKE;
void *pool_nalloc (struct pool *, size_t n, size_t s) MALLOC_LIKE;
void *pool_clone (struct pool *, const void *, size_t) MALLOC_LIKE;

void *pool_alloc_unaligned (struct pool *, size_t) MALLOC_LIKE;
void *pool_clone_unaligned (struct pool *, const void *, size_t) MALLOC_LIKE;
char *pool_strdup (struct pool *, const char *) MALLOC_LIKE;
char *pool_strdup0 (struct pool *, const char *, size_t) MALLOC_LIKE;
char *pool_vasprintf (struct pool *, const char *, va_list)
     MALLOC_LIKE PRINTF_FORMAT (2, 0);
char *pool_asprintf (struct pool *, const char *, ...)
     MALLOC_LIKE PRINTF_FORMAT (2, 3);

/* Standard allocation routines. */
void *pool_malloc (struct pool *, size_t) MALLOC_LIKE;
void *pool_nmalloc (struct pool *, size_t n, size_t s) MALLOC_LIKE;
void *pool_zalloc (struct pool *, size_t) MALLOC_LIKE;
void *pool_calloc (struct pool *, size_t n, size_t s) MALLOC_LIKE;
void *pool_realloc (struct pool *, void *, size_t);
void *pool_nrealloc (struct pool *, void *, size_t n, size_t s);
void *pool_2nrealloc (struct pool *, void *, size_t *pn, size_t s);
void pool_free (struct pool *, void *);

/* Subpools. */
struct pool *pool_create_subpool (struct pool *);
void pool_add_subpool (struct pool *, struct pool *subpool);

/* Files. */
FILE *pool_fopen (struct pool *, const char *, const char *);
int pool_fclose (struct pool *, FILE *) WARN_UNUSED_RESULT;
void pool_attach_file (struct pool *, FILE *);
void pool_detach_file (struct pool *, FILE *);

/* Temporary files. */
FILE *pool_create_temp_file (struct pool *);
void pool_fclose_temp_file (struct pool *, FILE *);
void pool_attach_temp_file (struct pool *, FILE *);
void pool_detach_temp_file (struct pool *, FILE *);

/* Custom allocations. */
void pool_register (struct pool *, void (*free) (void *), void *p);
bool pool_unregister (struct pool *, void *);

/* Partial freeing. */
void pool_mark (struct pool *, struct pool_mark *);
void pool_release (struct pool *, const struct pool_mark *);

#if DEBUGGING
void pool_dump (const struct pool *, const char *title);
#endif

#endif /* pool.h */
