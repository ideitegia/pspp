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

#if !alloc_h
#define alloc_h 1

#include <stddef.h>

/* malloc() wrapper functions. */
void *xmalloc (size_t size);
void *xcalloc (size_t size);
void *xrealloc (void *ptr, size_t size);
char *xstrdup (const char *s);

/* alloca() wrapper functions. */
#if defined (HAVE_ALLOCA) || defined (C_ALLOCA)
#include <alloca.h>
#define local_alloc(X) alloca (X)
#define local_free(P) ((void) 0)
#else
#define local_alloc(X) xmalloc (X)
#define local_free(P) free (P)
#endif

#endif /* alloc.h */
