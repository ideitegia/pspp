/* PSPP - a program for statistical analysis.
   Copyright (C) 2010 Free Software Foundation, Inc.

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

#include "data/mrset.h"

#include <stdlib.h>

#include "data/dictionary.h"
#include "data/val-type.h"
#include "data/variable.h"

#include "gl/xalloc.h"

/* Creates and returns a clone of OLD.  The caller is responsible for freeing
   the new multiple response set (using mrset_destroy()). */
struct mrset *
mrset_clone (const struct mrset *old)
{
  struct mrset *new;

  new = xmalloc (sizeof *new);
  new->name = xstrdup (old->name);
  new->label = old->label != NULL ? xstrdup (old->label) : NULL;
  new->type = old->type;
  new->vars = xmemdup (old->vars, old->n_vars * sizeof *old->vars);
  new->n_vars = old->n_vars;

  new->cat_source = old->cat_source;
  new->label_from_var_label = old->label_from_var_label;
  value_clone (&new->counted, &old->counted, old->width);
  new->width = old->width;

  return new;
}

/* Frees MRSET and the data that it contains. */
void
mrset_destroy (struct mrset *mrset)
{
  if (mrset != NULL)
    {
      free (mrset->name);
      free (mrset->label);
      free (mrset->vars);
      value_destroy (&mrset->counted, mrset->width);
    }
}

/* Checks various constraints on MRSET:

   - MRSET has a valid name for a multiple response set (beginning with '$').

   - MRSET has a valid type.

   - MRSET has at least 2 variables.

   - All of MRSET's variables are in DICT.

   - All of MRSET's variables are the same type (numeric or string).

   - If MRSET is a multiple dichotomy set, its counted value has the same type
     as and is no wider than its narrowest variable.

   Returns true if all the constraints are satisfied, otherwise false. */
bool
mrset_ok (const struct mrset *mrset, const struct dictionary *dict)
{
  enum val_type type;
  size_t i;

  if (mrset->name == NULL
      || mrset->name[0] != '$'
      || (mrset->type != MRSET_MD && mrset->type != MRSET_MC)
      || mrset->vars == NULL
      || mrset->n_vars < 2)
    return false;

  type = var_get_type (mrset->vars[0]);
  if (mrset->type == MRSET_MD && type != val_type_from_width (mrset->width))
    return false;
  for (i = 0; i < mrset->n_vars; i++)
    if (!dict_contains_var (dict, mrset->vars[i])
        || type != var_get_type (mrset->vars[i])
        || (mrset->type == MRSET_MD
            && mrset->width > var_get_width (mrset->vars[i])))
      return false;

  return true;
}