/* PSPP - a program for statistical analysis.
   Copyright (C) 2009 Free Software Foundation, Inc.

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

#include <stdio.h>

#include "categoricals.h"

#include <gl/xalloc.h>
#include <data/variable.h>
#include <data/case.h>
#include <data/value.h>
#include <libpspp/hmap.h>

struct categoricals
{
  const struct variable **vars;
  size_t n_vars;

  const struct variable *wv;

  struct hmap *map;

  int *next_index;

  size_t n_cats;
};


struct value_node
  {
    struct hmap_node node;      /* Node in hash map. */
    union value value;          /* The value being labeled. */
    double cc;                  /* The total of the weights of cases with this value */
    int index;                  /* A zero based integer, unique within the variable.
				   Can be used as an index into an array */
  };


static struct value_node *
lookup_value (const struct hmap *map, const struct variable *var, const union value *val)
{
  struct value_node *foo;
  unsigned int width = var_get_width (var);
  size_t hash = value_hash (val, width, 0);

  HMAP_FOR_EACH_WITH_HASH (foo, struct value_node, node, hash, map)
    {
      if (value_equal (val, &foo->value, width))
	break;
    }

  return foo;
}


struct categoricals *
categoricals_create (const struct variable **v, size_t n_vars, const struct variable *wv)
{
  size_t i;
  struct categoricals *cat = xmalloc (sizeof *cat);
  
  cat->vars = v;
  cat->n_vars = n_vars;
  cat->wv = wv;
  cat->n_cats = 0;

  cat->map = xmalloc (sizeof *cat->map * n_vars);
  cat->next_index = xcalloc (sizeof *cat->next_index, n_vars);

  for (i = 0 ; i < cat->n_vars; ++i)
    {
      hmap_init (&cat->map[i]);
    }

  return cat;
}


void
categoricals_update (struct categoricals *cat, const struct ccase *c)
{
  size_t i;
  
  const double weight = cat->wv ? case_data (c, cat->wv)->f : 1.0;

  for (i = 0 ; i < cat->n_vars; ++i)
    {
      unsigned int width = var_get_width (cat->vars[i]);
      const union value *val = case_data (c, cat->vars[i]);
      size_t hash = value_hash (val, width, 0);

      struct value_node  *node = lookup_value (&cat->map[i], cat->vars[i], val);

      if ( NULL == node)
	{
	  node = xmalloc (sizeof *node);

	  value_init (&node->value, width);
	  value_copy (&node->value, val, width);
	  node->cc = 0.0;

	  hmap_insert (&cat->map[i], &node->node,  hash);
	  cat->n_cats ++;
	  node->index = cat->next_index[i]++ ;
	}

      node->cc += weight;
    }
}

/* Return the number of categories (distinct values) for variable N */
size_t
categoricals_n_count (const struct categoricals *cat, size_t n)
{
  return hmap_count (&cat->map[n]);
}


/* Return the index for value VAL in the Nth variable */
int
categoricals_index (const struct categoricals *cat, size_t n, const union value *val)
{
  struct value_node *vn = lookup_value (&cat->map[n], cat->vars[n], val);

  if ( vn == NULL)
    return -1;

  return vn->index;
}


/* Return the total number of categories */
size_t
categoricals_total (const struct categoricals *cat)
{
  return cat->n_cats;
}






