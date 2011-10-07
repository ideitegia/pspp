/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2009, 2011 Free Software Foundation, Inc.

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

#include "math/tukey-hinges.h"

#include <math.h>

#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "math/order-stats.h"

#include "gl/xalloc.h"

void
tukey_hinges_calculate (const struct tukey_hinges *th, double hinge[3])
{
  double a[3];
  double a_star[3];
  int i;
  const struct order_stats *os = &th->parent;

  for (i = 0 ; i < 3 ; ++i)
    {
      a_star[i] = os->k[i].tc - os->k[i].cc;
      a[i] = a_star[i] / os->k[i].c_p1;

      if (a_star[i] < 1)
	{
	  if (os->k[i].c_p1 >= 1 )
	    {
	      hinge[i] = (1 - a_star[i]) * os->k[i].y
		+ a_star[i] * os->k[i].y_p1;
	    }
	  else
	    {
	      hinge[i] = (1 - a[i]) * os->k[i].y
		+ a[i] * os->k[i].y_p1;
	    }
	}
      else
	{
	  hinge[i] = os->k[i].y_p1;
	}

    }
}

static void
destroy (struct statistic *s)
{
  struct tukey_hinges *th = UP_CAST (s, struct tukey_hinges, parent.parent);
  struct order_stats *os = &th->parent;

  free (os->k);
  free (s);
};

struct tukey_hinges *
tukey_hinges_create (double W, double c_min)
{
  double d;
  struct tukey_hinges *th = xzalloc (sizeof (*th));
  struct order_stats *os = &th->parent;
  struct statistic *stat = &os->parent;

  assert (c_min >= 0);

  os->n_k = 3;
  os->k = xcalloc (3, sizeof (*os->k));

  if ( c_min >= 1.0)
    {
      d = floor ((W + 3) / 2.0) / 2.0;

      os->k[0].tc = d;
      os->k[1].tc = W/2.0 + 0.5;
      os->k[2].tc = W + 1 - d;
    }
  else
    {
      d = floor ((W/c_min + 3.0)/ 2.0) / 2.0 ;
      os->k[0].tc = d * c_min;
      os->k[1].tc = (W + c_min) / 2.0;
      os->k[2].tc = W + c_min * (1 - d);
    }


  stat->destroy = destroy;

  return th;
}
