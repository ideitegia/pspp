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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include <assert.h>
#include "levene.h"
#include "hash.h"
#include "var.h"
#include "vfm.h"
#include "alloc.h"
#include "stats.h"

#include <math.h>


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

static void levene_precalc (void *);
static int levene_calc (struct ccase *, void *);
static void levene_postcalc (void *);


/* Second pass */
static void levene2_precalc (void *);
static int levene2_calc (struct ccase *, void *);
static void levene2_postcalc (void *);


struct levene_info
{

  /* The number of groups */
  int n_groups;

  /* Per group statistics */
  struct t_test_proc **group_stats;

  /* The independent variable */
  struct variable *v_indep; 

  /* Number of dependent variables */
  int n_dep;

  /* The dependent variables */
  struct variable  **v_dep;

};



void  
levene(struct variable *v_indep, int n_dep, struct variable **v_dep)
{
  struct levene_info l;

  l.n_dep=n_dep;
  l.v_indep=v_indep;
  l.v_dep=v_dep;

  procedure(levene_precalc, levene_calc, levene_postcalc, &l);
  procedure(levene2_precalc,levene2_calc,levene2_postcalc,&l);
      
}

static struct hsh_table **hash;

static int 
compare_group_id(const void *a_, const void *b_, void *aux)
{
  const struct group_statistics *a = (struct group_statistics *) a_;
  const struct group_statistics *b = (struct group_statistics *) b_;

  int width = (int) aux;
  
  return compare_values(&a->id, &b->id, width);
}


static unsigned 
hash_group_id(const void *g_, void *aux)
{
  const struct group_statistics *g = (struct group_statistics *) g_;

  int width = (int) aux;

  if ( 0 == width ) 
    return hsh_hash_double (g->id.f);
  else
    return hsh_hash_bytes (g->id.s, width);

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
levene_precalc (void *_l)
{
  int i;
  struct levene_info *l = (struct levene_info *) _l;

  lz  = xmalloc (sizeof (struct lz_stats ) * l->n_dep ) ;

  hash = xmalloc (sizeof ( struct hsh_table *) * l->n_dep );

  for(i=0; i < l->n_dep ; ++i ) 
    {
      struct variable *v = l->v_dep[i];
      int g;
      int number_of_groups = v->p.t_t.n_groups ; 

      hash[i] = hsh_create (l->n_dep * number_of_groups,
			    compare_group_id, hash_group_id,
			    0,(void *) l->v_indep->width);

      lz[i].grand_total = 0;
      lz[i].total_n = 0;
      lz[i].n_groups = number_of_groups;

      for (g = 0 ; g < v->p.t_t.n_groups ; ++g ) 
	{
	  struct group_statistics *gs = &v->p.t_t.gs[g];
	  gs->lz_total=0;
	  hsh_insert(hash[i],gs);
	}
    }

}

static int 
levene_calc (struct ccase *c, void *_l)
{
  int var;
  struct levene_info *l = (struct levene_info *) _l;
  union value *gv = &c->data[l->v_indep->fv];
  struct group_statistics key;
  double weight = dict_get_case_weight(default_dict,c); 
  
  key.id = *gv;

  for (var = 0; var < l->n_dep; ++var) 
    {
      double levene_z;
      union value *v = &c->data[l->v_dep[var]->fv];
      struct group_statistics *gs;
      gs = hsh_find(hash[var],&key);
      assert(0 == compare_values(&gs->id, &key.id, l->v_indep->width));

      /* FIXME: handle SYSMIS properly */

      levene_z= fabs(v->f - gs->mean);
      lz[var].grand_total += levene_z * weight;
      lz[var].total_n += weight; 

      gs->lz_total += levene_z * weight;

    }
  return 0;
}


static void 
levene_postcalc (void *_l)
{
  int v;

  struct levene_info *l = (struct levene_info *) _l;

  for (v = 0; v < l->n_dep; ++v) 
    {
      lz[v].grand_mean = lz[v].grand_total / lz[v].total_n ;

    }

}


/* The denominator for the expression for the Levene */
static double *lz_denominator;

static void 
levene2_precalc (void *_l)
{
  int v;

  struct levene_info *l = (struct levene_info *) _l;

  lz_denominator = (double *) xmalloc(sizeof(double) * l->n_dep);

  /* This stuff could go in the first post calc . . . */
  for (v = 0; v < l->n_dep; ++v) 
    {
      struct hsh_iterator hi;
      struct group_statistics *g;
      for(g = (struct group_statistics *) hsh_first(hash[v],&hi);
	  g != 0 ;
	  g = (struct group_statistics *) hsh_next(hash[v],&hi) )
	{
	  g->lz_mean = g->lz_total/g->n ;
	}
      lz_denominator[v] = 0;
  }
}

static int 
levene2_calc (struct ccase *c, void *_l)
{
  int var;

  struct levene_info *l = (struct levene_info *) _l;

  double weight = dict_get_case_weight(default_dict,c); 

  union value *gv = &c->data[l->v_indep->fv];
  struct group_statistics key;

  key.id = *gv;

  for (var = 0; var < l->n_dep; ++var) 
    {
      double levene_z;
      union value *v = &c->data[l->v_dep[var]->fv];
      struct group_statistics *gs;
      gs = hsh_find(hash[var],&key);
      assert(gs);
      assert(0 == compare_values(&gs->id, &key.id, l->v_indep->width));

      /* FIXME: handle SYSMIS properly */

      levene_z = fabs(v->f - gs->mean); 

      lz_denominator[var] += weight * sqr(levene_z - gs->lz_mean);
    }

  return 0;
}


static void 
levene2_postcalc (void *_l)
{
  int v;

  struct levene_info *l = (struct levene_info *) _l;

  for (v = 0; v < l->n_dep; ++v) 
    {
      double lz_numerator = 0;
      struct hsh_iterator hi;
      struct group_statistics *g;
      for(g = (struct group_statistics *) hsh_first(hash[v],&hi);
	  g != 0 ;
	  g = (struct group_statistics *) hsh_next(hash[v],&hi) )
	{

	  lz_numerator += g->n * sqr(g->lz_mean - lz[v].grand_mean );
      

	}
      lz_numerator *= ( l->v_dep[v]->p.t_t.ugs.n - 
			l->v_dep[v]->p.t_t.n_groups );

      lz_denominator[v] /= (l->v_dep[v]->p.t_t.n_groups - 1);
      
      l->v_dep[v]->p.t_t.levene = lz_numerator/lz_denominator[v] ;
    }

  /* Now clear up after ourselves */
  free(lz_denominator);
  for (v = 0; v < l->n_dep; ++v) 
    {
      hsh_destroy(hash[v]);
    }

  free(hash);
  free(lz);
}


