/* PSPP - A program for statistical analysis . -*-c-*-

Copyright (C) 2004 Free Software Foundation, Inc.
Author: John Darrington 2004

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

#include "factor_stats.h"
#include "config.h"
#include "val.h"
#include "hash.h"
#include "algorithm.h"
#include "alloc.h"

#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <assert.h>



void
metrics_precalc(struct metrics *fs)
{
  assert (fs) ;

  fs->n = 0;
  fs->n_missing = 0;
  fs->ssq = 0;
  fs->sum = 0;
  fs->min = DBL_MAX;
  fs->max = -DBL_MAX;

  fs->ordered_data = hsh_create(20,
				(hsh_compare_func *) compare_values,
				(hsh_hash_func *) hash_value,
				(hsh_free_func *) weighted_value_free,
				(void *) 0);

}


/* Include val in the calculation for the metrics.
   If val is null, then treat it as MISSING
*/
void
metrics_calc(struct metrics *fs, const union value *val, 
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
  fs->n    += weight;
  fs->ssq  += x * x * weight;
  fs->sum  += x * weight;

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

      cn = xmalloc( sizeof (struct case_node) ) ;
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
      
      cn = xmalloc( sizeof (struct case_node) ) ;
      cn->next=0;
      cn->num = case_no;
      (*wv)->case_nos  = cn;

    }

}

void
metrics_postcalc(struct metrics *m)
{
  double sample_var; 
  double cc = 0.0;
  double tc ;
  int k1, k2 ;
  int i;
  int j = 1;  

  struct weighted_value **data;


  int n_data;
  
  m->mean = m->sum / m->n;

  sample_var = ( m->ssq / m->n  - m->mean * m->mean );

  m->var  = m->n * sample_var / ( m->n - 1) ;
  m->stddev = sqrt(m->var);


  /* FIXME: Check this is correct ???
     Shouldn't we use the sample variance ??? */
  m->stderr = sqrt (m->var / m->n) ;

  data = (struct weighted_value **) hsh_data(m->ordered_data);
  n_data = hsh_count(m->ordered_data);

  if ( n_data == 0 ) 
    {
      m->trimmed_mean = m->mean;
      return;
    }


  m->wv = xmalloc(sizeof(struct weighted_value ) * n_data);

  for ( i = 0 ; i < n_data ; ++i )
      m->wv[i] = *(data[i]);

  sort (m->wv, n_data, sizeof (struct weighted_value) , 
	(algo_compare_func *) compare_values, 0);

  
  /* Trimmed mean calculation */

  tc = m->n * 0.05 ;
  k1 = -1;
  k2 = -1;


  for ( i = 0 ; i < n_data ; ++i ) 
    {
      cc += m->wv[i].w;
      m->wv[i].cc = cc;

      m->wv[i].rank = j + (m->wv[i].w - 1) / 2.0 ;
      
      j += m->wv[i].w;
      
      if ( cc < tc ) 
	k1 = i;

    }

  k2 = n_data;
  for ( i = n_data -1  ; i >= 0; --i ) 
    {
      if ( tc > m->n - m->wv[i].cc) 
	k2 = i;
    }


  m->trimmed_mean = 0;
  for ( i = k1 + 2 ; i <= k2 - 1 ; ++i ) 
    {
      m->trimmed_mean += m->wv[i].v.f * m->wv[i].w;
    }


  m->trimmed_mean += (m->n - m->wv[k2 - 1].cc - tc) * m->wv[k2].v.f ;
  m->trimmed_mean += (m->wv[k1 + 1].cc - tc) * m->wv[k1 + 1].v.f ;
  m->trimmed_mean /= 0.9 * m->n ;

}


struct weighted_value *
weighted_value_create(void)
{
  struct weighted_value *wv;
  wv = xmalloc (sizeof (struct weighted_value ));

  wv->cc = 0;
  wv->case_nos = 0;

  return wv;
}

void 
weighted_value_free(struct weighted_value *wv)
{
  struct case_node *cn = wv->case_nos;

  while(cn)
    {
      struct case_node *next = cn->next;
      
      free(cn);
      cn = next;
    }

  free(wv);

}





/* Create a factor statistics object with for N dependent vars
   and ID as the value of the independent variable */
struct factor_statistics * 
create_factor_statistics (int n, union value *id0, union value *id1)
{
  struct factor_statistics *f;

  f =  xmalloc( sizeof  ( struct factor_statistics ));

  f->id[0] = *id0;
  f->id[1] = *id1;
  f->m = xmalloc( sizeof ( struct metrics ) * n ) ;

  return f;
}


void
factor_statistics_free(struct factor_statistics *f)
{
  free(f->m) ; 

  free(f);
}






int 
factor_statistics_compare(const struct factor_statistics *f0,
	                  const struct factor_statistics *f1, void *aux)
{

  int cmp0;

  assert(f0);
  assert(f1);

  cmp0 = compare_values(&f0->id[0], &f1->id[0], aux);

  if ( cmp0 != 0 ) 
    return cmp0;


  if ( ( f0->id[1].f == SYSMIS )  && (f1->id[1].f != SYSMIS) ) 
    return 1;

  if ( ( f0->id[1].f != SYSMIS )  && (f1->id[1].f == SYSMIS) ) 
    return -1;

  return compare_values(&f0->id[1], &f1->id[1], aux);
  
}

unsigned int 
factor_statistics_hash(const struct factor_statistics *f, void *aux)
{
  
  unsigned int h;

  h = hash_value(&f->id[0], aux);
  
  if ( f->id[1].f != SYSMIS )
    h += hash_value(&f->id[1], aux);


  return h;

}
	
