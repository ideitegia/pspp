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

#if !runs_h
#define runs_h 1

#include <stddef.h>
#include <stdbool.h>
#include "language/stats/npar.h"

enum cp_mode
  {
    CP_MEAN,
    CP_MEDIAN,
    CP_MODE,
    CP_CUSTOM
  };


struct runs_test
{
  struct one_sample_test parent;

  double cutpoint;

  enum cp_mode cp_mode;
};


void runs_execute (const struct dataset *ds,
			struct casereader *input,
                        enum mv_class exclude,
			const struct npar_test *test,
			bool,
		   double);


#endif
