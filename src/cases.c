/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "alloc.h"
#include "cases.h"
#include "var.h"
#include "vfm.h"

#include "debug-print.h"

/* Initializes V. */
void
vec_init (struct long_vec * v)
{
  v->vec = NULL;
  v->n = v->m = 0;
}

/* Deletes the contents of V. */
void
vec_clear (struct long_vec * v)
{
  free (v->vec);
  v->vec = NULL;
  v->n = v->m = 0;
}

/* Inserts ELEM into V. */
void
vec_insert (struct long_vec * v, long elem)
{
  if (v->n >= v->m)
    {
      v->m = (v->m == 0 ? 16 : 2 * v->m);
      v->vec = xrealloc (v->vec, v->m * sizeof *v->vec);
    }
  v->vec[v->n++] = elem;
}

/* Deletes all occurrences of values A through B exclusive from V. */
void
vec_delete (struct long_vec * v, long a, long b)
{
  int i;

  for (i = v->n - 1; i >= 0; i--)
    if (v->vec[i] >= a && v->vec[i] < b)
      v->vec[i] = v->vec[--v->n];
}

/* Sticks V->FV in the proper vector. */
void
envector (const struct variable *v)
{
  if (v->type == NUMERIC)
    {
      if (v->left)
	vec_insert (&init_zero, v->fv);
      else
	vec_insert (&reinit_sysmis, v->fv);
    }
  else
    {
      int i;

      if (v->left)
	for (i = v->fv; i < v->fv + v->nv; i++)
	  vec_insert (&init_blanks, i);
      else
	for (i = v->fv; i < v->fv + v->nv; i++)
	  vec_insert (&reinit_blanks, i);
    }
}

/* Removes V->FV from the proper vector. */
void
devector (const struct variable *v)
{
  if (v->type == NUMERIC)
    {
      if (v->left)
	vec_delete (&init_zero, v->fv, v->fv + 1);
      else
	vec_delete (&reinit_sysmis, v->fv, v->fv + 1);
    }
  else if (v->left)
    vec_delete (&init_blanks, v->fv, v->fv + v->nv);
  else
    vec_delete (&reinit_blanks, v->fv, v->fv + v->nv);
}
