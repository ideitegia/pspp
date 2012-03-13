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

#ifndef __NP_H__
#define __NP_H__

#include "order-stats.h"

struct casewriter;

enum
  {
    NP_IDX_Y = 0,
    NP_IDX_NS,
    NP_IDX_DNS,
    n_NP_IDX
  };

struct np
{
  struct order_stats parent;

  double n;
  double mean;
  double stddev;

  double prev_cc;

  double ns_min;
  double ns_max;

  double dns_min;
  double dns_max;

  double y_min;
  double y_max;

  struct casewriter *writer;
};


struct np * np_create (double n, double mean, double var);

#endif
