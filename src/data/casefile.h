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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#ifndef HEADER_CASEFILE
#define HEADER_CASEFILE

#include <stddef.h>
#include <stdbool.h>

struct ccase;
struct casefile;
struct casereader;

struct casefile *casefile_create (size_t value_cnt);
void casefile_destroy (struct casefile *);

bool casefile_error (const struct casefile *);
bool casefile_in_core (const struct casefile *);
bool casefile_to_disk (const struct casefile *);
bool casefile_sleep (const struct casefile *);

size_t casefile_get_value_cnt (const struct casefile *);
unsigned long casefile_get_case_cnt (const struct casefile *);

bool casefile_append (struct casefile *, const struct ccase *);
bool casefile_append_xfer (struct casefile *, struct ccase *);

void casefile_mode_reader (struct casefile *);
struct casereader *casefile_get_reader (const struct casefile *);
struct casereader *casefile_get_destructive_reader (struct casefile *);
struct casereader *casefile_get_random_reader (const struct casefile *);

const struct casefile *casereader_get_casefile (const struct casereader *);
bool casereader_read (struct casereader *, struct ccase *);
bool casereader_read_xfer (struct casereader *, struct ccase *);
void casereader_destroy (struct casereader *);

void casereader_seek (struct casereader *, unsigned long case_idx);

unsigned long casereader_cnum(const struct casereader *);

#endif /* casefile.h */
