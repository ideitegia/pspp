/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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

#ifndef VAL_LABS_H
#define VAL_LABS_H 1

#include <stdbool.h>
#include <stddef.h>

#include <data/value.h>

struct val_labs;
struct variable;

struct val_lab 
  {
    union value value;
    const char *label;
  };

struct val_labs *val_labs_create (int width);
struct val_labs *val_labs_copy (const struct val_labs *);
void val_labs_destroy (struct val_labs *);
void val_labs_clear (struct val_labs *);
size_t val_labs_count (const struct val_labs *);

bool val_labs_can_set_width (const struct val_labs *, int new_width);
void val_labs_set_width (struct val_labs *, int new_width);

bool val_labs_add (struct val_labs *, union value, const char *);
void val_labs_replace (struct val_labs *, union value, const char *);
bool val_labs_remove (struct val_labs *, union value);
char *val_labs_find (const struct val_labs *, union value);

struct val_labs_iterator;

struct val_lab *val_labs_first (const struct val_labs *,
                                struct val_labs_iterator **);
struct val_lab *val_labs_first_sorted (const struct val_labs *,
                                       struct val_labs_iterator **);
struct val_lab *val_labs_next (const struct val_labs *,
                               struct val_labs_iterator **);
void val_labs_done (struct val_labs_iterator **);

#endif /* value-labels.h */
