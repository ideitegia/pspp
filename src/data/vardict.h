/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009 Free Software Foundation, Inc.

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

#ifndef DATA_VARDICT_H
#define DATA_VARDICT_H 1

/* Interface between dictionary and variable code.
   This header file should only be included by variable.c and
   dictionary.c. */

struct dictionary ;

/* Binds a variable to a dictionary. */
struct vardict_info
  {
    struct dictionary *dict;
    struct variable *var;
    struct hmap_node name_node; /* In struct dictionary's name_map. */
    int case_index;     /* Index into case of variable data. */
  };

/* Called by dictionary code, defined in variable.c. */
struct vardict_info *var_get_vardict (const struct variable *);
void var_set_vardict (struct variable *, struct vardict_info *);
bool var_has_vardict (const struct variable *);
void var_clear_vardict (struct variable *);

/* Called by variable.c, defined in dictionary.c. */
void dict_var_changed (const struct variable *v, unsigned int what, struct variable *ov);

int vardict_get_dict_index (const struct vardict_info *);

static inline int
vardict_get_case_index (const struct vardict_info *vardict)
{
  return vardict->case_index;
}

static inline struct dictionary *
vardict_get_dictionary (const struct vardict_info *vardict)
{
  return vardict->dict;
}

#endif /* data/vardict.h */
