/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.

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

#include <config.h>
#include <libpspp/alloc.h>
#include <libpspp/compiler.h>
#include "casefilter.h"
#include <stdlib.h>

#include <stdio.h>
#include <data/case.h>
#include <data/variable.h>
#include <data/missing-values.h>

struct casefilter
 {
   enum mv_class class;

   const struct variable **vars;
   int n_vars;
 };


/* Returns true iff the entire case should be skipped */
bool
casefilter_skip_case (const struct casefilter *filter, const struct ccase *c)
{
  int i;

  for (i = 0; i < filter->n_vars; ++i)
    {
      if ( casefilter_variable_missing (filter, c, filter->vars[i]))
	return true;
    }

  return false;
}

/* Returns true iff the variable V in case C is missing */
bool
casefilter_variable_missing (const struct casefilter *filter,
			     const struct ccase *c,
			     const struct variable *var)
{
  const union value *val = case_data (c, var) ;
  return var_is_value_missing (var, val, filter->class);
}

/* Create a new casefilter that drops cases in which any of the
   N_VARS variables in VARS are in the given CLASS of missing values.
   VARS is an array of variables which if *any* of them are missing.
   N_VARS is the size of VARS.
 */
struct casefilter *
casefilter_create (enum mv_class class, struct variable **vars, int n_vars)
{
  int i;
  struct casefilter * filter = xmalloc (sizeof (*filter)) ;

  filter->class = class;
  filter->vars = xnmalloc (n_vars, sizeof (*filter->vars) );

  for ( i = 0 ; i < n_vars ; ++i )
    filter->vars[i] = vars[i];

  filter->n_vars = n_vars ;

  return filter ;
}


/* Add the variables in VARS to the list of variables for which the
   filter considers. N_VARS is the size of VARS */
void
casefilter_add_variables (struct casefilter *filter,
			  struct variable *const *vars, int n_vars)
{
  int i;

  filter->vars = xnrealloc (filter->vars, filter->n_vars + n_vars,
			   sizeof (*filter->vars) );

  for ( i = 0 ; i < n_vars ; ++i )
    filter->vars[i + filter->n_vars] = vars[i];

  filter->n_vars += n_vars ;
}

/* Destroy the filter FILTER */
void
casefilter_destroy (struct casefilter *filter)
{
  free (filter->vars);
  free (filter);
}
