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

#if !alloc_h
#define alloc_h 1

#include <stddef.h>

/* malloc() wrapper functions. */
void *xmalloc (size_t size);
void *xcalloc (size_t n_memb, size_t size);
void *xrealloc (void *ptr, size_t size);
char *xstrdup (const char *s);
void out_of_memory (void) NO_RETURN;

/* alloca() wrapper functions. */
#if defined (HAVE_ALLOCA) || defined (C_ALLOCA)
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
#define local_alloc(X) alloca (X)
#define local_free(P) ((void) 0)
#else
#define local_alloc(X) xmalloc (X)
#define local_free(P) free (P)
#endif

#endif /* alloc.h */
