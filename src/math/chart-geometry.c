/* PSPP - a program for statistical analysis.
   Copyright (C) 2004 Free Software Foundation, Inc.

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

#include <config.h>
#include <math.h>
#include <float.h>

#include "chart-geometry.h"

/* Adjust tick to be a sensible value
   ie:  ... 0.1,0.2,0.5,   1,2,5,  10,20,50 ... */
double
chart_rounded_tick(double tick)
{
  int i;

  double diff = DBL_MAX;
  double t = tick;

  static const double standard_ticks[] = {1, 2, 5, 10};

  double factor;

  /* Avoid arithmetic problems with very small values */
  if (abs (tick) < DBL_EPSILON)
     return 0;

  factor = pow(10,ceil(log10(standard_ticks[0] / tick))) ;

  for (i = 3  ; i >= 0 ; --i)
    {
      const double d = fabs( tick - standard_ticks[i] / factor ) ;

      if ( d < diff )
	{
	  diff = d;
	  t = standard_ticks[i] / factor ;
	}
    }

  return t;

}

