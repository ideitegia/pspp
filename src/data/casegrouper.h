/* PSPP - a program for statistical analysis.
   Copyright (C) 2007 Free Software Foundation, Inc.

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

/* Casegrouper.

   Breaks up the cases from a casereader into sets of contiguous
   cases based on some criteria, e.g. sets of cases that all have
   the same values for some subset of variables.  Each set of
   cases is made available to the client as a casereader. */

#ifndef DATA_CASEGROUPER_H
#define DATA_CASEGROUPER_H 1

#include <stdbool.h>
#include <stddef.h>

struct casereader;
struct ccase;
struct dictionary;
struct subcase;
struct variable;

struct casegrouper *
casegrouper_create_func (struct casereader *,
                         bool (*same_group) (const struct ccase *,
                                             const struct ccase *,
                                             void *aux),
                         void (*destroy) (void *aux),
                         void *aux);
struct casegrouper *casegrouper_create_vars (struct casereader *,
                                             const struct variable *const *vars,
                                             size_t var_cnt);
struct casegrouper *casegrouper_create_splits (struct casereader *,
                                               const struct dictionary *);
struct casegrouper *casegrouper_create_subcase (struct casereader *,
                                                const struct subcase *);
bool casegrouper_get_next_group (struct casegrouper *, struct casereader **);
bool casegrouper_destroy (struct casegrouper *);

#endif /* data/casegrouper.h */
