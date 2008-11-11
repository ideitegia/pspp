/* PSPP - a program for statistical analysis.
   Copyright (C) 2008 Free Software Foundation, Inc.

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

#ifndef DATA_ATTRIBUTES_H
#define DATA_ATTRIBUTES_H 1

#include <libpspp/hmapx.h>

/* This header supports custom attribute of the sort maintained
   by the DATAFILE ATTRIBUTE and VARIABLE ATTRIBUTE commands.

   Attributes have a name (the rules for which are the same as
   those for PSPP variable names) and one or more values, each of
   which is a string.  An attribute may be part of one attribute
   set.

   An attribute set is an unordered collection of attributes
   with names that are unique (case-insensitively). */

struct attribute *attribute_create (const char *name);
struct attribute *attribute_clone (const struct attribute *);
void attribute_destroy (struct attribute *);

const char *attribute_get_name (const struct attribute *);
const char *attribute_get_value (const struct attribute *, size_t index);
void attribute_add_value (struct attribute *, const char *);
void attribute_set_value (struct attribute *, size_t index, const char *);
void attribute_del_value (struct attribute *, size_t index);
size_t attribute_get_n_values (const struct attribute *);

struct attrset 
  {
    struct hmap map;
  };

void attrset_init (struct attrset *);
void attrset_clone (struct attrset *, const struct attrset *);
void attrset_destroy (struct attrset *);

size_t attrset_count (const struct attrset *);

struct attribute *attrset_lookup (struct attrset *, const char *);
void attrset_add (struct attrset *, struct attribute *);
void attrset_delete (struct attrset *, const char *);
void attrset_clear (struct attrset *);

struct attrset_iterator
  {
    struct hmap_node *node;
  };
struct attribute *attrset_first (const struct attrset *,
                                 struct attrset_iterator *);
struct attribute *attrset_next (const struct attrset *,
                                struct attrset_iterator *);


#endif /* data/attributes.h */
