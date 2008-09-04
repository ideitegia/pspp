/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2008 Free Software Foundation, Inc.

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
#include <assert.h>
#include <libpspp/misc.h>

#include <output/charts/box-whisker.h>
#include <output/charts/plot-chart.h>

#include <output/chart.h>
#include <math/chart-geometry.h>
#include <math/box-whisker.h>

/* Draw a box-and-whiskers plot
*/

/* Draw an OUTLIER on the plot CH
 * at CENTRELINE
 */
static void
draw_case (struct chart *ch, double centreline,
	   const struct outlier *outlier)
{

#define MARKER_CIRCLE 4
#define MARKER_STAR 3

  pl_fmarker_r(ch->lp,
	       centreline,
	       ch->data_bottom + (outlier->value - ch->y_min) * ch->ordinate_scale,
	       outlier->extreme ? MARKER_STAR : MARKER_CIRCLE,
	       20);

  pl_moverel_r(ch->lp, 10,0);

  pl_alabel_r(ch->lp, 'l', 'c', ds_cstr (&outlier->label));
}


void
boxplot_draw_boxplot (struct chart *ch,
		      double box_centre,
		      double box_width,
		      const struct box_whisker *bw,
		      const char *name)
{
  double whisker[2];
  double hinge[3];

  const struct ll_list *outliers;

  const double box_left = box_centre - box_width / 2.0;

  const double box_right = box_centre + box_width / 2.0;

  double box_bottom ;
  double box_top ;
  double bottom_whisker ;
  double top_whisker ;

  box_whisker_whiskers (bw, whisker);
  box_whisker_hinges (bw, hinge);

  box_bottom = ch->data_bottom + (hinge[0] - ch->y_min ) * ch->ordinate_scale;

  box_top = ch->data_bottom + (hinge[2] - ch->y_min ) * ch->ordinate_scale;

  bottom_whisker = ch->data_bottom + (whisker[0] - ch->y_min) *
    ch->ordinate_scale;

  top_whisker = ch->data_bottom + (whisker[1] - ch->y_min) * ch->ordinate_scale;

  pl_savestate_r(ch->lp);

  /* Draw the box */
  pl_savestate_r (ch->lp);
  pl_fillcolorname_r (ch->lp, ch->fill_colour);
  pl_filltype_r (ch->lp,1);
  pl_fbox_r (ch->lp,
	    box_left,
	    box_bottom,
	    box_right,
	    box_top);

  pl_restorestate_r (ch->lp);

  /* Draw the median */
  pl_savestate_r (ch->lp);
  pl_linewidth_r (ch->lp, 5);
  pl_fline_r (ch->lp,
	     box_left,
	     ch->data_bottom + (hinge[1] - ch->y_min) * ch->ordinate_scale,
	     box_right,
	     ch->data_bottom + (hinge[1] - ch->y_min) * ch->ordinate_scale);
  pl_restorestate_r (ch->lp);

  /* Draw the bottom whisker */
  pl_fline_r (ch->lp,
	     box_left,
	     bottom_whisker,
	     box_right,
	     bottom_whisker);

  /* Draw top whisker */
  pl_fline_r (ch->lp,
	     box_left,
	     top_whisker,
	     box_right,
	     top_whisker);


  /* Draw centre line.
     (bottom half) */
  pl_fline_r (ch->lp,
	     box_centre, bottom_whisker,
	     box_centre, box_bottom);

  /* (top half) */
  pl_fline_r (ch->lp,
	     box_centre, top_whisker,
	     box_centre, box_top);

  outliers = box_whisker_outliers (bw);
  for (struct ll *ll = ll_head (outliers);
       ll != ll_null (outliers); ll = ll_next (ll))
    {
      const struct outlier *outlier = ll_data (ll, struct outlier, ll);
      draw_case (ch, box_centre, outlier);
    }

  /* Draw  tick  mark on x axis */
  draw_tick(ch, TICK_ABSCISSA, box_centre - ch->data_left, name);

  pl_restorestate_r(ch->lp);
}

void
boxplot_draw_yscale (struct chart *ch, double y_max, double y_min)
{
  double y_tick;
  double d;

  if ( !ch )
     return ;

  ch->y_max  = y_max;
  ch->y_min  = y_min;

  y_tick = chart_rounded_tick (fabs(ch->y_max - ch->y_min) / 5.0);

  ch->y_min = (ceil( ch->y_min  / y_tick ) - 1.0  ) * y_tick;

  ch->y_max = ( floor( ch->y_max  / y_tick ) + 1.0  ) * y_tick;

  ch->ordinate_scale = fabs(ch->data_top - ch->data_bottom)
    / fabs(ch->y_max - ch->y_min) ;

  /* Move to data bottom-left */
  pl_move_r(ch->lp,
	    ch->data_left, ch->data_bottom);

  for ( d = ch->y_min; d <= ch->y_max ; d += y_tick )
    {
      draw_tick (ch, TICK_ORDINATE, (d - ch->y_min ) * ch->ordinate_scale, "%g", d);
    }
}
