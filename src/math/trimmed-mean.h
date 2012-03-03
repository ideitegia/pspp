/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2009 Free Software Foundation, Inc.

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

#ifndef __TRIMMED_MEAN_H__
#define __TRIMMED_MEAN_H__

#include "order-stats.h"


struct trimmed_mean
{
  struct order_stats parent;

  /* The partial sum */
  double sum;

  /* The product of c_{k_1+1} and y_{k_1 + 1} */
  double cyk1p1;

  double w;
  double tail;
};

struct trimmed_mean * trimmed_mean_create (double W, double c_min);
double trimmed_mean_calculate (const struct trimmed_mean *);

#endif
