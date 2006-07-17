/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.
   Written by John Darrington <john@darrington.wattle.id.au>

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

#ifndef FLEXIFILE_H
#define FLEXIFILE_H

#include <config.h>
#include <stdbool.h>
#include <stdlib.h>

struct ccase;
struct casefile;
struct casereader;
struct flexifile;
struct flexifilereader;

#define FLEXIFILE(CF) ( (struct flexifile *) CF)
#define FLEXIFILEREADER(CR) ( (struct flexifilereader *) CR)

struct casefile *flexifile_create (size_t value_cnt);

bool flexifile_get_case(const struct flexifile *ff, unsigned long casenum, 
			struct ccase *const c);

bool flexifile_resize (struct flexifile *ff, int n_values, int posn);

bool flexifile_insert_case (struct flexifile *ff, struct ccase *c, int posn);
bool flexifile_delete_cases (struct flexifile *ff, int n_cases, int first);


#endif
