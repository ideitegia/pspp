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
#include "order-stats.h"
#include <libpspp/assertion.h>
#include <data/val-type.h>
#include <gl/xalloc.h>
#include <data/variable.h>
#include <data/casereader.h>
#include <string.h>

#if 0

#include <stdio.h>

static void
order_stats_dump_k1 (const struct order_stats *os)
{
  struct k *k = &os->k[0];
  printf ("K1: tc %g; c %g cc %g ccp %g\n",
	  k->tc, k->c, k->cc, k->cc_p1);

}

static void
order_stats_dump_k2 (const struct order_stats *os)
{
  struct k *k = &os->k[1];
  printf ("K2: tc %g; c %g cc %g ccp %g\n",
	  k->tc, k->c, k->cc, k->cc_p1);
}


void
order_stats_dump (const struct order_stats *os)
{
  order_stats_dump_k1 (os);
  order_stats_dump_k2 (os);
}

#endif

static void
update_k_lower (struct k *kk,
		double y_i, double c_i, double cc_i)
{
  if ( cc_i <= kk->tc )
    {
      kk->cc = cc_i;
      kk->c = c_i;
      kk->y = y_i;
    }
}


static void
update_k_upper (struct k *kk,
		double y_i, double c_i, double cc_i)
{
  if ( cc_i > kk->tc && kk->c_p1 == 0)
    {
      kk->cc_p1 = cc_i;
      kk->c_p1 = c_i;
      kk->y_p1 = y_i;
    }
}


static void
update_k_values (const struct ccase *cx, double y_i, double c_i, double cc_i,
		 struct order_stats **os, size_t n_os)
{
  int j;

  for (j = 0 ; j < n_os ; ++j)
    {
      int k;
      struct order_stats *tos = os[j];
      struct statistic  *stat = &tos->parent;
      for (k = 0 ; k < tos->n_k; ++k)
	{
	  struct k *myk = &tos->k[k];
	  update_k_lower (myk, y_i, c_i, cc_i);
	  update_k_upper (myk, y_i, c_i, cc_i);
	}

      if ( stat->accumulate )
	stat->accumulate (stat, cx, c_i, cc_i, y_i);

      tos->cc = cc_i;
    }
}


void
order_stats_accumulate (struct order_stats **os, size_t nos,
			struct casereader *reader,
			const struct variable *wv,
			const struct variable *var,
			enum mv_class exclude)
{
  struct ccase *cx;
  struct ccase *prev_cx = NULL;
  double prev_value = -DBL_MAX;

  double cc_i = 0;
  double c_i = 0;

  for (; (cx = casereader_read (reader)) != NULL; case_unref (cx))
    {
      const double weight = wv ? case_data (cx, wv)->f : 1.0;
      const double this_value = case_data (cx, var)->f;

      /* The casereader MUST be sorted */
      assert (this_value >= prev_value);

      if ( var_is_value_missing (var, case_data (cx, var), exclude))
	continue;

      case_unref (prev_cx);

      if ( prev_value == -DBL_MAX || prev_value == this_value)
	c_i += weight;

      if ( prev_value > -DBL_MAX && this_value > prev_value)
	{
	  update_k_values (prev_cx, prev_value, c_i, cc_i, os, nos);
	  c_i = weight;
	}

      cc_i += weight;
      prev_value = this_value;
      prev_cx = case_ref (cx);
    }

  update_k_values (prev_cx, prev_value, c_i, cc_i, os, nos);
  case_unref (prev_cx);

  casereader_destroy (reader);
}


