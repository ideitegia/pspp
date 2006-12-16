/* PSPP - computes sample statistics.
   Copyright (C) 2004 Free Software Foundation, Inc.

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


#ifndef GROUP_H
#define GROUP_H


#include <data/value.h>


/* Statistics for grouped data */
struct group_statistics
  {
    /* The value of the independent variable for this group */
    union value id;

    /* The arithmetic mean */
    double mean;

    /* Population std. deviation */
    double std_dev;

    /* Sample std. deviation */
    double s_std_dev;
    
    /* count */
    double n;

    double sum;

    /* Sum of squares */
    double ssq;

    /* Std Err of Mean */
    double se_mean;

    /* Sum of differences */
    double sum_diff;

    /* Mean of differences */
    double mean_diff ;

    /* Running total of the Levene for this group */
    double lz_total;
    
    /* Group mean of Levene */
    double lz_mean; 


    /* min and max values */
    double minimum ; 
    double maximum ;


  };




/* These funcs are useful for hash tables */

/* Return -1 if the id of a is less than b; +1 if greater than and 
   0 if equal */
int  compare_group(const struct group_statistics *a, 
		   const struct group_statistics *b, 
		   int width);

unsigned hash_group(const struct group_statistics *g, int width);

void  free_group(struct group_statistics *v, void *aux);



#endif
