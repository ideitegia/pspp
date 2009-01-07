/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2009 Free Software Foundation, Inc.

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
#include "levene.h"
#include <libpspp/message.h>
#include <data/case.h>
#include <data/casereader.h>
#include <data/dictionary.h>
#include "group-proc.h"
#include <libpspp/hash.h>
#include <libpspp/str.h>
#include <data/variable.h>
#include <data/procedure.h>
#include <libpspp/misc.h>
#include "group.h"

#include <math.h>
#include <stdlib.h>

#include "xalloc.h"


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
  const struct variable *v_indep;

  /* Number of dependent variables */
  size_t n_dep;

  /* The dependent variables */
  const struct variable  **v_dep;

  /* Filter for missing values */
  enum mv_class exclude;

  /* An array of lz_stats for each variable */
  struct lz_stats *lz;

  /* The denominator for the expression for the Levene */
  double *lz_denominator;

};

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

/* First pass */
static void  levene_precalc (const struct levene_info *l);
static int levene_calc (const struct dictionary *dict, const struct ccase *,
			const struct levene_info *l);
static void levene_postcalc (struct levene_info *);


/* Second pass */
static void levene2_precalc (struct levene_info *l);
static int levene2_calc (const struct dictionary *, const struct ccase *,
			 struct levene_info *l);
static void levene2_postcalc (struct levene_info *);


void
levene(const struct dictionary *dict,
       struct casereader *reader,
       const struct variable *v_indep, size_t n_dep,
       const struct variable **v_dep,
       enum mv_class exclude)
{
  struct casereader *pass1, *pass2;
  struct ccase *c;
  struct levene_info l;

  l.n_dep      = n_dep;
  l.v_indep    = v_indep;
  l.v_dep      = v_dep;
  l.exclude    = exclude;
  l.lz         = xnmalloc (l.n_dep, sizeof *l.lz);
  l.lz_denominator = xnmalloc (l.n_dep, sizeof *l.lz_denominator);

  casereader_split (reader, &pass1, &pass2);

  levene_precalc (&l);
  for (; (c = casereader_read (pass1)) != NULL; case_unref (c))
    levene_calc (dict, c, &l);
  casereader_destroy (pass1);
  levene_postcalc (&l);

  levene2_precalc(&l);
  for (; (c = casereader_read (pass2)) != NULL; case_unref (c))
    levene2_calc (dict, c, &l);
  casereader_destroy (pass2);
  levene2_postcalc (&l);

  free (l.lz_denominator);
  free (l.lz);
}

static void
levene_precalc (const struct levene_info *l)
{
  size_t i;

  for(i = 0; i < l->n_dep ; ++i )
    {
      const struct variable *var = l->v_dep[i];
      struct group_proc *gp = group_proc_get (var);
      struct group_statistics *gs;
      struct hsh_iterator hi;

      l->lz[i].grand_total = 0;
      l->lz[i].total_n = 0;
      l->lz[i].n_groups = gp->n_groups ;


      for ( gs = hsh_first(gp->group_hash, &hi);
	    gs != 0;
	    gs = hsh_next(gp->group_hash, &hi))
	{
	  gs->lz_total = 0;
	}

    }

}

static int
levene_calc (const struct dictionary *dict, const struct ccase *c,
	     const struct levene_info *l)
{
  size_t i;
  bool warn = false;
  const union value *gv = case_data (c, l->v_indep);
  struct group_statistics key;
  double weight = dict_get_case_weight (dict, c, &warn);

  key.id = *gv;

  for (i = 0; i < l->n_dep; ++i)
    {
      const struct variable *var = l->v_dep[i];
      struct group_proc *gp = group_proc_get (var);
      double levene_z;
      const union value *v = case_data (c, var);
      struct group_statistics *gs;

      gs = hsh_find(gp->group_hash,(void *) &key );

      if ( 0 == gs )
	continue ;

      if ( !var_is_value_missing (var, v, l->exclude))
	{
	  levene_z= fabs(v->f - gs->mean);
	  l->lz[i].grand_total += levene_z * weight;
	  l->lz[i].total_n += weight;

	  gs->lz_total += levene_z * weight;
	}
    }
  return 0;
}


static void
levene_postcalc (struct levene_info *l)
{
  size_t v;

  for (v = 0; v < l->n_dep; ++v)
    {
      /* This is Z_LL */
      l->lz[v].grand_mean = l->lz[v].grand_total / l->lz[v].total_n ;
    }


}



static void
levene2_precalc (struct levene_info *l)
{
  size_t v;


  /* This stuff could go in the first post calc . . . */
  for (v = 0;
       v < l->n_dep;
       ++v)
    {
      struct hsh_iterator hi;
      struct group_statistics *g;

      const struct variable *var = l->v_dep[v] ;
      struct hsh_table *hash = group_proc_get (var)->group_hash;


      for(g = (struct group_statistics *) hsh_first(hash,&hi);
	  g != 0 ;
	  g = (struct group_statistics *) hsh_next(hash,&hi) )
	{
	  g->lz_mean = g->lz_total / g->n ;
	}
      l->lz_denominator[v] = 0;
  }
}

static int
levene2_calc (const struct dictionary *dict, const struct ccase *c,
	      struct levene_info *l)
{
  size_t i;
  bool warn = false;

  double weight = dict_get_case_weight (dict, c, &warn);

  const union value *gv = case_data (c, l->v_indep);
  struct group_statistics key;

  key.id = *gv;

  for (i = 0; i < l->n_dep; ++i)
    {
      double levene_z;
      const struct variable *var = l->v_dep[i] ;
      const union value *v = case_data (c, var);
      struct group_statistics *gs;

      gs = hsh_find(group_proc_get (var)->group_hash,(void *) &key );

      if ( 0 == gs )
	continue;

      if ( !var_is_value_missing (var, v, l->exclude))
	{
	  levene_z = fabs(v->f - gs->mean);
	  l->lz_denominator[i] += weight * pow2 (levene_z - gs->lz_mean);
	}
    }

  return 0;
}


static void
levene2_postcalc (struct levene_info *l)
{
  size_t v;

  for (v = 0; v < l->n_dep; ++v)
    {
      double lz_numerator = 0;
      struct hsh_iterator hi;
      struct group_statistics *g;

      const struct variable *var = l->v_dep[v] ;
      struct group_proc *gp = group_proc_get (var);
      struct hsh_table *hash = gp->group_hash;

      for(g = (struct group_statistics *) hsh_first(hash,&hi);
	  g != 0 ;
	  g = (struct group_statistics *) hsh_next(hash,&hi) )
	{
	  lz_numerator += g->n * pow2(g->lz_mean - l->lz[v].grand_mean );
	}
      lz_numerator *= ( gp->ugs.n - gp->n_groups );

      l->lz_denominator[v] *= (gp->n_groups - 1);

      gp->levene = lz_numerator / l->lz_denominator[v] ;

    }
}

