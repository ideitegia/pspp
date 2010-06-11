/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010 Free Software Foundation, Inc.

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

#ifndef LIBPSPP_TEMP_FILE_H
#define LIBPSPP_TEMP_FILE_H 1

#include <stdbool.h>
#include <sys/types.h>

struct temp_file *temp_file_create (void);
bool temp_file_destroy (struct temp_file *);
bool temp_file_read (const struct temp_file *, off_t offset, size_t n, void *);
bool temp_file_write (struct temp_file *, off_t offset, size_t n,
                      const void *);
bool temp_file_error (const struct temp_file *);

#endif /* libpspp/temp-file.h */
