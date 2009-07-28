/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2008, 2009 Free Software Foundation, Inc.

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

#include <output/charts/box-whisker.h>

#include <math.h>
#include <assert.h>

#include <libpspp/misc.h>
#include <math/chart-geometry.h>
#include <math/box-whisker.h>
#include <output/chart.h>
#include <output/chart-provider.h>
#include <output/charts/plot-chart.h>

/* Draw a box-and-whiskers plot
*/

struct box
  {
    struct box_whisker *bw;
    char *label;
  };

struct boxplot
  {
    struct chart chart;
    double y_min;
    double y_max;
    char *title;
    struct box *boxes;
    size_t n_boxes, boxes_allocated;
  };

static const struct chart_class boxplot_chart_class;

struct boxplot *
boxplot_create (double y_min, double y_max, const char *title)
{
  struct boxplot *boxplot = xmalloc (sizeof *boxplot);
  chart_init (&boxplot->chart, &boxplot_chart_class);
  boxplot->y_min = y_min;
  boxplot->y_max = y_max;
  boxplot->title = xstrdup (title);
  boxplot->boxes = NULL;
  boxplot->n_boxes = boxplot->boxes_allocated = 0;
  return boxplot;
}

void
boxplot_add_box (struct boxplot *boxplot,
                 struct box_whisker *bw, const char *label)
{
  struct box *box;
  if (boxplot->n_boxes >= boxplot->boxes_allocated)
    boxplot->boxes = x2nrealloc (boxplot->boxes, &boxplot->boxes_allocated,
                                 sizeof *boxplot->boxes);
  box = &boxplot->boxes[boxplot->n_boxes++];
  box->bw = bw;
  box->label = xstrdup (label);
}

struct chart *
boxplot_get_chart (struct boxplot *boxplot)
{
  return &boxplot->chart;
}

/* Draw an OUTLIER on the plot CH
 * at CENTRELINE
 */
static void
draw_case (plPlotter *lp, const struct chart_geometry *geom, double centreline,
	   const struct outlier *outlier)
{

#define MARKER_CIRCLE 4
#define MARKER_STAR 3

  pl_fmarker_r(lp,
	       centreline,
	       geom->data_bottom + (outlier->value - geom->y_min) * geom->ordinate_scale,
	       outlier->extreme ? MARKER_STAR : MARKER_CIRCLE,
	       20);

  pl_moverel_r(lp, 10,0);

  pl_alabel_r(lp, 'l', 'c', ds_cstr (&outlier->label));
}

static void
boxplot_draw_box (plPlotter *lp, const struct chart_geometry *geom,
                  double box_centre,
                  double box_width,
                  const struct box_whisker *bw,
                  const char *name)
{
  double whisker[2];
  double hinge[3];
  struct ll *ll;

  const struct ll_list *outliers;

  const double box_left = box_centre - box_width / 2.0;

  const double box_right = box_centre + box_width / 2.0;

  double box_bottom ;
  double box_top ;
  double bottom_whisker ;
  double top_whisker ;

  box_whisker_whiskers (bw, whisker);
  box_whisker_hinges (bw, hinge);

  box_bottom = geom->data_bottom + (hinge[0] - geom->y_min ) * geom->ordinate_scale;

  box_top = geom->data_bottom + (hinge[2] - geom->y_min ) * geom->ordinate_scale;

  bottom_whisker = geom->data_bottom + (whisker[0] - geom->y_min) *
    geom->ordinate_scale;

  top_whisker = geom->data_bottom + (whisker[1] - geom->y_min) * geom->ordinate_scale;

  pl_savestate_r(lp);

  /* Draw the box */
  pl_savestate_r (lp);
  pl_fillcolor_r (lp,
                  geom->fill_colour.red * 257,
                  geom->fill_colour.green * 257,
                  geom->fill_colour.blue * 257);
  pl_filltype_r (lp,1);
  pl_fbox_r (lp,
	    box_left,
	    box_bottom,
	    box_right,
	    box_top);

  pl_restorestate_r (lp);

  /* Draw the median */
  pl_savestate_r (lp);
  pl_linewidth_r (lp, 5);
  pl_fline_r (lp,
	     box_left,
	     geom->data_bottom + (hinge[1] - geom->y_min) * geom->ordinate_scale,
	     box_right,
	     geom->data_bottom + (hinge[1] - geom->y_min) * geom->ordinate_scale);
  pl_restorestate_r (lp);

  /* Draw the bottom whisker */
  pl_fline_r (lp,
	     box_left,
	     bottom_whisker,
	     box_right,
	     bottom_whisker);

  /* Draw top whisker */
  pl_fline_r (lp,
	     box_left,
	     top_whisker,
	     box_right,
	     top_whisker);


  /* Draw centre line.
     (bottom half) */
  pl_fline_r (lp,
	     box_centre, bottom_whisker,
	     box_centre, box_bottom);

  /* (top half) */
  pl_fline_r (lp,
	     box_centre, top_whisker,
	     box_centre, box_top);

  outliers = box_whisker_outliers (bw);
  for (ll = ll_head (outliers);
       ll != ll_null (outliers); ll = ll_next (ll))
    {
      const struct outlier *outlier = ll_data (ll, struct outlier, ll);
      draw_case (lp, geom, box_centre, outlier);
    }

  /* Draw  tick  mark on x axis */
  draw_tick(lp, geom, TICK_ABSCISSA, box_centre - geom->data_left, "%s", name);

  pl_restorestate_r(lp);
}

static void
boxplot_draw_yscale (plPlotter *lp, struct chart_geometry *geom,
                     double y_max, double y_min)
{
  double y_tick;
  double d;

  geom->y_max = y_max;
  geom->y_min = y_min;

  y_tick = chart_rounded_tick (fabs (geom->y_max - geom->y_min) / 5.0);

  geom->y_min = (ceil (geom->y_min / y_tick) - 1.0) * y_tick;

  geom->y_max = (floor (geom->y_max / y_tick) + 1.0) * y_tick;

  geom->ordinate_scale = (fabs (geom->data_top - geom->data_bottom)
                          / fabs (geom->y_max - geom->y_min));

  /* Move to data bottom-left */
  pl_move_r (lp, geom->data_left, geom->data_bottom);

  for (d = geom->y_min; d <= geom->y_max; d += y_tick)
    draw_tick (lp, geom, TICK_ORDINATE,
               (d - geom->y_min) * geom->ordinate_scale, "%g", d);
}

static void
boxplot_chart_draw (const struct chart *chart, plPlotter *lp)
{
  const struct boxplot *boxplot = (struct boxplot *) chart;
  struct chart_geometry geom;
  double box_width;
  size_t i;

  chart_geometry_init (lp, &geom);
  boxplot_draw_yscale (lp, &geom, boxplot->y_max, boxplot->y_min);
  chart_write_title (lp, &geom, "%s", boxplot->title);

  box_width = (geom.data_right - geom.data_left) / boxplot->n_boxes / 2.0;
  for (i = 0; i < boxplot->n_boxes; i++)
    {
      const struct box *box = &boxplot->boxes[i];
      const double box_centre = (i * 2 + 1) * box_width + geom.data_left;
      boxplot_draw_box (lp, &geom, box_centre, box_width, box->bw, box->label);
    }

  chart_geometry_free (lp);
}

static void
boxplot_chart_destroy (struct chart *chart)
{
  struct boxplot *boxplot = (struct boxplot *) chart;
  size_t i;

  free (boxplot->title);
  for (i = 0; i < boxplot->n_boxes; i++)
    {
      struct box *box = &boxplot->boxes[i];
      struct statistic *statistic = &box->bw->parent.parent;
      statistic->destroy (statistic);
      free (box->label);
    }
  free (boxplot->boxes);
  free (boxplot);
}

static const struct chart_class boxplot_chart_class =
  {
    boxplot_chart_draw,
    boxplot_chart_destroy
  };
