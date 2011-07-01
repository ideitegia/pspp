/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2011 Free Software Foundation, Inc.

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

#if !n_par_summary_h
#define n_par_summary_h 1

#include "data/missing-values.h"

struct variable ;
struct casereader ;
struct dictionary;

struct descriptives
{
  double n;
  double mean;
  double std_dev;
  double min;
  double max;
};

void
do_summary_box (const struct descriptives *desc,
		const struct variable *const *vv,
		int n_vars);

void npar_summary_calc_descriptives (struct descriptives *desc,
				     struct casereader *input,
				     const struct dictionary *dict,
				     const struct variable *const *vv,
				     int n_vars,
                                     enum mv_class filter);

#endif
