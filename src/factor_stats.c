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

#include <stdlib.h>
#include <math.h>
#include <float.h>

void
metrics_precalc(struct metrics *fs)
{
  fs->n = 0;
  fs->ssq = 0;
  fs->sum = 0;
  fs->min = DBL_MAX;
  fs->max = -DBL_MAX;
}

void
metrics_calc(struct metrics *fs, double x, double weight)
{
  fs->n    += weight;
  fs->ssq  += x * x * weight;
  fs->sum  += x * weight;

  if ( x < fs->min) fs->min = x;
  if ( x > fs->max) fs->max = x;

}

void
metrics_postcalc(struct metrics *fs)
{
  double sample_var; 
  fs->mean = fs->sum / fs->n;

  sample_var = ( fs->ssq / fs->n  - fs->mean * fs->mean );

  fs->var  = fs->n * sample_var / ( fs->n - 1) ;
  fs->stddev = sqrt(fs->var);


  /* FIXME: Check this is correct ???
     Shouldn't we use the sample variance ??? */
  fs->stderr = sqrt (fs->var / fs->n) ;

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
