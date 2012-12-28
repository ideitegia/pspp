/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2010, 2011, 2012  Free Software Foundation, Inc.

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

#include "data/vector.h"

#include <stdlib.h>

#include "data/dictionary.h"
#include "data/identifier.h"
#include "libpspp/assertion.h"
#include "libpspp/i18n.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

/* Vector of variables. */
struct vector
  {
    char *name;                         /* Name. */
    struct variable **vars;             /* Set of variables. */
    size_t var_cnt;                     /* Number of variables. */
  };

/* Checks that all the variables in VECTOR have consistent
   width. */
static void
check_widths (const struct vector *vector)
{
  int width = var_get_width (vector->vars[0]);
  size_t i;

  for (i = 1; i < vector->var_cnt; i++)
    assert (width == var_get_width (vector->vars[i]));
}

/* Creates and returns a new vector with the given UTF-8 encoded NAME
   that contains the VAR_CNT variables in VARS.
   All variables in VARS must have the same type and width. */
struct vector *
vector_create (const char *name, struct variable **vars, size_t var_cnt)
{
  struct vector *vector = xmalloc (sizeof *vector);

  assert (var_cnt > 0);
  assert (id_is_plausible (name, false));

  vector->name = xstrdup (name);
  vector->vars = xmemdup (vars, var_cnt * sizeof *vector->vars);
  vector->var_cnt = var_cnt;
  check_widths (vector);

  return vector;
}

/* Creates and returns a new vector as a clone of OLD, but that
   contains variables from NEW_DICT that are in the same position
   as those in OLD are in OLD_DICT.
   All variables in the new vector must have the same type and
   width. */
struct vector *
vector_clone (const struct vector *old,
              const struct dictionary *old_dict,
              const struct dictionary *new_dict)
{
  struct vector *new = xmalloc (sizeof *new);
  size_t i;

  new->name = xstrdup (old->name);
  new->vars = xnmalloc (old->var_cnt, sizeof *new->vars);
  new->var_cnt = old->var_cnt;
  for (i = 0; i < new->var_cnt; i++)
    {
      assert (dict_contains_var (old_dict, old->vars[i]));
      new->vars[i] = dict_get_var (new_dict,
                                   var_get_dict_index (old->vars[i]));
    }
  check_widths (new);

  return new;
}

/* Destroys VECTOR. */
void
vector_destroy (struct vector *vector)
{
  free (vector->name);
  free (vector->vars);
  free (vector);
}

/* Returns VECTOR's name, as a UTF-8 encoded string. */
const char *
vector_get_name (const struct vector *vector)
{
  return vector->name;
}

/* Returns the type of the variables in VECTOR. */
enum val_type vector_get_type (const struct vector *vector)
{
  return var_get_type (vector->vars[0]);
}

/* Returns the variable in VECTOR with the given INDEX. */
struct variable *
vector_get_var (const struct vector *vector, size_t index)
{
  assert (index < vector->var_cnt);
  return vector->vars[index];
}

/* Returns the number of variables in VECTOR. */
size_t
vector_get_var_cnt (const struct vector *vector)
{
  return vector->var_cnt;
}

/* Compares two pointers to vectors represented by A and B and
   returns a strcmp()-type result. */
int
compare_vector_ptrs_by_name (const void *a_, const void *b_)
{
  struct vector *const *pa = a_;
  struct vector *const *pb = b_;
  struct vector *a = *pa;
  struct vector *b = *pb;

  return utf8_strcasecmp (a->name, b->name);
}

