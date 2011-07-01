/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2009, 2011 Free Software Foundation, Inc.

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

#include "output/charts/piechart.h"

#include <stdlib.h>

#include "libpspp/cast.h"
#include "libpspp/str.h"
#include "output/chart-item-provider.h"

#include "gl/xalloc.h"

/* Creates and returns a chart that will render a piechart with
   the given TITLE and the N_SLICES described in SLICES. */
struct chart_item *
piechart_create (const char *title, const struct slice *slices, int n_slices)
{
  struct piechart *pie;
  int i;

  pie = xmalloc (sizeof *pie);
  chart_item_init (&pie->chart_item, &piechart_class, title);
  pie->slices = xnmalloc (n_slices, sizeof *pie->slices);
  for (i = 0; i < n_slices; i++)
    {
      const struct slice *src = &slices[i];
      struct slice *dst = &pie->slices[i];

      ds_init_string (&dst->label, &src->label);
      dst->magnitude = src->magnitude;
    }
  pie->n_slices = n_slices;
  return &pie->chart_item;
}

static void
piechart_destroy (struct chart_item *chart_item)
{
  struct piechart *pie = to_piechart (chart_item);
  int i;

  for (i = 0; i < pie->n_slices; i++)
    {
      struct slice *slice = &pie->slices[i];
      ds_destroy (&slice->label);
    }
  free (pie->slices);
  free (pie);
}

const struct chart_item_class piechart_class =
  {
    piechart_destroy
  };
