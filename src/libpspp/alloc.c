/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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

#include <config.h>
#include "alloc.h"
#include <stdlib.h>

/* Allocates and returns N elements of S bytes each.
   N must be nonnegative, S must be positive.
   Returns a null pointer if the memory cannot be obtained,
   including the case where N * S overflows the range of size_t. */
void *
nmalloc (size_t n, size_t s)
{
  return !xalloc_oversized (n, s) ? malloc (n * s) : NULL;
}
