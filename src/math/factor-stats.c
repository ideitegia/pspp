/* PSPP - a program for statistical analysis.
   Copyright (C) 2004 Free Software Foundation, Inc.

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
#include "factor-stats.h"
#include <data/val-type.h>
#include <data/value.h>
#include <libpspp/hash.h>
#include <libpspp/array.h>
#include "moments.h"
#include "percentiles.h"

#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <assert.h>
#include "histogram.h"

#include "xalloc.h"

void
metrics_precalc(struct metrics *m)
{
  assert (m) ;

  m->n_missing = 0;

  m->min = DBL_MAX;
  m->max = -DBL_MAX;

  m->histogram = 0;

  m->moments = moments1_create(MOMENT_KURTOSIS);

  m->ordered_data = hsh_create(20,
				(hsh_compare_func *) compare_values,
				(hsh_hash_func *) hash_value,
				(hsh_free_func *) weighted_value_free,
				(void *) 0);
}


/* Include val in the calculation for the metrics.
   If val is null, then treat it as MISSING
*/
void
metrics_calc (struct metrics *fs, const union value *val,
	      double weight, int case_no)
{
  struct weighted_value **wv;
  double x;

  if ( ! val )
    {
      fs->n_missing += weight;
      return ;
    }

  x = val->f;

  moments1_add(fs->moments, x, weight);

  if ( x < fs->min) fs->min = x;
  if ( x > fs->max) fs->max = x;

  wv = (struct weighted_value **) hsh_probe (fs->ordered_data,(void *) val );

  if ( *wv  )
    {
      /* If this value has already been seen, then simply
	 increase its weight  and push a new case number */

      struct case_node *cn;

      assert( (*wv)->v.f == val->f );
      (*wv)->w += weight;

      cn = xmalloc ( sizeof *cn);
      cn->next = (*wv)->case_nos ;
      cn->num = case_no;

      (*wv)->case_nos = cn;
    }
  else
    {
      struct case_node *cn;

      *wv = weighted_value_create();
      (*wv)->v = *val;
      (*wv)->w = weight;

      cn = xmalloc (sizeof *cn);
      cn->next=0;
      cn->num = case_no;
      (*wv)->case_nos  = cn;

    }

}

void
metrics_postcalc(struct metrics *m)
{
  double cc = 0.0;
  double tc ;
  int k1, k2 ;
  int i;
  int j = 1;

  moments1_calculate (m->moments, &m->n, &m->mean, &m->var,
		      &m->skewness, &m->kurtosis);

  moments1_destroy (m->moments);


  m->stddev = sqrt(m->var);

  /* FIXME: Check this is correct ???
     Shouldn't we use the sample variance ??? */
  m->se_mean = sqrt (m->var / m->n) ;



  m->wvp = (struct weighted_value **) hsh_sort(m->ordered_data);
  m->n_data = hsh_count(m->ordered_data);

  /* Trimmed mean calculation */
  if ( m->n_data <= 1 )
    {
      m->trimmed_mean = m->mean;
      return;
    }

  m->histogram = histogram_create(10, m->min, m->max);

  for ( i = 0 ; i < m->n_data ; ++i )
    {
      struct weighted_value **wv = (m->wvp) ;
      gsl_histogram_accumulate(m->histogram, wv[i]->v.f, wv[i]->w);
    }

  tc = m->n * 0.05 ;
  k1 = -1;
  k2 = -1;

  for ( i = 0 ; i < m->n_data ; ++i )
    {
      cc += m->wvp[i]->w;
      m->wvp[i]->cc = cc;

      m->wvp[i]->rank = j + (m->wvp[i]->w - 1) / 2.0 ;

      j += m->wvp[i]->w;

      if ( cc < tc )
	k1 = i;
    }



  k2 = m->n_data;
  for ( i = m->n_data -1  ; i >= 0; --i )
    {
      if ( tc > m->n - m->wvp[i]->cc)
	k2 = i;
    }


  /* Calculate the percentiles */
  ptiles (m->ptile_hash, (const struct weighted_value **) m->wvp,
          m->n_data, m->n, m->ptile_alg);

  tukey_hinges ((const struct weighted_value **) m->wvp,
                m->n_data, m->n, m->hinge);

  /* Special case here */
  if ( k1 + 1 == k2 )
    {
      m->trimmed_mean = m->wvp[k2]->v.f;
      return;
    }

  m->trimmed_mean = 0;
  for ( i = k1 + 2 ; i <= k2 - 1 ; ++i )
    {
      m->trimmed_mean += m->wvp[i]->v.f * m->wvp[i]->w;
    }


  m->trimmed_mean += (m->n - m->wvp[k2 - 1]->cc - tc) * m->wvp[k2]->v.f ;
  m->trimmed_mean += (m->wvp[k1 + 1]->cc - tc) * m->wvp[k1 + 1]->v.f ;
  m->trimmed_mean /= 0.9 * m->n ;


}


struct weighted_value *
weighted_value_create(void)
{
  struct weighted_value *wv;
  wv = xmalloc (sizeof *wv);

  wv->cc = 0;
  wv->case_nos = 0;

  return wv;
}

void
weighted_value_free(struct weighted_value *wv)
{
  struct case_node *cn ;

  if ( !wv )
    return ;

  cn = wv->case_nos;

  while(cn)
    {
      struct case_node *next = cn->next;

      free(cn);
      cn = next;
    }

  free(wv);

}





/* Create a factor statistics object with for N dependent vars
   and ID0 and ID1 as the values of the independent variable */
struct factor_statistics *
create_factor_statistics (int n,
			  union value *id0,
			  union value *id1)
{
  struct factor_statistics *f;

  f = xmalloc (sizeof *f);

  f->id[0] = id0;
  f->id[1] = id1;
  f->m = xnmalloc (n, sizeof *f->m);
  memset (f->m, 0, sizeof(struct metrics) * n);
  f->n_var = n;

  return f;
}

void
metrics_destroy(struct metrics *m)
{
  hsh_destroy(m->ordered_data);
  hsh_destroy(m->ptile_hash);
  if ( m-> histogram )
    gsl_histogram_free(m->histogram);
}

void
factor_statistics_free(struct factor_statistics *f)
{

  int i;
  free (f->id[0]);
  free (f->id[1]);
  for ( i = 0 ; i < f->n_var; ++i )
       metrics_destroy(&f->m[i]);
  free(f->m) ;
  free(f);
}


int
factor_statistics_compare(const struct factor_statistics *f0,
	                  const struct factor_statistics *f1, int width)
{

  int cmp0;

  assert(f0);
  assert(f1);

  cmp0 = compare_values(f0->id[0], f1->id[0], width);

  if ( cmp0 != 0 )
    return cmp0;


  if ( ( f0->id[1]->f == SYSMIS ) && (f1->id[1]->f != SYSMIS) )
    return 1;

  if ( ( f0->id[1]->f != SYSMIS )  && (f1->id[1]->f == SYSMIS) )
    return -1;

  return compare_values (f0->id[1], f1->id[1], width);
}

unsigned int
factor_statistics_hash (const struct factor_statistics *f, int width)
{
  unsigned int h;

  h = hash_value (f->id[0], width);

  if ( f->id[1]->f != SYSMIS )
    h += hash_value(f->id[1], width);

  return h;
}