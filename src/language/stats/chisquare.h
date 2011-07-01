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

#if !chisquare_h
#define chisquare_h 1

#include <stddef.h>
#include <stdbool.h>
#include "language/stats/npar.h"

struct chisquare_test
{
  struct one_sample_test parent;

  bool ranged ;     /* True if this test has a range specified */

  int lo;           /* Lower bound of range (undefined if RANGED is false) */
  int hi;           /* Upper bound of range (undefined if RANGED is false) */

  double *expected;
  int n_expected;
};

struct casereader;
struct dataset;


void chisquare_execute (const struct dataset *ds,
			struct casereader *input,
                        enum mv_class exclude,
			const struct npar_test *test,
			bool,
			double);



#endif
