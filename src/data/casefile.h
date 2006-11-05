/* PSPP - computes sample statistics.
   Copyright (C) 2004, 2006 Free Software Foundation, Inc.
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

#ifndef CASEFILE_H
#define CASEFILE_H

#include <config.h>
#include <stddef.h>
#include <stdbool.h>


struct ccase;
struct casereader;
struct casefile;
struct casefilter;

/* Casereader functions */

struct casefile *casereader_get_casefile (const struct casereader *r);

unsigned long casereader_cnum (const struct casereader *r);

bool casereader_read (struct casereader *r, struct ccase *c);

bool casereader_read_xfer (struct casereader *r, struct ccase *c);

void casereader_destroy (struct casereader *r);

struct casereader *casereader_clone(const struct casereader *r);


/* Casefile functions */

void casefile_destroy (struct casefile *cf);

bool casefile_error (const struct casefile *cf);

unsigned long casefile_get_case_cnt (const struct casefile *cf);

size_t casefile_get_value_cnt (const struct casefile *cf);

struct casereader *casefile_get_reader (const struct casefile *cf, struct casefilter *filter);

struct casereader *casefile_get_destructive_reader (struct casefile *cf);

bool casefile_append (struct casefile *cf, const struct ccase *c);

bool casefile_append_xfer (struct casefile *cf, struct ccase *c);

bool casefile_sleep (const struct casefile *cf);

bool casefile_in_core (const struct casefile *cf);

bool casefile_to_disk (const struct casefile *cf);

#endif
