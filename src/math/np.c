/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2009 Free Software Foundation, Inc.

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
#include "np.h"
#include <math/moments.h>
#include <gl/xalloc.h>
#include <stdlib.h>
#include <math.h>
#include <gsl/gsl_cdf.h>
#include <libpspp/compiler.h>
#include <data/case.h>
#include <data/casewriter.h>

static void
destroy (struct statistic *stat)
{
  struct order_stats *os = (struct order_stats *) stat;
  free (os);
}


static void
acc (struct statistic *s, const struct ccase *cx UNUSED,
     double c, double cc, double y)
{
  struct ccase *cp;
  struct np *np = (struct np *) s;
  double rank = np->prev_cc + (c + 1) / 2.0;

  double ns = gsl_cdf_ugaussian_Pinv (rank / ( np->n + 1 ));

  double z = (y - np->mean) / np->stddev;

  double dns = z - ns;

  maximize (&np->ns_max, ns);
  minimize (&np->ns_min, ns);

  maximize (&np->dns_max, dns);
  minimize (&np->dns_min, dns);

  maximize (&np->y_max, y);
  minimize (&np->y_min, y);

  cp = case_create (n_NP_IDX);
  case_data_rw_idx (cp, NP_IDX_Y)->f = y;
  case_data_rw_idx (cp, NP_IDX_NS)->f = ns;
  case_data_rw_idx (cp, NP_IDX_DNS)->f = dns;
  casewriter_write (np->writer, cp);

  np->prev_cc = cc;
}

struct order_stats *
np_create (const struct moments1 *m)
{
  double variance;
  struct np *np = xzalloc (sizeof (*np));
  struct statistic *stat = (struct statistic *) np;
  struct order_stats *os = (struct order_stats *) np;

  np->prev_cc = 0;

  moments1_calculate (m, &np->n, &np->mean, &variance, NULL, NULL);

  np->stddev = sqrt (variance);

  np->y_min = np->ns_min = np->dns_min = DBL_MAX;
  np->y_max = np->ns_max = np->dns_max = -DBL_MAX;

  np->writer = autopaging_writer_create (n_NP_IDX);

  os->k = 0;
  stat->destroy = destroy;
  stat->accumulate = acc;

  return os;
}
