/* PSPP - a program for statistical analysis.
   Copyright (C) 2006 Free Software Foundation, Inc.

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

#ifndef SCRATCH_HANDLE_H
#define SCRATCH_HANDLE_H 1

#include <stdbool.h>

/* A scratch file. */
struct scratch_handle
  {
    struct dictionary *dictionary;      /* Dictionary. */
    struct casereader *casereader;      /* Cases. */
  };

void scratch_handle_destroy (struct scratch_handle *);

#endif /* scratch-handle.h */
