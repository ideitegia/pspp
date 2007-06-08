/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.

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

#if !binomial_h
#define binomial_h 1

#include <config.h>
#include <stddef.h>
#include <stdbool.h>

#include "npar.h"


struct binomial_test
{
  struct one_sample_test parent;
  double p;
  double category1;
  double category2;
  double cutpoint;
};


struct casereader;
struct dataset;


void binomial_execute (const struct dataset *,
		       struct casereader *,
                       enum mv_class,
		       const struct npar_test *);

#endif
