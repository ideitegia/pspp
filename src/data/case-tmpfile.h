/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2011 Free Software Foundation, Inc.

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

/* Manager for temporary files, each of which stores an array of
   like-size cases.

   Partial and whole cases may be read from and written to a
   case_tmpfile in random order.  The indexes of the cases
   written in a case_tmpfile need not be sequential or start from
   0 (although this will be inefficient if the file system does
   not support sparse files).  The case_tmpfile does not track
   which cases have been written, so the client is responsible
   for reading data only from cases (or partial cases) that have
   previously been written. */

#ifndef DATA_CASE_TMPFILE_H
#define DATA_CASE_TMPFILE_H 1

#include "data/case.h"

struct caseproto;

struct case_tmpfile *case_tmpfile_create (const struct caseproto *);
bool case_tmpfile_destroy (struct case_tmpfile *);

bool case_tmpfile_error (const struct case_tmpfile *);
void case_tmpfile_force_error (struct case_tmpfile *);
const struct taint *case_tmpfile_get_taint (const struct case_tmpfile *);

bool case_tmpfile_get_values (const struct case_tmpfile *,
                              casenumber, size_t start_value,
                              union value[], size_t value_cnt);
struct ccase *case_tmpfile_get_case (const struct case_tmpfile *, casenumber);

bool case_tmpfile_put_values (struct case_tmpfile *,
                              casenumber, size_t start_value,
                              const union value[], size_t value_cnt);
bool case_tmpfile_put_case (struct case_tmpfile *,
                            casenumber, struct ccase *);

#endif /* data/case-tmpfile.h */
