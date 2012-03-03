/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2011, 2012 Free Software Foundation, Inc.

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
#include "data/value.h"
#include "libpspp/hmap.h"

/* One value label.

   A value label is normally part of a struct val_labs (see
   below). */
struct val_lab
  {
    struct hmap_node node;      /* Node in hash map. */
    union value value;          /* The value being labeled. */
    const char *label;          /* An interned string. */
    const char *escaped_label;  /* An interned string. */
  };

/* Returns the value in VL.  The caller must not modify or free
   the returned value.

   The width of the returned value cannot be determined directly
   from VL.  It may be obtained by calling val_labs_get_width on
   the val_labs struct that VL is in. */
static inline const union value *val_lab_get_value (const struct val_lab *vl)
{
  return &vl->value;
}

/* Returns the label in VL as a UTF-8 encoded interned string, in a format
   appropriate for use in output.  The caller must not modify or free the
   returned value. */
static inline const char *
val_lab_get_label (const struct val_lab *vl)
{
  return vl->label;
}

/* Returns the label in VL as a UTF-8 encoded interned string.  Any new-line
   characters in the label's usual output form are represented in the returned
   string as the two-byte sequence "\\n".  This form is used on the VALUE
   LABELS command, in system and portable files, and passed to val_labs_add()
   and val_labs_replace().

   The caller must not modify or free the returned value. */
static inline const char *
val_lab_get_escaped_label (const struct val_lab *vl)
{
  return vl->escaped_label;
}

/* A set of value labels. */
struct val_labs
  {
    int width;                  /* 0=numeric, otherwise string width. */
    struct hmap labels;         /* Hash table of `struct val_lab's. */
  };

/* Creating and destroying sets of value labels. */
struct val_labs *val_labs_create (int width);
struct val_labs *val_labs_clone (const struct val_labs *);
void val_labs_clear (struct val_labs *);
void val_labs_destroy (struct val_labs *);
size_t val_labs_count (const struct val_labs *);

/* Looking up value labels. */
const char *val_labs_find (const struct val_labs *, const union value *);
struct val_lab *val_labs_lookup (const struct val_labs *,
                                 const union value *);
const union value *val_labs_find_value (const struct val_labs *,
                                        const char *label);

/* Basic properties. */
size_t val_labs_count (const struct val_labs *);
int val_labs_get_width (const struct val_labs *);
bool val_labs_can_set_width (const struct val_labs *, int new_width);
void val_labs_set_width (struct val_labs *, int new_width);

/* Adding value labels. */
bool val_labs_add (struct val_labs *, const union value *, const char *);
void val_labs_replace (struct val_labs *, const union value *, const char *);
void val_labs_remove (struct val_labs *, struct val_lab *);

/* Iterating through value labels. */
const struct val_lab *val_labs_first (const struct val_labs *);
const struct val_lab *val_labs_next (const struct val_labs *,
                                     const struct val_lab *);
const struct val_lab **val_labs_sorted (const struct val_labs *);

/* Properties of entire sets. */
unsigned int val_labs_hash (const struct val_labs *, unsigned int basis);
bool val_labs_equal (const struct val_labs *, const struct val_labs *);

#endif /* data/value-labels.h */
