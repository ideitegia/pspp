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

#if !heap_h
#define heap_h 1

/* This module implements a priority queue as a heap as described in
   Knuth 5.2.3.  This is a first-in-smallest-out priority queue. */

#include <stddef.h>

/* One element of a heap. */
struct heap_elem
  {
    int index;			/* Data. */
    int key;			/* Key value. */
  };

/* An entire heap. */
struct heap
  {
    size_t n_elem;		/* Number of elements in heap. */
    size_t m_elem;		/* Number of elements allocated for heap. */
    struct heap_elem *elem;	/* Heap elements. */
  };

struct heap *heap_create (size_t m_elem);
void heap_destroy (struct heap *);
int heap_insert (struct heap *, int index, int key);
int heap_delete (struct heap *, int *key);
int heap_size (struct heap *);

#if GLOBAL_DEBUGGING
void heap_verify (const struct heap *);
void heap_dump (const struct heap *);
#endif

#endif /* heap_h */
