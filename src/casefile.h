/* PSPP - computes sample statistics.
   Copyright (C) 2004 Free Software Foundation, Inc.
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

#ifndef HEADER_CASEFILE
#define HEADER_CASEFILE

#include <stddef.h>

struct ccase;
struct casefile;
struct casereader;

struct casefile *casefile_create (size_t case_size);
void casefile_destroy (struct casefile *);

int casefile_in_core (const struct casefile *);
size_t casefile_get_case_size (const struct casefile *);
unsigned long casefile_get_case_cnt (const struct casefile *);

void casefile_append (struct casefile *, const struct ccase *);
void casefile_to_disk (struct casefile *);

int casefile_sort (struct casefile *,
                   int (*compare) (const struct ccase *,
                                   const struct ccase *, void *aux),
                   void *aux);

struct casereader *casefile_get_reader (const struct casefile *);
int casereader_read (struct casereader *, const struct ccase **);
void casereader_destroy (struct casereader *);

#endif /* casefile.h */
