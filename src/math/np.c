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

#include "math/np.h"

#include <gsl/gsl_cdf.h>
#include <math.h>
#include <stdlib.h>

#include "data/case.h"
#include "data/casewriter.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/misc.h"
#include "math/moments.h"

#include "gl/xalloc.h"

static void
destroy (struct statistic *stat)
{
  struct np *np = UP_CAST (stat, struct np, parent.parent);
  free (np);
}


static void
acc (struct statistic *s, const struct ccase *cx UNUSED,
     double c, double cc, double y)
{
  struct ccase *cp;
  struct np *np = UP_CAST (s, struct np, parent.parent);
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

  cp = case_create (casewriter_get_proto (np->writer));
  case_data_rw_idx (cp, NP_IDX_Y)->f = y;
  case_data_rw_idx (cp, NP_IDX_NS)->f = ns;
  case_data_rw_idx (cp, NP_IDX_DNS)->f = dns;
  casewriter_write (np->writer, cp);

  np->prev_cc = cc;
}

struct np *
np_create (double n, double mean, double var)
{
  struct np *np = xzalloc (sizeof (*np));
  struct order_stats *os = &np->parent;
  struct statistic *stat = &os->parent;
  struct caseproto *proto;
  int i;

  np->prev_cc = 0;

  np->n = n;
  np->mean = mean;

  np->stddev = sqrt (var);

  np->y_min = np->ns_min = np->dns_min = DBL_MAX;
  np->y_max = np->ns_max = np->dns_max = -DBL_MAX;

  proto = caseproto_create ();
  for (i = 0; i < n_NP_IDX; i++)
    proto = caseproto_add_width (proto, 0);
  np->writer = autopaging_writer_create (proto);
  caseproto_unref (proto);

  os->k = 0;
  stat->destroy = destroy;
  stat->accumulate = acc;

  return np;
}
