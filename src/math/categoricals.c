/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011, 2012, 2014 Free Software Foundation, Inc.

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

#include <float.h>
#include <stdio.h>

#include "data/case.h"
#include "data/value.h"
#include "data/variable.h"
#include "libpspp/array.h"
#include "libpspp/hmap.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "libpspp/hash-functions.h"

#include "gl/xalloc.h"

#define CATEGORICALS_DEBUG 0

struct value_node
{
  struct hmap_node node;      /* Node in hash map. */

  union value val;            /* The value */

  int index;                  /* A zero based unique index for this value */
};


struct interaction_value
{
  struct hmap_node node;      /* Node in hash map */

  struct ccase *ccase;        /* A case (probably the first in the dataset) which matches
				 this value */

  double cc;                  /* Total of the weights of cases matching this interaction */

  void *user_data;            /* A pointer to data which the caller can store stuff */
};

static struct value_node *
lookup_value (const struct hmap *map, const union value *val, unsigned int hash, int width)
{
  struct value_node *vn = NULL;
  HMAP_FOR_EACH_WITH_HASH (vn, struct value_node, node, hash, map)
    {
      if (value_equal (&vn->val, val, width))
	break;
    }
  
  return vn;
}

struct variable_node
{
  struct hmap_node node;      /* Node in hash map. */
  const struct variable *var; /* The variable */

  struct hmap valmap;         /* A map of value nodes */
  int n_vals;                 /* Number of values for this variable */
};


/* Comparison function to sort value_nodes in ascending order */
static int
compare_value_node_3way (const void *vn1_, const void *vn2_, const void *aux)
{
  const struct value_node *const *vn1p = vn1_;
  const struct value_node *const *vn2p = vn2_;

  const struct variable_node *vn = aux;


  return value_compare_3way (&(*vn1p)->val, &(*vn2p)->val, var_get_width (vn->var));
}



static struct variable_node *
lookup_variable (const struct hmap *map, const struct variable *var, unsigned int hash)
{
  struct variable_node *vn = NULL;
  HMAP_FOR_EACH_WITH_HASH (vn, struct variable_node, node, hash, map)
    {
      if (vn->var == var)
	break;
      
      fprintf (stderr, "%s:%d Warning: Hash table collision\n", __FILE__, __LINE__);
    }
  
  return vn;
}


struct interact_params
{
  /* A map of cases indexed by a interaction_value */
  struct hmap ivmap;

  const struct interaction *iact;

  int base_subscript_short;
  int base_subscript_long;

  /* The number of distinct values of this interaction */
  int n_cats;

  /* An array of integers df_n * df_{n-1} * df_{n-2} ...
     These are the products of the degrees of freedom for the current 
     variable and all preceeding variables */
  int *df_prod; 

  double *enc_sum;

  /* A map of interaction_values indexed by subscript */
  struct interaction_value **reverse_interaction_value_map;

  double cc;
};


/* Comparison function to sort the reverse_value_map in ascending order */
static int
compare_interaction_value_3way (const void *vn1_, const void *vn2_, const void *aux)
{
  const struct interaction_value *const *vn1p = vn1_;
  const struct interaction_value *const *vn2p = vn2_;

  const struct interact_params *iap = aux;

  return interaction_case_cmp_3way (iap->iact, (*vn1p)->ccase, (*vn2p)->ccase);
}

struct categoricals
{
  /* The weight variable */
  const struct variable *wv;

  /* An array of interact_params */
  struct interact_params *iap;

  /* Map whose members are the union of the variables which comprise IAP */
  struct hmap varmap;

  /* The size of IAP. (ie, the number of interactions involved.) */
  size_t n_iap;

  /* The number of categorical variables which contain entries.
     In the absence of missing values, this will be equal to N_IAP */
  size_t n_vars;

  size_t df_sum;

  /* A map to enable the lookup of variables indexed by subscript.
     This map considers only the N - 1 of the N variables.
  */
  int *reverse_variable_map_short;

  /* Like the above, but uses all N variables */
  int *reverse_variable_map_long;

  size_t n_cats_total;

  struct pool *pool;

  /* Missing values in the dependent varirable to be excluded */
  enum mv_class dep_excl;

  /* Missing values in the factor variables to be excluded */
  enum mv_class fctr_excl;

  const void *aux1;
  void *aux2;

  bool sane;

  const struct payload *payload;
};

static void
categoricals_dump (const struct categoricals *cat)
{
  if (CATEGORICALS_DEBUG)
    {
      int i;

      printf ("Reverse Variable Map (short):\n");
      for (i = 0; i < cat->df_sum; ++i)
	{
	  printf (" %d", cat->reverse_variable_map_short[i]);
	}
      printf ("\n");

      printf ("Reverse Variable Map (long):\n");
      for (i = 0; i < cat->n_cats_total; ++i)
	{
	  printf (" %d", cat->reverse_variable_map_long[i]);
	}
      printf ("\n");

      printf ("Number of interactions %d\n", cat->n_iap);
      for (i = 0 ; i < cat->n_iap; ++i)
	{
	  int v;
	  struct string str;
	  const struct interact_params *iap = &cat->iap[i];
	  const struct interaction *iact = iap->iact;

	  ds_init_empty (&str);
	  interaction_to_string (iact, &str);

	  printf ("\nInteraction: \"%s\" (number of categories: %d); ", ds_cstr (&str), iap->n_cats);
	  ds_destroy (&str);
	  printf ("Base index (short/long): %d/%d\n", iap->base_subscript_short, iap->base_subscript_long);

	  printf ("\t(");
	  for (v = 0; v < hmap_count (&iap->ivmap); ++v)
	    {
	      int vv;
	      const struct interaction_value *iv = iap->reverse_interaction_value_map[v];

	      if (v > 0)  printf ("   ");
	      printf ("{");
	      for (vv = 0; vv < iact->n_vars; ++vv)
		{
		  const struct variable *var = iact->vars[vv];
		  const union value *val = case_data (iv->ccase, var);
		  unsigned int varhash = hash_pointer (var, 0);
		  struct variable_node *vn = lookup_variable (&cat->varmap, var, varhash);

		  const int width = var_get_width (var);
		  unsigned int valhash = value_hash (val, width, 0);
		  struct value_node *valn = lookup_value (&vn->valmap, val, valhash, width);

		  assert (vn->var == var);

		  printf ("%.*g(%d)", DBL_DIG + 1, val->f, valn->index);
		  if (vv < iact->n_vars - 1)
		    printf (", ");
		}
	      printf ("}");
	    }
	  printf (")\n");
	}
    }
}

void
categoricals_destroy (struct categoricals *cat)
{
  struct variable_node *vn = NULL;
  int i;
  if (NULL == cat)
    return;

  for (i = 0; i < cat->n_iap; ++i)
    {
      struct interaction_value *iv = NULL;
      /* Interate over each interaction value, and unref any cases that we reffed */
      HMAP_FOR_EACH (iv, struct interaction_value, node, &cat->iap[i].ivmap)
	{
	  if (cat->payload && cat->payload->destroy)
	    cat->payload->destroy (cat->aux1, cat->aux2, iv->user_data);
	  case_unref (iv->ccase);
	}

      free (cat->iap[i].enc_sum);
      free (cat->iap[i].df_prod);
      hmap_destroy (&cat->iap[i].ivmap);
    }

  /* Interate over each variable and delete its value map */
  HMAP_FOR_EACH (vn, struct variable_node, node, &cat->varmap)
    {
      hmap_destroy (&vn->valmap);
    }

  hmap_destroy (&cat->varmap);

  pool_destroy (cat->pool);

  free (cat);
}



static struct interaction_value *
lookup_case (const struct hmap *map, const struct interaction *iact, const struct ccase *c)
{
  struct interaction_value *iv = NULL;
  size_t hash = interaction_case_hash (iact, c, 0);

  HMAP_FOR_EACH_WITH_HASH (iv, struct interaction_value, node, hash, map)
    {
      if (interaction_case_equal (iact, c, iv->ccase))
	break;

      fprintf (stderr, "Warning: Hash table collision\n");
    }

  return iv;
}

bool 
categoricals_sane (const struct categoricals *cat)
{
  return cat->sane;
}

struct categoricals *
categoricals_create (struct interaction *const*inter, size_t n_inter,
		     const struct variable *wv, enum mv_class dep_excl, enum mv_class fctr_excl)
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
  cat->dep_excl = dep_excl;
  cat->fctr_excl = fctr_excl;
  cat->payload = NULL;
  cat->aux2 = NULL;
  cat->sane = false;

  cat->iap = pool_calloc (cat->pool, cat->n_iap, sizeof *cat->iap);

  hmap_init (&cat->varmap);
  for (i = 0 ; i < cat->n_iap; ++i)
    {
      int v;
      hmap_init (&cat->iap[i].ivmap);
      cat->iap[i].iact = inter[i];
      cat->iap[i].cc = 0.0;
      for (v = 0; v < inter[i]->n_vars; ++v)
	{
	  const struct variable *var = inter[i]->vars[v];
	  unsigned int hash = hash_pointer (var, 0);
	  struct variable_node *vn = lookup_variable (&cat->varmap, var, hash);
	  if (vn == NULL)
	    {
	      vn = pool_malloc (cat->pool, sizeof *vn);
	      vn->var = var;
	      vn->n_vals = 0;
	      hmap_init (&vn->valmap);

	      hmap_insert (&cat->varmap, &vn->node,  hash);
	    }
	}
    }

  return cat;
}



void
categoricals_update (struct categoricals *cat, const struct ccase *c)
{
  int i;
  struct variable_node *vn = NULL;
  double weight;

  if (NULL == cat)
    return;

  weight = cat->wv ? case_data (c, cat->wv)->f : 1.0;

  assert (NULL == cat->reverse_variable_map_short);
  assert (NULL == cat->reverse_variable_map_long);

  /* Interate over each variable, and add the value of that variable
     to the appropriate map, if it's not already present. */
  HMAP_FOR_EACH (vn, struct variable_node, node, &cat->varmap)
    {
      const int width = var_get_width (vn->var);
      const union value *val = case_data (c, vn->var);
      unsigned int hash = value_hash (val, width, 0);

      struct value_node *valn = lookup_value (&vn->valmap, val, hash, width);
      if (valn == NULL)
	{
	  valn = pool_malloc (cat->pool, sizeof *valn);
	  valn->index = -1; 
	  vn->n_vals++;
	  value_init (&valn->val, width);
	  value_copy (&valn->val, val, width);
	  hmap_insert (&vn->valmap, &valn->node, hash);
	}
    }
  
  for (i = 0 ; i < cat->n_iap; ++i)
    {
      const struct interaction *iact = cat->iap[i].iact;

      size_t hash;
      struct interaction_value *node;

      if ( interaction_case_is_missing (iact, c, cat->fctr_excl))
	continue;

      hash = interaction_case_hash (iact, c, 0);
      node = lookup_case (&cat->iap[i].ivmap, iact, c);

      if ( NULL == node)
	{
	  node = pool_malloc (cat->pool, sizeof *node);
	  node->ccase = case_ref (c);
	  node->cc = weight;

	  hmap_insert (&cat->iap[i].ivmap, &node->node, hash);

	  if (cat->payload) 
	    {
	      node->user_data = cat->payload->create (cat->aux1, cat->aux2);
	    }
	}
      else
	{
	  node->cc += weight;
	}
      cat->iap[i].cc += weight;

      if (cat->payload)
	{
	  double weight = cat->wv ? case_data (c, cat->wv)->f : 1.0;
	  cat->payload->update (cat->aux1, cat->aux2, node->user_data, c, weight);
	}

    }
}

/* Return the number of categories (distinct values) for interction N */
size_t
categoricals_n_count (const struct categoricals *cat, size_t n)
{
  return hmap_count (&cat->iap[n].ivmap);
}


size_t
categoricals_df (const struct categoricals *cat, size_t n)
{
  const struct interact_params *iap = &cat->iap[n];
  return iap->df_prod[iap->iact->n_vars - 1];
}


/* Return the total number of categories */
size_t
categoricals_n_total (const struct categoricals *cat)
{
  if (!categoricals_is_complete (cat))
    return 0;

  return cat->n_cats_total;
}

size_t
categoricals_df_total (const struct categoricals *cat)
{
  if (NULL == cat)
    return 0;

  return cat->df_sum;
}

bool
categoricals_is_complete (const struct categoricals *cat)
{
  return (NULL != cat->reverse_variable_map_short);
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
  int i;
  int idx_short = 0;
  int idx_long = 0;

  if (NULL == cat)
    return;

  cat->df_sum = 0;
  cat->n_cats_total = 0;

  /* Calculate the degrees of freedom, and the number of categories */
  for (i = 0 ; i < cat->n_iap; ++i)
    {
      int df = 1;
      const struct interaction *iact = cat->iap[i].iact;

      cat->iap[i].df_prod = iact->n_vars ? xcalloc (iact->n_vars, sizeof (int)) : NULL;

      cat->iap[i].n_cats = 1;
      
      for (v = 0 ; v < iact->n_vars; ++v)
	{
	  int x;
	  const struct variable *var = iact->vars[v];

	  struct variable_node *vn = lookup_variable (&cat->varmap, var, hash_pointer (var, 0));

	  struct value_node *valnd = NULL;
	  struct value_node **array ;

	  assert (vn->n_vals == hmap_count (&vn->valmap));

	  if  (vn->n_vals == 0)
	    {
	      cat->sane = false;
	      return;
	    }

	  /* Sort the VALMAP here */
	  array = xcalloc (sizeof *array, vn->n_vals);
	  x = 0;
	  HMAP_FOR_EACH (valnd, struct value_node, node, &vn->valmap)
	    {
	      /* Note: This loop is probably superfluous, it could be done in the 
	       update stage (at the expense of a realloc) */
	      array[x++] = valnd;
	    }

	  sort (array, vn->n_vals, sizeof (*array), 
		compare_value_node_3way, vn);

	  for (x = 0; x <  vn->n_vals; ++x)
	    {
	      struct value_node *vvv = array[x];
	      vvv->index = x;
	    }
	  free (array);

	  cat->iap[i].df_prod[v] = df * (vn->n_vals - 1);
      	  df = cat->iap[i].df_prod[v];

	  cat->iap[i].n_cats *= vn->n_vals;
	}

      if (v > 0)
	cat->df_sum += cat->iap[i].df_prod [v - 1];

      cat->n_cats_total += cat->iap[i].n_cats;
    }


  cat->reverse_variable_map_short = pool_calloc (cat->pool,
						 cat->df_sum,
						 sizeof *cat->reverse_variable_map_short);

  cat->reverse_variable_map_long = pool_calloc (cat->pool,
						cat->n_cats_total,
						sizeof *cat->reverse_variable_map_long);

  for (i = 0 ; i < cat->n_iap; ++i)
    {
      struct interaction_value *ivn = NULL;
      int x = 0;
      int ii;
      struct interact_params *iap = &cat->iap[i];

      iap->base_subscript_short = idx_short;
      iap->base_subscript_long = idx_long;

      iap->reverse_interaction_value_map = pool_calloc (cat->pool, iap->n_cats,
							sizeof *iap->reverse_interaction_value_map);

      HMAP_FOR_EACH (ivn, struct interaction_value, node, &iap->ivmap)
	{
	  iap->reverse_interaction_value_map[x++] = ivn;
	}

      assert (x <= iap->n_cats);

      /* For some purposes (eg CONTRASTS in ONEWAY) the values need to be sorted */
      sort (iap->reverse_interaction_value_map, x, sizeof (*iap->reverse_interaction_value_map),
	    compare_interaction_value_3way, iap);

      /* Fill the remaining values with null */
      for (ii = x ; ii < iap->n_cats; ++ii)
	iap->reverse_interaction_value_map[ii] = NULL;

      /* Populate the reverse variable maps. */
      if (iap->df_prod)
	{
	  for (ii = 0; ii < iap->df_prod [iap->iact->n_vars - 1]; ++ii)
	    cat->reverse_variable_map_short[idx_short++] = i;
	}

      for (ii = 0; ii < iap->n_cats; ++ii)
	cat->reverse_variable_map_long[idx_long++] = i;
    }

  assert (cat->n_vars <= cat->n_iap);

  categoricals_dump (cat);

  /* Tally up the sums for all the encodings */
  for (i = 0 ; i < cat->n_iap; ++i)
    {
      int x, y;
      struct interact_params *iap = &cat->iap[i];
      const struct interaction *iact = iap->iact;

      const int df = iap->df_prod ? iap->df_prod [iact->n_vars - 1] : 0;

      iap->enc_sum = xcalloc (df, sizeof (*(iap->enc_sum)));

      for (y = 0; y < hmap_count (&iap->ivmap); ++y)
	{
	  struct interaction_value *iv = iap->reverse_interaction_value_map[y];
	  for (x = iap->base_subscript_short; x < iap->base_subscript_short + df ;++x)
	    {
	      const double bin = categoricals_get_effects_code_for_case (cat, x, iv->ccase);
	      iap->enc_sum [x - iap->base_subscript_short] += bin * iv->cc;
	    }
	  if (cat->payload && cat->payload->calculate)
	    cat->payload->calculate (cat->aux1, cat->aux2, iv->user_data);
	}
    }

  cat->sane = true;
}


static int
reverse_variable_lookup_short (const struct categoricals *cat, int subscript)
{
  assert (cat->reverse_variable_map_short);
  assert (subscript >= 0);
  assert (subscript < cat->df_sum);

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

  return   vp->enc_sum[subscript - vp->base_subscript_short];
}


/* Returns unity if the value in case C at SUBSCRIPT is equal to the category
   for that subscript */
static double
categoricals_get_code_for_case (const struct categoricals *cat, int subscript,
				const struct ccase *c,
				bool effects_coding)
{
  const struct interaction *iact = categoricals_get_interaction_by_subscript (cat, subscript);

  const int i = reverse_variable_lookup_short (cat, subscript);

  const int base_index = cat->iap[i].base_subscript_short;

  int v;
  double result = 1.0;

  const struct interact_params *iap = &cat->iap[i];

  double dfp = 1.0;
  for (v = 0; v < iact->n_vars; ++v)
    {
      const struct variable *var = iact->vars[v];

      const union value *val = case_data (c, var);
      const int width = var_get_width (var);
      const struct variable_node *vn = lookup_variable (&cat->varmap, var, hash_pointer (var, 0));

      const unsigned int hash = value_hash (val, width, 0);
      const struct value_node *valn = lookup_value (&vn->valmap, val, hash, width);

      double bin = 1.0;

      const double df = iap->df_prod[v] / dfp;

      /* Translate the subscript into an index for the individual variable */
      const int index = ((subscript - base_index) % iap->df_prod[v] ) / dfp;
      dfp = iap->df_prod [v];

      if (effects_coding && valn->index == df )
	bin = -1.0;
      else if ( valn->index != index )
	bin = 0;
    
      result *= bin;
    }

  return result;
}


/* Returns unity if the value in case C at SUBSCRIPT is equal to the category
   for that subscript */
double
categoricals_get_dummy_code_for_case (const struct categoricals *cat, int subscript,
				     const struct ccase *c)
{
  return categoricals_get_code_for_case (cat, subscript, c, false);
}

/* Returns unity if the value in case C at SUBSCRIPT is equal to the category
   for that subscript. 
   Else if it is the last category, return -1.
   Otherwise return 0.
 */
double
categoricals_get_effects_code_for_case (const struct categoricals *cat, int subscript,
					const struct ccase *c)
{
  return categoricals_get_code_for_case (cat, subscript, c, true);
}


size_t
categoricals_get_n_variables (const struct categoricals *cat)
{
  printf ("%s\n", __FUNCTION__);
  return cat->n_vars;
}


/* Return a case containing the set of values corresponding to 
   the Nth Category of the IACTth interaction */
const struct ccase *
categoricals_get_case_by_category_real (const struct categoricals *cat, int iact, int n)
{
  const struct interaction_value *vn;

  const struct interact_params *vp = &cat->iap[iact];

  if ( n >= hmap_count (&vp->ivmap))
    return NULL;

  vn = vp->reverse_interaction_value_map [n];

  return vn->ccase;
}

/* Return a the user data corresponding to the Nth Category of the IACTth interaction. */
void *
categoricals_get_user_data_by_category_real (const struct categoricals *cat, int iact, int n)
{
  const struct interact_params *vp = &cat->iap[iact];
  const struct interaction_value *iv ;

  if ( n >= hmap_count (&vp->ivmap))
    return NULL;

  iv = vp->reverse_interaction_value_map [n];

  return iv->user_data;
}



/* Return a case containing the set of values corresponding to SUBSCRIPT */
const struct ccase *
categoricals_get_case_by_category (const struct categoricals *cat, int subscript)
{
  int vindex = reverse_variable_lookup_long (cat, subscript);
  const struct interact_params *vp = &cat->iap[vindex];
  const struct interaction_value *vn = vp->reverse_interaction_value_map [subscript - vp->base_subscript_long];

  return vn->ccase;
}

void *
categoricals_get_user_data_by_category (const struct categoricals *cat, int subscript)
{
  int vindex = reverse_variable_lookup_long (cat, subscript);
  const struct interact_params *vp = &cat->iap[vindex];

  const struct interaction_value *iv = vp->reverse_interaction_value_map [subscript - vp->base_subscript_long];
  return iv->user_data;
}




void
categoricals_set_payload (struct categoricals *cat, const struct payload *p,
			  const void *aux1, void *aux2)
{
  cat->payload = p;
  cat->aux1 = aux1;
  cat->aux2 = aux2;
}
