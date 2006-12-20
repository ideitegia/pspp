/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.
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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#if !n_par_summary_h
#define n_par_summary_h 1

#include <config.h>

struct variable ;
struct casefile ;
struct dictionary;
struct casefilter;

struct descriptives
{
  double n;
  double mean;
  double std_dev;
  double min;
  double max;
};

void npar_summary_calc_descriptives (struct descriptives *desc,
				     const struct casefile *cf,
				     struct casefilter *filter,
				     const struct dictionary *dict,
				     const struct variable *const *vv, 
				     int n_vars);


void do_summary_box (const struct descriptives *desc, 
		     const struct variable *const *vv,
		     int n_vars);



#endif
