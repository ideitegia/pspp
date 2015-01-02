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

#include "box-whisker.h"

#include <math.h>
#include <float.h>

#include "data/case.h"
#include "data/data-out.h"
#include "data/val-type.h"
#include "data/variable.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/str.h"
#include "math/order-stats.h"
#include "math/tukey-hinges.h"

#include "gl/xalloc.h"

static void
destroy (struct statistic *s)
{
  struct box_whisker *bw = UP_CAST (s, struct box_whisker, parent.parent);
  struct order_stats *os = &bw->parent;
  struct ll *ll;

  for (ll = ll_head (&bw->outliers); ll != ll_null (&bw->outliers); )
    {
      struct outlier *e = ll_data (ll, struct outlier, ll);

      ll = ll_next (ll);

      ds_destroy (&e->label);
      free (e);
    }

  free (os->k);
  free (s);
};


static void
acc (struct statistic *s, const struct ccase *cx,
     double c UNUSED, double cc UNUSED, double y)
{
  struct box_whisker *bw = UP_CAST (s, struct box_whisker, parent.parent);
  bool extreme;
  struct outlier *o;

  if ( y > bw->hinges[2] + bw->step) /* Upper outlier */
    {
      extreme = (y > bw->hinges[2] + 2 * bw->step) ;
    }

  else if (y < bw->hinges[0] - bw->step) /* Lower outlier */
    {
      extreme = (y < bw->hinges[0] - 2 * bw->step) ;
    }

  else /* Not an outlier */
    {
      if (bw->whiskers[0] == SYSMIS)
	bw->whiskers[0] = y;

      if (y > bw->whiskers[1])
	bw->whiskers[1] = y;
	  
      return;
    }

  /* y is an outlier */

  o = xzalloc (sizeof *o) ;
  o->value = y;
  o->extreme = extreme;
  ds_init_empty (&o->label);

  if (bw->id_var)
    {
      char *s = data_out (case_data_idx (cx, bw->id_idx),
                           var_get_encoding (bw->id_var),
                           var_get_print_format (bw->id_var));

      ds_put_cstr (&o->label, s);
      free (s);
    }
  else
    {
      ds_put_format (&o->label,
                     "%ld",
                     (casenumber) case_data_idx (cx, bw->id_idx)->f);
    }

  ll_push_head (&bw->outliers, &o->ll);
}

void
box_whisker_whiskers (const struct box_whisker *bw, double whiskers[2])
{
  whiskers[0] = bw->whiskers[0];
  whiskers[1] = bw->whiskers[1];
}

void
box_whisker_hinges (const struct box_whisker *bw, double hinges[3])
{
  hinges[0] = bw->hinges[0];
  hinges[1] = bw->hinges[1];
  hinges[2] = bw->hinges[2];
}

const struct ll_list *
box_whisker_outliers (const struct box_whisker *bw)
{
  return &bw->outliers;
}

/*
  Create a box_whisker struct, suitable for generating a boxplot.

  TH are the tukey hinges of the dataset.

  id_idx is the index into the casereader which will be used to label 
  outliers.
  id_var is the variable from which that label came, or NULL
*/
struct box_whisker *
box_whisker_create (const struct tukey_hinges *th,
		    size_t id_idx, const struct variable *id_var)
{
  struct box_whisker *w = xzalloc (sizeof (*w));
  struct order_stats *os = &w->parent;
  struct statistic *stat = &os->parent;

  os->n_k = 0;

  stat->destroy = destroy;
  stat->accumulate = acc;

  tukey_hinges_calculate (th, w->hinges);

  w->id_idx = id_idx;
  w->id_var = id_var;

  w->step = (w->hinges[2] - w->hinges[0]) * 1.5;

  w->whiskers[1] = w->hinges[2];
  w->whiskers[0] = SYSMIS;

  ll_init (&w->outliers);

  return w;
}
