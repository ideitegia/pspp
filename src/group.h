/* PSPP - computes sample statistics.
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



#ifndef GROUP_H
#define GROUP_H


#include "val.h"


enum comparison
  {
    CMP_LE = -2,
    CMP_LT = -1,
    CMP_EQ = 0,
    CMP_GT = 1,
    CMP_GE = 2
  };


/* Statistics for grouped data */
struct group_statistics
  {
    /* The value of the independent variable for this group */
    union value id;

    /* The criterium matching for comparing with id 
       (applicable only to T-TEST) FIXME: therefore it shouldn't be here
     */
    enum comparison criterion;

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




/* These funcs are usefull for hash tables */

/* Return -1 if the id of a is less than b; +1 if greater than and 
   0 if equal */
int  compare_group(const struct group_statistics *a, 
		   const struct group_statistics *b, 
		   int width);

unsigned hash_group(const struct group_statistics *g, int width);

void  free_group(struct group_statistics *v, void *aux);



#endif
