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

#ifndef T_TEST_H
#define T_TEST_H 1

#include "data/missing-values.h"

struct variable;
typedef const struct variable *vp[2];

enum missing_type
  {
    MISS_LISTWISE,
    MISS_ANALYSIS,
  };

enum mode
  {
    MODE_undef,
    MODE_PAIRED,
    MODE_INDEP,
    MODE_SINGLE,
  };

struct tt
{
  size_t n_vars;
  const struct variable **vars;
  enum mode mode;
  enum missing_type missing_type;
  enum mv_class exclude;
  double confidence;
  const struct variable *wv;
  const struct dictionary *dict;
};

struct casereader;
union value;

void one_sample_run (const struct tt *tt, double testval, struct casereader *reader);
void paired_run (const struct tt *tt, size_t n_pairs, vp *pairs, struct casereader *reader);
void indep_run (struct tt *tt, const struct variable *gvar,
		bool cut,
		const union value *gval0, const union value *gval1,
		struct casereader *reader);


#endif
