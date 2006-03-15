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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA. */

#ifndef PERCENTILES_H
#define PERCENTILES_H


#include <libpspp/hash.h>

struct weighted_value ;

/* The algorithm used to calculate percentiles */
enum pc_alg {
  PC_NONE=0, 
  PC_HAVERAGE, 
  PC_WAVERAGE, 
  PC_ROUND, 
  PC_EMPIRICAL, 
  PC_AEMPIRICAL
} ;



extern  const char *ptile_alg_desc[];




struct percentile {

  /* The break point of the percentile */
  double p;

  /* The value of the percentile */
  double v;
};


/* Calculate the percentiles of the break points in pc_bp,
   placing the values in pc_val.
   wv is  a sorted array of weighted values of the data set.
*/
void ptiles(struct hsh_table *pc_hash,
	    const struct weighted_value **wv,
	    int n_data,
	    double w,
	    enum pc_alg algorithm);


/* Calculate Tukey's Hinges and the Whiskers for the box plot*/
void tukey_hinges(const struct weighted_value **wv,
		  int n_data, 
		  double w,
		  double hinges[3]);



/* Hash utility functions */
int ptile_compare(const struct percentile *p1, 
		   const struct percentile *p2, 
		   void *aux);

unsigned ptile_hash(const struct percentile *p, void *aux);


#endif
