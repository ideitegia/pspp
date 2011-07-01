/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011 Free Software Foundation, Inc.

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

#ifndef LIBPSPP_INTERN_H
#define LIBPSPP_INTERN_H 1

/* Interned strings.

   An "interned" string is stored in a global hash table.  Only one copy of any
   given string is kept in the hash table, which reduces memory usage in cases
   where there might otherwise be many duplicates of a given string.

   Interned strings can be compared for equality by comparing pointers, which
   can also be a significant advantage in some cases.

   Interned strings are immutable.

   See http://en.wikipedia.org/wiki/String_interning for more information. */

#include <stdbool.h>
#include <stddef.h>

const char *intern_new (const char *);
const char *intern_ref (const char *);
void intern_unref (const char *);

size_t intern_strlen (const char *);

bool is_interned_string (const char *);

#endif /* libpspp/intern.h */
