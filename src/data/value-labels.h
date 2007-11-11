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

/* Sets of value labels.

   struct val_labs represents a mapping from `union value's to
   strings.  The `union value's in the mapping all have the same
   width.  If this width is numeric or short string, the mapping
   may contain any number of entries; long string mappings are
   always empty. */

#ifndef DATA_VALUE_LABELS_H
#define DATA_VALUE_LABELS_H 1

#include <stdbool.h>
#include <stddef.h>
#include <data/value.h>

/* One value label. */
struct val_lab
  {
    union value value;
    const char *label;
  };

/* Creating and destroying sets of value labels. */
struct val_labs *val_labs_create (int width);
struct val_labs *val_labs_clone (const struct val_labs *);
void val_labs_clear (struct val_labs *);
void val_labs_destroy (struct val_labs *);

/* Looking up value labels. */
char *val_labs_find (const struct val_labs *, union value);

/* Basic properties. */
size_t val_labs_count (const struct val_labs *);
bool val_labs_can_set_width (const struct val_labs *, int new_width);
void val_labs_set_width (struct val_labs *, int new_width);

/* Adding value labels. */
bool val_labs_add (struct val_labs *, union value, const char *);
void val_labs_replace (struct val_labs *, union value, const char *);
bool val_labs_remove (struct val_labs *, union value);

/* Iterating through value labels. */
struct val_labs_iterator;
struct val_lab *val_labs_first (const struct val_labs *,
                                struct val_labs_iterator **);
struct val_lab *val_labs_first_sorted (const struct val_labs *,
                                       struct val_labs_iterator **);
struct val_lab *val_labs_next (const struct val_labs *,
                               struct val_labs_iterator **);
void val_labs_done (struct val_labs_iterator **);

#endif /* data/value-labels.h */
