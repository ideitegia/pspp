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

#ifndef DATA_VARDICT_H
#define DATA_VARDICT_H 1

/* Interface between dictionary and variable code.
   This header file should only be included by variable.c and
   dictionary.c. */

/* Dictionary data stored in variable. */
struct vardict_info 
  {
    int dict_index;     /* Dictionary index containing the variable. */
    int case_index;     /* Index into case of variable data. */
  };

const struct vardict_info *var_get_vardict (const struct variable *);
void var_set_vardict (struct variable *, const struct vardict_info *);
bool var_has_vardict (const struct variable *);
void var_clear_vardict (struct variable *);

#endif /* data/vardict.h */
