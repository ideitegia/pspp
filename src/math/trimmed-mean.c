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

#include "math/trimmed-mean.h"

#include <math.h>

#include "data/val-type.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "math/order-stats.h"

#include "gl/xalloc.h"

static void
acc (struct statistic *s, const struct ccase *cx UNUSED, double c, double cc, double y)
{
  struct trimmed_mean *tm = UP_CAST (s, struct trimmed_mean, parent.parent);
  struct order_stats *os = &tm->parent;

  if ( cc > os->k[0].tc && cc <= os->k[1].tc)
    tm->sum += c * y;

  if ( tm->cyk1p1 == SYSMIS && cc > os->k[0].tc)
    tm->cyk1p1 = c * y;
}

static void
destroy (struct statistic *s)
{
  struct trimmed_mean *tm = UP_CAST (s, struct trimmed_mean, parent.parent);
  struct order_stats *os = &tm->parent;
  free (os->k);
  free (tm);
}

struct trimmed_mean *
trimmed_mean_create (double W, double tail)
{
  struct trimmed_mean *tm = xzalloc (sizeof (*tm));
  struct order_stats *os = &tm->parent;
  struct statistic *stat = &os->parent;

  os->n_k = 2;
  os->k = xcalloc (2, sizeof (*os->k));

  assert (tail >= 0);
  assert (tail <= 1);

  os->k[0].tc = tail * W;
  os->k[1].tc = W * (1 - tail);

  stat->accumulate = acc;
  stat->destroy = destroy;

  tm->cyk1p1 = SYSMIS;
  tm->w = W;
  tm->tail = tail;

  return tm;
}


double
trimmed_mean_calculate (const struct trimmed_mean *tm)
{
  const struct order_stats *os = (const struct order_stats *) tm;

  return
    (
     (os->k[0].cc - os->k[0].tc) * os->k[0].y_p1
     +
      (tm->w - os->k[1].cc - os->k[0].tc) * os->k[1].y_p1
     +
      tm->sum
     )
    / ((1.0 - tm->tail * 2) * tm->w);
}
