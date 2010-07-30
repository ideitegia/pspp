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

/* An interface to an array of octets that is stored on disk as a temporary
   file. */

#ifndef LIBPSPP_EXT_ARRAY_H
#define LIBPSPP_EXT_ARRAY_H 1

#include <stdbool.h>
#include <sys/types.h>

struct ext_array *ext_array_create (void);
bool ext_array_destroy (struct ext_array *);
bool ext_array_read (const struct ext_array *, off_t offset, size_t n, void *);
bool ext_array_write (struct ext_array *, off_t offset, size_t n,
                      const void *);
bool ext_array_error (const struct ext_array *);

#endif /* libpspp/ext-array.h */
