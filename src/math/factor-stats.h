/* PSPP - A program for statistical analysis . -*-c-*-

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

#ifndef FACTOR_STATS
#define FACTOR_STATS


/* FIXME: These things should probably be amalgamated with the
   group_statistics struct */

#include <libpspp/hash.h>
#include <data/value.h>
#include <string.h>
#include <gsl/gsl_histogram.h>
#include "percentiles.h"


struct moments1;

struct metrics
{
  double n;

  double n_missing;

  double min;

  double max;

  double mean;

  double se_mean;

  double var;

  double stddev;

  struct moments1 *moments;

  gsl_histogram *histogram;

  double skewness;
  double kurtosis;

  double trimmed_mean;

  /* A hash of data for this factor. */
  struct hsh_table *ordered_data;

  /* A Pointer to this hash table AFTER it has been SORTED and crunched */
  struct weighted_value **wvp;

  /* The number of values in the above array
     (if all the weights are 1, then this will
     be the same as n) */
  int n_data;

  /* Percentile stuff */

  /* A hash of struct percentiles */
  struct hsh_table *ptile_hash;

  /* Algorithm to be used for calculating percentiles */
  enum pc_alg ptile_alg;

  /* Tukey's Hinges */
  double hinge[3];

};


struct metrics * metrics_create(void);

void metrics_precalc(struct metrics *m);

void metrics_calc(struct metrics *m, const union value *f, double weight,
		  int case_no);

void metrics_postcalc(struct metrics *m);

void  metrics_destroy(struct metrics *m);



/* Linked list of case nos */
struct case_node
{
  int num;
  struct case_node *next;
};

struct weighted_value
{
  union value v;

  /* The weight */
  double w;

  /* The cumulative weight */
  double cc;

  /* The rank */
  double rank;

  /* Linked list of cases nos which have this value */
  struct case_node *case_nos;

};


struct weighted_value *weighted_value_create(void);

void weighted_value_free(struct weighted_value *wv);



struct factor_statistics {

  /* The values of the independent variables */
  union value *id[2];

  /* The an array stats for this factor, one for each dependent var */
  struct metrics *m;

  /* The number of dependent variables */
  int n_var;
};


/* Create a factor statistics object with for N dependent vars
   and ID as the value of the independent variable */
struct factor_statistics * create_factor_statistics (int n,
			  union value *id0,
			  union value *id1);


void factor_statistics_free(struct factor_statistics *f);


/* Compare f0 and f1.
   width is the width of the independent variable */
int
factor_statistics_compare(const struct factor_statistics *f0,
	                  const struct factor_statistics *f1, int width);

unsigned int
factor_statistics_hash(const struct factor_statistics *f, int width);

#endif
