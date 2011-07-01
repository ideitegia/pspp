/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2011 Free Software Foundation, Inc.

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

#include "math/extrema.h"

#include <stdlib.h>

#include "data/case.h"
#include "data/val-type.h"
#include "libpspp/compiler.h"
#include "libpspp/ll.h"

#include "gl/xalloc.h"

struct extrema
{
  size_t capacity;
  size_t n;
  struct ll_list list;

  ll_compare_func *cmp_func;
};


static int
cmp_descending (const struct ll *a_, const struct ll *b_, void *aux UNUSED)
{
  const struct extremum *a = ll_data (a_, struct extremum, ll);
  const struct extremum *b = ll_data (b_, struct extremum, ll);

  if ( a->value > b->value) return -1;

  return (a->value < b->value);
}

static int
cmp_ascending (const struct ll *a_, const struct ll *b_, void *aux UNUSED)
{
  const struct extremum *a = ll_data (a_, struct extremum, ll);
  const struct extremum *b = ll_data (b_, struct extremum, ll);

  if ( a->value < b->value) return -1;

  return (a->value > b->value);
}


struct extrema *
extrema_create (size_t n, enum extreme_end end)
{
  struct extrema *extrema = xzalloc (sizeof *extrema);
  extrema->capacity = n;

  if ( end == EXTREME_MAXIMA )
    extrema->cmp_func = cmp_descending;
  else
    extrema->cmp_func = cmp_ascending;

  ll_init (&extrema->list);

  return extrema;
}

void
extrema_destroy (struct extrema *extrema)
{
  struct ll *ll = ll_head (&extrema->list);

  while (ll != ll_null (&extrema->list))
    {
      struct extremum *e = ll_data (ll, struct extremum, ll);

      ll = ll_next (ll);
      free (e);
    }

  free (extrema);
}


void
extrema_add (struct extrema *extrema, double val,
	     double weight,
	     casenumber location)
{
  struct extremum *e = xzalloc (sizeof *e) ;
  e->value = val;
  e->location = location;
  e->weight = weight;

  if ( val == SYSMIS)
    {
      free (e);
      return;
    }

  ll_insert_ordered (ll_head (&extrema->list), ll_null (&extrema->list),
		       &e->ll,  extrema->cmp_func, NULL);

  if ( extrema->n++ > extrema->capacity)
    {
      struct ll *tail = ll_tail (&extrema->list);
      struct extremum *et = ll_data (tail, struct extremum, ll);

      ll_remove (tail);

      free (et);
    }
}


const struct ll_list *
extrema_list (const struct extrema *ex)
{
  return &ex->list;
}


bool
extrema_top (const struct extrema *ex, double *v)
{
  const struct extremum  *top;

  if ( ll_is_empty (&ex->list))
    return false;

  top = (const struct extremum *)
    ll_data (ll_head(&ex->list), struct extremum, ll);

  *v = top->value;

  return true;
}
