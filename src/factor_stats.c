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
  fs->n = 0;
  fs->ssq = 0;
  fs->sum = 0;
  fs->min = DBL_MAX;
  fs->max = -DBL_MAX;

  fs->ordered_data = hsh_create(20,
				(hsh_compare_func *) compare_values,
				(hsh_hash_func *) hash_value,
				0,
				(void *) 0);
}

void
metrics_calc(struct metrics *fs, const union value *val, double weight)
{


  struct weighted_value **wv;
  const double x = val->f;
  
  fs->n    += weight;
  fs->ssq  += x * x * weight;
  fs->sum  += x * weight;

  if ( x < fs->min) fs->min = x;
  if ( x > fs->max) fs->max = x;


  wv = (struct weighted_value **) hsh_probe (fs->ordered_data,(void *) val );

  if ( *wv  ) 
    {
      /* If this value has already been seen, then simply 
	 increase its weight */

      assert( (*wv)->v.f == val->f );
      (*wv)->w += weight;      
    }
  else
    {
      *wv = xmalloc( sizeof (struct weighted_value) );
      (*wv)->v = *val;
      (*wv)->w = weight;
      hsh_insert(fs->ordered_data,(void *) *wv);
    }

}

void
metrics_postcalc(struct metrics *fs)
{
  double sample_var; 
  double cc = 0.0;
  double tc ;
  int k1, k2 ;
  int i;
  int j = 1;  

  struct weighted_value **data;


  int n_data;
  
  fs->mean = fs->sum / fs->n;

  sample_var = ( fs->ssq / fs->n  - fs->mean * fs->mean );

  fs->var  = fs->n * sample_var / ( fs->n - 1) ;
  fs->stddev = sqrt(fs->var);


  /* FIXME: Check this is correct ???
     Shouldn't we use the sample variance ??? */
  fs->stderr = sqrt (fs->var / fs->n) ;

  data = (struct weighted_value **) hsh_data(fs->ordered_data);
  n_data = hsh_count(fs->ordered_data);

  fs->wv = xmalloc ( sizeof (struct weighted_value) * n_data);

  for ( i = 0 ; i < n_data ; ++i )
    fs->wv[i] = *(data[i]);

  sort (fs->wv, n_data, sizeof (struct weighted_value) , 
	(algo_compare_func *) compare_values, 0);


  
  tc = fs->n * 0.05 ;
  k1 = -1;
  k2 = -1;


  for ( i = 0 ; i < n_data ; ++i ) 
    {
      cc += fs->wv[i].w;
      fs->wv[i].cc = cc;

      fs->wv[i].rank = j + (fs->wv[i].w - 1) / 2.0 ;
      
      j += fs->wv[i].w;
      
      if ( cc < tc ) 
	k1 = i;

    }

  k2 = n_data;
  for ( i = n_data -1  ; i >= 0; --i ) 
    {
      if ( tc > fs->n - fs->wv[i].cc) 
	k2 = i;
    }


  fs->trimmed_mean = 0;
  for ( i = k1 + 2 ; i <= k2 - 1 ; ++i ) 
    {
      fs->trimmed_mean += fs->wv[i].v.f * fs->wv[i].w;
    }


  fs->trimmed_mean += (fs->n - fs->wv[k2 - 1].cc - tc) * fs->wv[k2].v.f ;
  fs->trimmed_mean += (fs->wv[k1 + 1].cc - tc) * fs->wv[k1 + 1].v.f ;
  fs->trimmed_mean /= 0.9 * fs->n ;

}


/* Functions for hashes */

void 
free_factor_stats(struct factor_statistics *f, int width UNUSED)
{
  free (f);
}

int
compare_indep_values(const struct factor_statistics *f1, 
		     const struct factor_statistics *f2, 
		     int width)
{
  return compare_values(f1->id, f2->id, width);
}


unsigned 
hash_indep_value(const struct factor_statistics *f, int width)
{
  return hash_value(f->id, width);
}
