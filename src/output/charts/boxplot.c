/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2008, 2009, 2011 Free Software Foundation, Inc.

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

#include "output/charts/boxplot.h"

#include "math/box-whisker.h"
#include "output/chart-item-provider.h"

struct boxplot *
boxplot_create (double y_min, double y_max, const char *title)
{
  struct boxplot *boxplot = xmalloc (sizeof *boxplot);
  chart_item_init (&boxplot->chart_item, &boxplot_class, title);
  boxplot->y_min = y_min;
  boxplot->y_max = y_max;
  boxplot->boxes = NULL;
  boxplot->n_boxes = boxplot->boxes_allocated = 0;
  return boxplot;
}

void
boxplot_add_box (struct boxplot *boxplot,
                 struct box_whisker *bw, const char *label)
{
  struct boxplot_box *box;
  if (boxplot->n_boxes >= boxplot->boxes_allocated)
    boxplot->boxes = x2nrealloc (boxplot->boxes, &boxplot->boxes_allocated,
                                 sizeof *boxplot->boxes);
  box = &boxplot->boxes[boxplot->n_boxes++];
  box->bw = bw;
  box->label = xstrdup (label);
}

static void
boxplot_chart_destroy (struct chart_item *chart_item)
{
  struct boxplot *boxplot = to_boxplot (chart_item);
  size_t i;

  for (i = 0; i < boxplot->n_boxes; i++)
    {
      struct boxplot_box *box = &boxplot->boxes[i];
      struct statistic *statistic = &box->bw->parent.parent;
      statistic->destroy (statistic);
      free (box->label);
    }
  free (boxplot->boxes);
  free (boxplot);
}

const struct chart_item_class boxplot_class =
  {
    boxplot_chart_destroy
  };
