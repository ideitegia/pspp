/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2012 Free Software Foundation, Inc.

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

#include "data/case.h"
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
  i->n_vars = 0;
  if ( v )
    {
      i->vars[0] = v;
      i->n_vars = 1;
    }
  return i;
}

/* Deep copy an interaction */
struct interaction *
interaction_clone (const struct interaction *iact)
{
  int v;
  struct interaction  *i = xmalloc (sizeof *i);
  i->vars = xcalloc (iact->n_vars, sizeof *i->vars);
  i->n_vars = iact->n_vars;

  for (v = 0; v < iact->n_vars; ++v)
    {
      i->vars[v] = iact->vars[v];
    }

  return i;
}


void
interaction_destroy (struct interaction *i)
{
  if (NULL == i)
    return;

  free (i->vars);
  free (i);
}

void
interaction_add_variable (struct interaction *i, const struct variable *v)
{
  i->vars = xrealloc (i->vars, sizeof (*i->vars) * ++i->n_vars);
  i->vars[i->n_vars - 1] = v;
}


/*
  Do the variables in X->VARS constitute a proper
  subset of the variables in Y->VARS?
 */
bool
interaction_is_proper_subset (const struct interaction *x, const struct interaction *y)
{
  if (x->n_vars >= y->n_vars)
    return false;

  return interaction_is_subset (x, y);
}

/*
  Do the variables in X->VARS constitute a 
  subset (proper or otherwise) of the variables in Y->VARS?
 */
bool
interaction_is_subset (const struct interaction *x, const struct interaction *y)
{
  size_t i;
  size_t j;
  size_t n = 0;

  /* By definition, a subset cannot have more members than its superset */
  if (x->n_vars > y->n_vars)
    return false;

  /* Count the number of values which are members of both sets */
  for (i = 0; i < x->n_vars; i++)
    {
      for (j = 0; j < y->n_vars; j++)
	{
	  if (x->vars [i] == y->vars [j])
	    {
	      n++;
	    }
	}
    }

  /* If ALL the members of X were also found in Y, then this must be a subset */    
  if (n >= x->n_vars)
    return true;

  return false;
}




void
interaction_dump (const struct interaction *i)
{
  int v = 0;
  if ( i->n_vars == 0)
    {
      printf ("(empty)\n");
      return;
    }
  printf ("%s", var_get_name (i->vars[v]));
  for (v = 1; v < i->n_vars; ++v)
    printf (" * %s", var_get_name (i->vars[v]));
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
  if ( iact->n_vars == 0)
    return;
  ds_put_cstr (str, var_to_string (iact->vars[v]));
  for (v = 1; v < iact->n_vars; ++v)
    {
      ds_put_cstr (str, " * ");
      ds_put_cstr (str, var_to_string (iact->vars[v]));
    }
}

unsigned int
interaction_case_hash (const struct interaction *iact, const struct ccase *c, unsigned int base)
{
  int i;
  size_t hash = base;
  for (i = 0; i < iact->n_vars; ++i)
    {
      const struct variable *var = iact->vars[i];
      const union value *val = case_data (c, var);
      hash = value_hash (val, var_get_width (var), hash);
    }
  return hash;
}

bool
interaction_case_equal (const struct interaction *iact, const struct ccase *c1, const struct ccase *c2)
{
  int i;
  bool same = true;

  for (i = 0; i < iact->n_vars; ++i)
    {
      const struct variable *var = iact->vars[i];
      if ( ! value_equal (case_data (c1, var), case_data (c2, var), var_get_width (var)))
	{
	  same = false;
	  break;
	}
    }

  return same;
}


int
interaction_case_cmp_3way (const struct interaction *iact, const struct ccase *c1, const struct ccase *c2)
{
  int i;
  int result = 0;

  for (i = 0; i < iact->n_vars; ++i)
    {
      const struct variable *var = iact->vars[i];
      result = value_compare_3way (case_data (c1, var), case_data (c2, var), var_get_width (var));
      if (result != 0)
	break;
    }

  return result;
}


bool
interaction_case_is_missing (const struct interaction *iact, const struct ccase *c, enum mv_class exclude)
{
  int i;
  bool missing = false;

  for (i = 0; i < iact->n_vars; ++i)
    {
      if ( var_is_value_missing (iact->vars[i], case_data (c, iact->vars[i]), exclude))
	{
	  missing = true;
	  break;
	}
    }

  return missing;
}

