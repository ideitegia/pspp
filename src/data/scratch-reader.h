/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.
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

#ifndef SCRATCH_READER_H
#define SCRATCH_READER_H 1

#include <stdbool.h>

struct dictionary;
struct file_handle;
struct ccase;
struct scratch_reader *scratch_reader_open (struct file_handle *,
                                            struct dictionary **);
bool scratch_reader_read_case (struct scratch_reader *, struct ccase *);
bool scratch_reader_error (const struct scratch_reader *);
void scratch_reader_close (struct scratch_reader *);

#endif /* scratch-reader.h */
