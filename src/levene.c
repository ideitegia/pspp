/* This file is part of GNU PSPP 
   Computes Levene test  statistic.

   Copyright (C) 2004 Free Software Foundation, Inc.
   Written by John Darrington <john@darrington.wattle.id.au>

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
#include "levene.h"
#include "error.h"
#include "case.h"
#include "casefile.h"
#include "dictionary.h"
#include "group_proc.h"
#include "hash.h"
#include "str.h"
#include "var.h"
#include "vfm.h"
#include "alloc.h"
#include "misc.h"
#include "group.h"

#include <math.h>
#include <stdlib.h>


/* This module calculates the Levene statistic for variables.

   Just for reference, the Levene Statistic is a defines as follows:

   W = \frac{ (n-k)\sum_{i=1}^k n_i(Z_{iL} - Z_{LL})^2}
            { (k-1)\sum_{i=1}^k \sum_{j=1}^{n_i} (Z_{ij} - Z_{iL})^2}

   where:
        k is the number of groups
	n is the total number of samples
        n_i is the number of samples in the ith group
        Z_{ij} is | Y_{ij} - Y_{iL} | where Y_{iL} is the mean of the ith group
	Z_{iL} is the  mean of Z_{ij} over the ith group
	Z_{LL} is the grand mean of Z_{ij}

   Imagine calculating that with pencil and paper!

 */


struct levene_info
{

  /* Per group statistics */
  struct t_test_proc **group_stats;

  /* The independent variable */
  struct variable *v_indep; 

  /* Number of dependent variables */
  size_t n_dep;

  /* The dependent variables */
  struct variable  **v_dep;

  /* How to treat missing values */
  enum lev_missing missing;

  /* Function to test for missing values */
  is_missing_func *is_missing;
};

/* First pass */
static void  levene_precalc (const struct levene_info *l);
static int levene_calc (const struct ccase *, void *);
static void levene_postcalc (void *);


/* Second pass */
static void levene2_precalc (void *);
static int levene2_calc (const struct ccase *, void *);
static void levene2_postcalc (void *);


void  
levene(const struct casefile *cf,
       struct variable *v_indep, size_t n_dep, struct variable **v_dep,
	     enum lev_missing missing,   is_missing_func value_is_missing)
{
  struct casereader *r;
  struct ccase c;
  struct levene_info l;

  l.n_dep      = n_dep;
  l.v_indep    = v_indep;
  l.v_dep      = v_dep;
  l.missing    = missing;
  l.is_missing = value_is_missing;



  levene_precalc(&l);
  for(r = casefile_get_reader (cf);
      casereader_read (r, &c) ;
      case_destroy (&c)) 
    {
      levene_calc(&c,&l);
    }
  casereader_destroy (r);
  levene_postcalc(&l);

  levene2_precalc(&l);
  for(r = casefile_get_reader (cf);
      casereader_read (r, &c) ;
      case_destroy (&c)) 
    {
      levene2_calc(&c,&l);
    }
  casereader_destroy (r);
  levene2_postcalc(&l);

}

/* Internal variables used in calculating the Levene statistic */

/* Per variable statistics */
struct lz_stats
{
  /* Total of all lz */
  double grand_total;

  /* Mean of all lz */
  double grand_mean;

  /* The total number of cases */
  double total_n ; 

  /* Number of groups */
  int n_groups;
};

/* An array of lz_stats for each variable */
static struct lz_stats *lz;


static void 
levene_precalc (const struct levene_info *l)
{
  size_t i;

  lz = xnmalloc (l->n_dep, sizeof *lz);

  for(i = 0; i < l->n_dep ; ++i ) 
    {
      struct variable *var = l->v_dep[i];
      struct group_proc *gp = group_proc_get (var);
      struct group_statistics *gs;
      struct hsh_iterator hi;

      lz[i].grand_total = 0;
      lz[i].total_n = 0;
      lz[i].n_groups = gp->n_groups ; 

      
      for ( gs = hsh_first(gp->group_hash, &hi);
	    gs != 0;
	    gs = hsh_next(gp->group_hash, &hi))
	{
	  gs->lz_total = 0;
	}
	    
    }

}

static int 
levene_calc (const struct ccase *c, void *_l)
{
  size_t i;
  int warn = 0;
  struct levene_info *l = (struct levene_info *) _l;
  const union value *gv = case_data (c, l->v_indep->fv);
  struct group_statistics key;
  double weight = dict_get_case_weight(default_dict,c,&warn); 

  /* Skip the entire case if /MISSING=LISTWISE is set */
  if ( l->missing == LEV_LISTWISE ) 
    {
      for (i = 0; i < l->n_dep; ++i) 
	{
	  struct variable *v = l->v_dep[i];
	  const union value *val = case_data (c, v->fv);

	  if (l->is_missing (&v->miss, val) )
	    {
	      return 0;
	    }
	}
    }

  
  key.id = *gv;

  for (i = 0; i < l->n_dep; ++i) 
    {
      struct variable *var = l->v_dep[i];
      struct group_proc *gp = group_proc_get (var);
      double levene_z;
      const union value *v = case_data (c, var->fv);
      struct group_statistics *gs;

      gs = hsh_find(gp->group_hash,(void *) &key );

      if ( 0 == gs ) 
	continue ;

      if ( ! l->is_missing(&var->miss, v))
	{
	  levene_z= fabs(v->f - gs->mean);
	  lz[i].grand_total += levene_z * weight;
	  lz[i].total_n += weight; 

	  gs->lz_total += levene_z * weight;
	}

    }
  return 0;
}


static void 
levene_postcalc (void *_l)
{
  size_t v;

  struct levene_info *l = (struct levene_info *) _l;

  for (v = 0; v < l->n_dep; ++v) 
    {
      /* This is Z_LL */
      lz[v].grand_mean = lz[v].grand_total / lz[v].total_n ;
    }

  
}


/* The denominator for the expression for the Levene */
static double *lz_denominator;

static void 
levene2_precalc (void *_l)
{
  size_t v;

  struct levene_info *l = (struct levene_info *) _l;

  lz_denominator = xnmalloc (l->n_dep, sizeof *lz_denominator);

  /* This stuff could go in the first post calc . . . */
  for (v = 0; v < l->n_dep; ++v) 
    {
      struct hsh_iterator hi;
      struct group_statistics *g;

      struct variable *var = l->v_dep[v] ;
      struct hsh_table *hash = group_proc_get (var)->group_hash;


      for(g = (struct group_statistics *) hsh_first(hash,&hi);
	  g != 0 ;
	  g = (struct group_statistics *) hsh_next(hash,&hi) )
	{
	  g->lz_mean = g->lz_total / g->n ;
	}
      lz_denominator[v] = 0;
  }
}

static int 
levene2_calc (const struct ccase *c, void *_l)
{
  size_t i;
  int warn = 0;

  struct levene_info *l = (struct levene_info *) _l;

  double weight = dict_get_case_weight(default_dict,c,&warn); 

  const union value *gv = case_data (c, l->v_indep->fv);
  struct group_statistics key;

  /* Skip the entire case if /MISSING=LISTWISE is set */
  if ( l->missing == LEV_LISTWISE ) 
    {
      for (i = 0; i < l->n_dep; ++i) 
	{
	  struct variable *v = l->v_dep[i];
	  const union value *val = case_data (c, v->fv);

	  if (l->is_missing(&v->miss, val) )
	    {
	      return 0;
	    }
	}
    }

  key.id = *gv;

  for (i = 0; i < l->n_dep; ++i) 
    {
      double levene_z;
      struct variable *var = l->v_dep[i] ;
      const union value *v = case_data (c, var->fv);
      struct group_statistics *gs;

      gs = hsh_find(group_proc_get (var)->group_hash,(void *) &key );

      if ( 0 == gs ) 
	continue;

      if ( ! l->is_missing (&var->miss, v) )
	{
	  levene_z = fabs(v->f - gs->mean); 
	  lz_denominator[i] += weight * pow2(levene_z - gs->lz_mean);
	}
    }

  return 0;
}


static void 
levene2_postcalc (void *_l)
{
  size_t v;

  struct levene_info *l = (struct levene_info *) _l;

  for (v = 0; v < l->n_dep; ++v) 
    {
      double lz_numerator = 0;
      struct hsh_iterator hi;
      struct group_statistics *g;

      struct variable *var = l->v_dep[v] ;
      struct group_proc *gp = group_proc_get (var);
      struct hsh_table *hash = gp->group_hash;

      for(g = (struct group_statistics *) hsh_first(hash,&hi);
	  g != 0 ;
	  g = (struct group_statistics *) hsh_next(hash,&hi) )
	{
	  lz_numerator += g->n * pow2(g->lz_mean - lz[v].grand_mean );
	}
      lz_numerator *= ( gp->ugs.n - gp->n_groups );

      lz_denominator[v] *= (gp->n_groups - 1);

      gp->levene = lz_numerator / lz_denominator[v] ;

    }

  /* Now clear up after ourselves */
  free(lz_denominator);
  free(lz);
}

