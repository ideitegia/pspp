/* PSPP - a program for statistical analysis.
   Copyright (C) 2011 Free Software Foundation, Inc.

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

#if !ks_one_sample_h
#define ks_one_sample_h 1

#include <stddef.h>
#include <stdbool.h>
#include "language/stats/npar.h"

enum dist
  {
    KS_NORMAL,
    KS_UNIFORM,
    KS_POISSON,
    KS_EXPONENTIAL
  };

struct ks_one_sample_test
{
  struct one_sample_test parent;

  double p[2];
  enum dist dist;
};

struct casereader;
struct dataset;


void ks_one_sample_execute (const struct dataset *ds,
			    struct casereader *input,
			    enum mv_class exclude,
			    const struct npar_test *test,
			    bool, double);

#endif
