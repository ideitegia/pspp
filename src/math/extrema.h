/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2011 Free Software Foundation, Inc.

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

#ifndef __EXTREMA_H__
#define __EXTREMA_H__ 1

#include <stddef.h>
#include "data/case.h"
#include "libpspp/ll.h"

struct extremum
{
  double value;
  casenumber location;
  double weight;

  /* Internal use only */
  struct ll ll;
};


enum extreme_end
  {
    EXTREME_MAXIMA,
    EXTREME_MINIMA
  };

struct extrema;

struct extrema *extrema_create (size_t n, enum extreme_end);

void extrema_destroy (struct extrema *extrema);

void extrema_add (struct extrema *extrema, double val,
		  double weight,
		  casenumber location);

void extrema_show (const struct extrema *extrema);

const struct ll_list * extrema_list (const struct extrema *);

bool extrema_top (const struct extrema *, double *);


#endif
