/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2008, 2011 Free Software Foundation, Inc.

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

#ifndef __ORDER_STATS_H__
#define __ORDER_STATS_H__

#include <stddef.h>
#include "data/missing-values.h"
#include "math/statistic.h"

struct casereader;
struct variable;

/*
  cc <= tc < cc_p1
*/
struct k
{
  double tc;
  double cc;
  double cc_p1;
  double c;
  double c_p1;
  double y;
  double y_p1;
};


struct order_stats
{
  struct statistic parent;
  int n_k;
  struct k *k;

  double cc;
};

enum mv_class;

void order_stats_dump (const struct order_stats *os);

void
order_stats_accumulate_idx (struct order_stats **os, size_t nos,
                            struct casereader *reader,
                            int wt_idx,
                            int val_idx);


void order_stats_accumulate (struct order_stats **ptl, size_t nos,
			     struct casereader *reader,
			     const struct variable *wv,
			     const struct variable *var,
			     enum mv_class exclude);

#endif
