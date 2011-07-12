/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "math/categoricals.h"
#include "math/interaction.h"

#include <stdio.h>

#include "data/case.h"
#include "data/value.h"
#include "data/variable.h"
#include "libpspp/array.h"
#include "libpspp/hmap.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

struct value_node
{
  struct hmap_node node;      /* Node in hash map. */
  struct ccase *ccase;
  double cc;                  /* The total of the weights of cases with this value */

  void *user_data;            /* A pointer to data which the caller can store stuff */

  int subscript;              /* A zero based integer, unique within the variable.
				 Can be used as an index into an array */
};

struct interact_params
{
  /* A map indexed by a union values */
  struct hmap map;

  const struct interaction *iact;

  int base_subscript_short;
  int base_subscript_long;

  /* The number of distinct values of this variable */
  int n_cats;

  /* A map of values indexed by subscript */
  const struct value_node **reverse_value_map;

  /* Total of the weights of this variable */
  double cc; 
};


/* Comparison function to sort the reverse_value_map in ascending order */
static int
compare_value_node (const void *vn1_, const void *vn2_, const void *aux)
{
  const struct value_node * const *vn1 = vn1_;
  const struct value_node * const *vn2 = vn2_;
  const struct interact_params *vp = aux;

  return interaction_case_cmp_3way (vp->iact, (*vn1)->ccase, (*vn2)->ccase);
}


struct categoricals
{
  /* The weight variable */
  const struct variable *wv;

  /* An array of interact_params */
  struct interact_params *iap;

  /* The size of IAP. (ie, the number of interactions involved.) */
  size_t n_iap;

  /* The number of categorical variables which contain entries.
     In the absence of missing values, this will be equal to N_IAP */
  size_t n_vars;

  /* A map to enable the lookup of variables indexed by subscript.
     This map considers only the N - 1 of the N variables.
   */
  int *reverse_variable_map_short;

  /* Like the above, but uses all N variables */
  int *reverse_variable_map_long;

  size_t n_cats_total;

  struct pool *pool;

  /* Missing values to be excluded */
  enum mv_class exclude;

  /* Function to be called on each update */
  update_func *update;

  /* Function specified by the caller to create user_data */
  user_data_create_func *user_data_create;

  /* Auxilliary data to be passed to update and user_data_create_func*/
  void *aux1;
  void *aux2;
};


void
categoricals_destroy ( struct categoricals *cat)
{
  int i;
  if (cat != NULL)
    {
      for (i = 0 ; i < cat->n_iap; ++i)
	{
	  struct hmap *map = &cat->iap[i].map;
	  struct value_node *nn;

	  HMAP_FOR_EACH (nn, struct value_node, node, map)
	    {
	      case_unref (nn->ccase);
	    }	  
	  
	  hmap_destroy (map);
	}
      
      pool_destroy (cat->pool);
      free (cat);
    }
}


#if 0
void
categoricals_dump (const struct categoricals *cat)
{
  int v;

  for (v = 0 ; v < cat->n_iap; ++v)
    {
      const struct interact_params *vp = &cat->iap[v];
      const struct hmap *m = &vp->map;
      struct hmap_node *node ;
      int x;
      struct string str ;
      ds_init_empty (&str);

      interaction_to_string (vp->iact, &str);
      printf ("\n%s (%d)  CC=%g n_cats=%d:\n", 
	      ds_cstr (&str),
	      vp->base_subscript_long, vp->cc, vp->n_cats);

#if 0
      printf ("Reverse map\n");
      for (x = 0 ; x < vp->n_cats; ++x)
	{
	  struct string s;
	  const struct value_node *vn = vp->reverse_value_map[x];
	  ds_init_empty (&s);
	  var_append_value_name (vp->var, &vn->value, &s);
	  printf ("Value for %d is %s\n", x, ds_cstr(&s));
	  ds_destroy (&s);
	}

      printf ("\nForward map\n");
      for (node = hmap_first (m); node; node = hmap_next (m, node))
	{
	  struct string s;
	  const struct value_node *vn = HMAP_DATA (node, struct value_node, node);
	  ds_init_empty (&s);
	  var_append_value_name (vp->var, &vn->value, &s);
	  printf ("Value: %s; Index %d; CC %g\n",
		  ds_cstr (&s),
		  vn->subscript, vn->cc);
	  ds_destroy (&s);
	}
#endif
    }

  assert (cat->n_vars <= cat->n_iap);

  printf ("\n");
  printf ("Number of interactions: %d\n", cat->n_iap);
  printf ("Number of non-empty categorical variables: %d\n", cat->n_vars);
  printf ("Total number of categories: %d\n", cat->n_cats_total);

  printf ("\nReverse variable map (short):\n");
  for (v = 0 ; v < cat->n_cats_total - cat->n_vars; ++v)
    printf ("%d ", cat->reverse_variable_map_short[v]);

  printf ("\nReverse variable map (long):\n");
  for (v = 0 ; v < cat->n_cats_total; ++v)
    printf ("%d ", cat->reverse_variable_map_long[v]);

  printf ("\n");
}
#endif


static struct value_node *
lookup_case (const struct hmap *map, const struct interaction *iact, const struct ccase *c)
{
  struct value_node *nn;
  size_t hash = interaction_case_hash (iact, c);

  HMAP_FOR_EACH_WITH_HASH (nn, struct value_node, node, hash, map)
    {
      if (interaction_case_equal (iact, c, nn->ccase))
	break;

      fprintf (stderr, "Warning: Hash table collision\n");
    }

  return nn;
}


struct categoricals *
categoricals_create (const struct interaction **inter, size_t n_inter,
		     const struct variable *wv, enum mv_class exclude,
		     user_data_create_func *udf,
		     update_func *update, void *aux1, void *aux2
		     )
{
  size_t i;
  struct categoricals *cat = xmalloc (sizeof *cat);
  
  cat->n_iap = n_inter;
  cat->wv = wv;
  cat->n_cats_total = 0;
  cat->n_vars = 0;
  cat->reverse_variable_map_short = NULL;
  cat->reverse_variable_map_long = NULL;
  cat->pool = pool_create ();
  cat->exclude = exclude;
  cat->update = update;
  cat->user_data_create = udf;

  cat->aux1 = aux1;
  cat->aux2 = aux2;


  cat->iap = pool_calloc (cat->pool, cat->n_iap, sizeof *cat->iap);

  for (i = 0 ; i < cat->n_iap; ++i)
    {
      hmap_init (&cat->iap[i].map);
      cat->iap[i].iact = inter[i];
    }

  return cat;
}



void
categoricals_update (struct categoricals *cat, const struct ccase *c)
{
  size_t i;
  
  const double weight = cat->wv ? case_data (c, cat->wv)->f : 1.0;

  assert (NULL == cat->reverse_variable_map_short);
  assert (NULL == cat->reverse_variable_map_long);

  for (i = 0 ; i < cat->n_iap; ++i)
    {
      const struct interaction *iact = cat->iap[i].iact;
      size_t hash;
      struct value_node *node ;

      if ( interaction_case_is_missing (iact, c, cat->exclude))
	continue;

      hash = interaction_case_hash (iact, c);
      node = lookup_case (&cat->iap[i].map, iact, c);

      if ( NULL == node)
	{
	  node = pool_malloc (cat->pool, sizeof *node);

	  node->ccase = case_ref (c);
	  node->cc = 0.0;

	  hmap_insert (&cat->iap[i].map, &node->node,  hash);
	  cat->n_cats_total++;
	  
	  if ( 0 == cat->iap[i].n_cats)
	    cat->n_vars++;

	  node->subscript = cat->iap[i].n_cats++ ;

	  if (cat->user_data_create)
	    node->user_data = cat->user_data_create (cat->aux1, cat->aux2);
	}

      node->cc += weight;
      cat->iap[i].cc += weight;

      if (cat->update)
	cat->update (node->user_data, cat->exclude, cat->wv, NULL, c, cat->aux1, cat->aux2);
    }
}

/* Return the number of categories (distinct values) for variable N */
size_t
categoricals_n_count (const struct categoricals *cat, size_t n)
{
  return hmap_count (&cat->iap[n].map);
}


/* Return the total number of categories */
size_t
categoricals_total (const struct categoricals *cat)
{
  return cat->n_cats_total;
}


/* This function must be called *before* any call to categoricals_get_*_by subscript and
 *after* all calls to categoricals_update */
void
categoricals_done (const struct categoricals *cat_)
{
  /* Implementation Note: Whilst this function is O(n) in cat->n_cats_total, in most
     uses it will be more efficient that using a tree based structure, since it
     is called only once, and means that subsequent lookups will be O(1).

     1 call of O(n) + 10^9 calls of O(1) is better than 10^9 calls of O(log n).
  */
  struct categoricals *cat = CONST_CAST (struct categoricals *, cat_);
  int v;
  int idx_short = 0;
  int idx_long = 0;
  cat->reverse_variable_map_short = pool_calloc (cat->pool,
						 cat->n_cats_total - cat->n_vars,
						 sizeof *cat->reverse_variable_map_short);

  cat->reverse_variable_map_long = pool_calloc (cat->pool,
						cat->n_cats_total,
						sizeof *cat->reverse_variable_map_long);
  
  for (v = 0 ; v < cat->n_iap; ++v)
    {
      int i;
      struct interact_params *vp = &cat->iap[v];
      int n_cats_total = categoricals_n_count (cat, v);
      struct hmap_node *node ;

      vp->reverse_value_map = pool_calloc (cat->pool, n_cats_total, sizeof *vp->reverse_value_map);

      vp->base_subscript_short = idx_short;
      vp->base_subscript_long = idx_long;

      for (node = hmap_first (&vp->map); node; node = hmap_next (&vp->map, node))
	{
	  const struct value_node *vn = HMAP_DATA (node, struct value_node, node);
	  vp->reverse_value_map[vn->subscript] = vn;
	}

      /* For some purposes (eg CONTRASTS in ONEWAY) the values need to be sorted */
      sort (vp->reverse_value_map, vp->n_cats, sizeof (const struct value_node *),
	    compare_value_node, vp);

      /* Populate the reverse variable maps. */
      for (i = 0; i < vp->n_cats - 1; ++i)
	cat->reverse_variable_map_short[idx_short++] = v;

      for (i = 0; i < vp->n_cats; ++i)
	cat->reverse_variable_map_long[idx_long++] = v;
    }

  assert (cat->n_vars <= cat->n_iap);
}


static int
reverse_variable_lookup_short (const struct categoricals *cat, int subscript)
{
  assert (cat->reverse_variable_map_short);
  assert (subscript >= 0);
  assert (subscript < cat->n_cats_total - cat->n_vars);

  return cat->reverse_variable_map_short[subscript];
}

static int
reverse_variable_lookup_long (const struct categoricals *cat, int subscript)
{
  assert (cat->reverse_variable_map_long);
  assert (subscript >= 0);
  assert (subscript < cat->n_cats_total);

  return cat->reverse_variable_map_long[subscript];
}


/* Return the interaction corresponding to SUBSCRIPT */
const struct interaction *
categoricals_get_interaction_by_subscript (const struct categoricals *cat, int subscript)
{
  int index = reverse_variable_lookup_short (cat, subscript);

  return cat->iap[index].iact;
}


/* Return the case corresponding to SUBSCRIPT */
static const struct ccase *
categoricals_get_case_by_subscript (const struct categoricals *cat, int subscript)
{
  int vindex = reverse_variable_lookup_short (cat, subscript);
  const struct interact_params *vp = &cat->iap[vindex];
  const struct value_node *vn = vp->reverse_value_map [subscript - vp->base_subscript_short];

  return vn->ccase;
}


double
categoricals_get_weight_by_subscript (const struct categoricals *cat, int subscript)
{
  int vindex = reverse_variable_lookup_short (cat, subscript);
  const struct interact_params *vp = &cat->iap[vindex];

  return vp->cc;
}

double
categoricals_get_sum_by_subscript (const struct categoricals *cat, int subscript)
{
  int vindex = reverse_variable_lookup_short (cat, subscript);
  const struct interact_params *vp = &cat->iap[vindex];

  const struct value_node *vn = vp->reverse_value_map [subscript - vp->base_subscript_short];
  return vn->cc;
}


/* Returns unity if the value in case C at SUBSCRIPT is equal to the category
   for that subscript */
double
categoricals_get_binary_by_subscript (const struct categoricals *cat, int subscript,
				      const struct ccase *c)
{
  const struct interaction *iact = categoricals_get_interaction_by_subscript (cat, subscript);

  const struct ccase *c2 =  categoricals_get_case_by_subscript (cat, subscript);

  return interaction_case_equal (iact, c, c2);
}


size_t
categoricals_get_n_variables (const struct categoricals *cat)
{
  return cat->n_vars;
}


/* Return a case containing the set of values corresponding to SUBSCRIPT */
const struct ccase *
categoricals_get_case_by_category (const struct categoricals *cat, int subscript)
{
  int vindex = reverse_variable_lookup_long (cat, subscript);
  const struct interact_params *vp = &cat->iap[vindex];
  const struct value_node *vn = vp->reverse_value_map [subscript - vp->base_subscript_long];

  return vn->ccase;
}


void *
categoricals_get_user_data_by_category (const struct categoricals *cat, int subscript)
{
  int vindex = reverse_variable_lookup_long (cat, subscript);
  const struct interact_params *vp = &cat->iap[vindex];

  const struct value_node *vn = vp->reverse_value_map [subscript - vp->base_subscript_long];
  return vn->user_data;
}
