/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009 Free Software Foundation, Inc.

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

/* A interface to allow a temporary file to be treated as an
   array of data. */

#ifndef LIBPSPP_TMPFILE_H
#define LIBPSPP_TMPFILE_H 1

#include <stdbool.h>
#include <sys/types.h>

struct tmpfile *tmpfile_create (void);
bool tmpfile_destroy (struct tmpfile *);
bool tmpfile_read (const struct tmpfile *, off_t offset, size_t n, void *);
bool tmpfile_write (struct tmpfile *, off_t offset, size_t n, const void *);
bool tmpfile_error (const struct tmpfile *);

#endif /* libpspp/tmpfile.h */
