/* PSPP - a program for statistical analysis.
   Copyright (C) 2004 Free Software Foundation, Inc.

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

#include <output/chart.h>

#include <output/charts/plot-chart.h>
#include <output/charts/cartesian.h>
#include <libpspp/compiler.h>


/* Start a new vector called NAME */
void
chart_vector_start (struct chart *ch, const char *name)
{
  if ( ! ch )
    return ;

  pl_savestate_r (ch->lp);

  pl_colorname_r (ch->lp, data_colour [ch->n_datasets % N_CHART_COLOURS]);

  ch->n_datasets++;
  ch->dataset = xrealloc (ch->dataset, ch->n_datasets * sizeof (*ch->dataset));

  ch->dataset[ch->n_datasets - 1] = strdup (name);
}

/* Plot a data point */
void
chart_datum (struct chart *ch, int dataset UNUSED, double x, double y)
{
  if ( ! ch )
    return ;

  {
    const double x_pos =
      (x - ch->x_min) * ch->abscissa_scale + ch->data_left ;

    const double y_pos =
      (y - ch->y_min) * ch->ordinate_scale + ch->data_bottom ;

    pl_savestate_r(ch->lp);

    pl_fmarker_r(ch->lp, x_pos, y_pos, 6, 15);

    pl_restorestate_r(ch->lp);
  }
}

void
chart_vector_end (struct chart *ch)
{
  pl_endpath_r (ch->lp);
  pl_colorname_r (ch->lp, "black");
  ch->in_path = false;
  pl_restorestate_r (ch->lp);
}

/* Plot a data point */
void
chart_vector (struct chart *ch, double x, double y)
{
  if ( ! ch )
    return ;

  {
    const double x_pos =
      (x - ch->x_min) * ch->abscissa_scale + ch->data_left ;

    const double y_pos =
      (y - ch->y_min) * ch->ordinate_scale + ch->data_bottom ;

    if ( ch->in_path)
      pl_fcont_r (ch->lp, x_pos, y_pos);
    else
      {
	pl_fmove_r (ch->lp, x_pos, y_pos);
	ch->in_path = true;
      }
  }
}



/* Draw a line with slope SLOPE and intercept INTERCEPT.
   between the points limit1 and limit2.
   If lim_dim is CHART_DIM_Y then the limit{1,2} are on the
   y axis otherwise the x axis
*/
void
chart_line (struct chart *ch, double slope, double intercept,
	   double limit1, double limit2, enum CHART_DIM lim_dim)
{
  double x1, y1;
  double x2, y2 ;

  if ( ! ch )
    return ;


  if ( lim_dim == CHART_DIM_Y )
    {
      x1 = ( limit1 - intercept ) / slope ;
      x2 = ( limit2 - intercept ) / slope ;
      y1 = limit1;
      y2 = limit2;
    }
  else
    {
      x1 = limit1;
      x2 = limit2;
      y1 = slope * x1 + intercept;
      y2 = slope * x2 + intercept;
    }

  y1 = (y1 - ch->y_min) * ch->ordinate_scale + ch->data_bottom ;
  y2 = (y2 - ch->y_min) * ch->ordinate_scale + ch->data_bottom ;
  x1 = (x1 - ch->x_min) * ch->abscissa_scale + ch->data_left ;
  x2 = (x2 - ch->x_min) * ch->abscissa_scale + ch->data_left ;

  pl_savestate_r(ch->lp);

  pl_fline_r(ch->lp, x1, y1, x2, y2);

  pl_restorestate_r(ch->lp);
}
