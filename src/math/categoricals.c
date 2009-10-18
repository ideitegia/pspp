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
#include <libpspp/pool.h>


struct value_node
{
  struct hmap_node node;      /* Node in hash map. */
  union value value;          /* The value being labeled. */
  double cc;                  /* The total of the weights of cases with this value */
  int subscript;              /* A zero based integer, unique within the variable.
				 Can be used as an index into an array */
};


struct var_params
{
  /* A map indexed by a union values */
  struct hmap map;

  int base_subscript;

  /* The number of distinct values of this variable */
  int n_cats;

  /* A map of values indexed by subscript */
  const struct value_node **reverse_value_map;
};


struct categoricals
{
  const struct variable **vars;
  size_t n_vars;

  const struct variable *wv;

  /* An array of var_params */
  struct var_params *vp;

  /* A map to enable the lookup of variables indexed by subscript */
  int *reverse_variable_map;

  size_t n_cats_total;

  struct pool *pool;
};


void
categoricals_destroy ( struct categoricals *cat)
{
  int i;
  for (i = 0 ; i < cat->n_vars; ++i)
    hmap_destroy (&cat->vp[i].map);

  pool_destroy (cat->pool);
  free (cat);
}


void
categoricals_dump (const struct categoricals *cat)
{
  int v;

  for (v = 0 ; v < cat->n_vars; ++v)
    {
      const struct var_params *vp = &cat->vp[v];
      const struct hmap *m = &vp->map;
      size_t width = var_get_width (cat->vars[v]);
      struct hmap_node *node ;
      int x;
     
      printf ("\n%s (%d):\n", var_get_name (cat->vars[v]), vp->base_subscript);

      assert (vp->reverse_value_map);

      printf ("Reverse map\n");
      for (x = 0 ; x < vp->n_cats; ++x)
	{
	  const struct value_node *vn = vp->reverse_value_map[x];
	  printf ("Value for %d is %s\n", x, value_str (&vn->value, width));
	}

      printf ("\nForward map\n");
      for (node = hmap_first (m); node; node = hmap_next (m, node))
	{
	  const struct value_node *vn = HMAP_DATA (node, struct value_node, node);
	  printf ("Value: %s; Index %d; CC %g\n",
		  value_str (&vn->value, width),  vn->subscript, vn->cc);
	}
    }
}



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

      fprintf (stderr, "Warning: Hash table collision\n");
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
  cat->n_cats_total = 0;
  cat->reverse_variable_map = NULL;
  cat->pool = pool_create ();

  cat->vp = pool_calloc (cat->pool, sizeof *cat->vp, n_vars);

  for (i = 0 ; i < cat->n_vars; ++i)
    hmap_init (&cat->vp[i].map);

  return cat;
}



void
categoricals_update (struct categoricals *cat, const struct ccase *c)
{
  size_t i;
  
  const double weight = cat->wv ? case_data (c, cat->wv)->f : 1.0;

  assert (NULL == cat->reverse_variable_map);

  for (i = 0 ; i < cat->n_vars; ++i)
    {
      unsigned int width = var_get_width (cat->vars[i]);
      const union value *val = case_data (c, cat->vars[i]);
      size_t hash = value_hash (val, width, 0);

      struct value_node  *node = lookup_value (&cat->vp[i].map, cat->vars[i], val);
      if ( NULL == node)
	{
	  node = pool_calloc (cat->pool, sizeof *node, 1);

	  value_init (&node->value, width);
	  value_copy (&node->value, val, width);
	  node->cc = 0.0;

	  hmap_insert (&cat->vp[i].map, &node->node,  hash);
	  cat->n_cats_total ++;
	  node->subscript = cat->vp[i].n_cats++ ;
	}

      node->cc += weight;
    }
}

/* Return the number of categories (distinct values) for variable N */
size_t
categoricals_n_count (const struct categoricals *cat, size_t n)
{
  return hmap_count (&cat->vp[n].map);
}


/* Return the index for value VAL in the Nth variable */
int
categoricals_index (const struct categoricals *cat, size_t n, const union value *val)
{
  struct value_node *vn = lookup_value (&cat->vp[n].map, cat->vars[n], val);

  if ( vn == NULL)
    return -1;

  return vn->subscript;
}


/* Return the total number of categories */
size_t
categoricals_total (const struct categoricals *cat)
{
  return cat->n_cats_total;
}


/* This function must be called *before* any call to categoricals_get_*_by subscript an
 *after* all calls to categoricals_update */
void
categoricals_done (struct categoricals *cat)
{
  /* Implementation Note: Whilst this function is O(n) in cat->n_cats_total, in most
     uses it will be more efficient that using a tree based structure, since it
     is called only once, and means that subsequent lookups will be O(1).

     1 call of O(n) + 10^9 calls of O(1) is better than 10^9 calls of O(log n).
  */
  int v;
  int idx = 0;
  cat->reverse_variable_map = pool_calloc (cat->pool, sizeof *cat->reverse_variable_map, cat->n_cats_total);
  
  for (v = 0 ; v < cat->n_vars; ++v)
    {
      int i;
      struct var_params *vp = &cat->vp[v];
      int n_cats_total = categoricals_n_count (cat, v);
      struct hmap_node *node ;

      vp->reverse_value_map = pool_calloc (cat->pool, sizeof *vp->reverse_value_map,  n_cats_total);

      vp->base_subscript = idx;

      for (node = hmap_first (&vp->map); node; node = hmap_next (&vp->map, node))
	{
	  const struct value_node *vn = HMAP_DATA (node, struct value_node, node);
	  vp->reverse_value_map[vn->subscript] = vn;
	}

      for (i = 0; i < vp->n_cats; ++i)
	cat->reverse_variable_map[idx++] = v;
    }
}



/* Return the categorical variable corresponding to SUBSCRIPT */
const struct variable *
categoricals_get_variable_by_subscript (const struct categoricals *cat, int subscript)
{
  int index;

  assert (cat->reverse_variable_map);
  
  index = cat->reverse_variable_map[subscript];

  return cat->vars[index];
}


/* Return the value corresponding to SUBSCRIPT */
const union value *
categoricals_get_value_by_subscript (const struct categoricals *cat, int subscript)
{
  int vindex = cat->reverse_variable_map[subscript];
  const struct var_params *vp = &cat->vp[vindex];
  const struct value_node *vn = vp->reverse_value_map [subscript - vp->base_subscript];

  return &vn->value;
}


/* Returns unity if the value in case C at SUBSCRIPT is equal to the category
   for that subscript */
double
categoricals_get_binary_by_subscript (const struct categoricals *cat, int subscript,
				      const struct ccase *c)
{
  const struct variable *var = categoricals_get_variable_by_subscript (cat, subscript);
  int width = var_get_width (var);

  const union value *val = case_data (c, var);

  return value_equal (val, categoricals_get_value_by_subscript (cat, subscript), width);
}
