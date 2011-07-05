/* PSPP - a program for statistical analysis.
   Copyright (C) 2011 Free Software Foundation, Inc.

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

#include "interaction.h"

#include "data/value.h"
#include "data/variable.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include <stdio.h>


/*
  An interaction is a structure containing a "product" of other
  variables. The variables can be either string or numeric.

  Interaction is commutative.  That means, that from a mathematical point of
  view,  the order of the variables is irrelevant.  However, for display
  purposes, and for matching with an interaction's value the order is 
  pertinent.
  
  Therefore, when using these functions, make sure the orders of variables 
  and values match when appropriate.
*/



struct interaction *
interaction_create (const struct variable *v)
{
  struct interaction  *i = xmalloc (sizeof *i);
  i->vars = xmalloc (sizeof *i->vars);
  i->vars[0] = v;
  i->n_vars = 1;
  return i;
}

void
interaction_destroy (struct interaction *i)
{
  free (i->vars);
  free (i);
}

void
interaction_add_variable (struct interaction *i, const struct variable *v)
{
  i->vars = xrealloc (i->vars, sizeof (*i->vars) * ++i->n_vars);
  i->vars[i->n_vars - 1] = v;
}


void
interaction_dump (const struct interaction *i)
{
  int v = 0;
  printf ("%s", var_get_name (i->vars[v]));
  for (v = 1; v < i->n_vars; ++v)
    {
      printf (" * %s", var_get_name (i->vars[v]));
    }
  printf ("\n");
}

/* Appends STR with a representation of the interaction, suitable for user
   display.

   STR must have been initialised prior to calling this function.
*/
void
interaction_to_string (const struct interaction *iact, struct string *str)
{
  int v = 0;
  ds_put_cstr (str, var_to_string (iact->vars[v]));
  for (v = 1; v < iact->n_vars; ++v)
    {
      ds_put_cstr (str, " * ");
      ds_put_cstr (str, var_to_string (iact->vars[v]));
    }
}

unsigned int
interaction_value_hash (const struct interaction *iact, const union value *val)
{
  int i;
  size_t hash = 0;
  for (i = 0; i < iact->n_vars; ++i)
    {
      hash = value_hash (&val[i], var_get_width (iact->vars[i]), hash);
    }

  return hash;
}

bool
interaction_value_equal (const struct interaction *iact, const union value *val1, const union value *val2)
{
  int i;
  bool same = true;

  for (i = 0; i < iact->n_vars; ++i)
    {
      if ( ! value_equal (&val1[i], &val2[i], var_get_width (iact->vars[i])))
	{
	  same = false;
	  break;
	}
    }

  return same;
}


bool
interaction_value_is_missing (const struct interaction *iact, const union value *val, enum mv_class exclude)
{
  int i;
  bool missing = false;

  for (i = 0; i < iact->n_vars; ++i)
    {
      if ( var_is_value_missing (iact->vars[i], &val[i], exclude))
	{
	  missing = true;
	  break;
	}
    }

  return missing;
}
