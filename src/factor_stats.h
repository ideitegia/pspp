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

#ifndef FACTOR_STATS
#define FACTOR_STATS


/* FIXME: These things should probably be amalgamated with the 
   group_statistics struct */


struct metrics
{
  double n;
  
  double ssq;
  
  double sum;

  double min;

  double max;

  double mean;
  
  double stderr;

  double var;

  double stddev;
};



struct factor_statistics {

  /* The value of the independent variable for this factor */
  const union value *id;

  /* An array of metrics indexed by dependent variable */
  struct metrics *stats;

};



void metrics_precalc(struct metrics *fs);

void metrics_calc(struct metrics *fs, double x, double weight);

void metrics_postcalc(struct metrics *fs);




/* These functions are necessary for creating hashes */

int compare_indep_values(const struct factor_statistics *f1, 
		     const struct factor_statistics *f2, 
		     int width);

unsigned hash_indep_value(const struct factor_statistics *f, int width) ;

void  free_factor_stats(struct factor_statistics *f, int width );


#endif
