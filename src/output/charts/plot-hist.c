/* PSPP - a program for statistical analysis.
   Copyright  (C) 2004 Free Software Foundation, Inc.

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

#include <stdio.h>
#include <plot.h>
#include <math.h>
#include <gsl/gsl_histogram.h>
#include <gsl/gsl_randist.h>
#include <assert.h>

#include <output/charts/plot-hist.h>
#include <output/charts/plot-chart.h>

#include <data/variable.h>
#include <libpspp/hash.h>
#include <output/chart.h>
#include <math/histogram.h>
#include <math/moments.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Write the legend of the chart */
static void
histogram_write_legend (struct chart *ch, double n, double mean, double stddev)
{
  char buf[100];

  if (!ch)
    return ;

  pl_savestate_r (ch->lp);

  sprintf (buf, "N = %.2f", n);
  pl_move_r (ch->lp, ch->legend_left, ch->data_bottom);
  pl_alabel_r (ch->lp, 0, 'b', buf);

  sprintf (buf, "Mean = %.1f", mean);
  pl_fmove_r (ch->lp,ch->legend_left,ch->data_bottom + ch->font_size * 1.5);
  pl_alabel_r (ch->lp, 0, 'b', buf);

  sprintf (buf, "Std. Dev = %.2f", stddev);
  pl_fmove_r (ch->lp, ch->legend_left, ch->data_bottom + ch->font_size * 1.5 * 2);
  pl_alabel_r (ch->lp, 0, 'b', buf);

  pl_restorestate_r (ch->lp);
}

static void hist_draw_bar (struct chart *ch, const struct histogram *hist, int bar);


static void
hist_draw_bar (struct chart *ch, const struct histogram *hist, int bar)
{
  if (!ch)
    return ;

  {
    double upper;
    double lower;
    double height;

    const size_t bins = gsl_histogram_bins (hist->gsl_hist);
    const double x_pos = (ch->data_right - ch->data_left) * bar / (double) bins ;
    const double width = (ch->data_right - ch->data_left) / (double) bins ;

    assert ( 0 == gsl_histogram_get_range (hist->gsl_hist, bar, &lower, &upper));

    assert ( upper >= lower);

    height = gsl_histogram_get (hist->gsl_hist, bar) *
     (ch->data_top - ch->data_bottom) / gsl_histogram_max_val (hist->gsl_hist);

    pl_savestate_r (ch->lp);
    pl_move_r (ch->lp,ch->data_left, ch->data_bottom);
    pl_fillcolorname_r (ch->lp, ch->fill_colour);
    pl_filltype_r (ch->lp,1);


    pl_fboxrel_r (ch->lp,
		 x_pos, 0,
		 x_pos + width, height);

    pl_restorestate_r (ch->lp);

    {
      char buf[5];
      snprintf (buf,5,"%g", (upper + lower) / 2.0);
      draw_tick (ch, TICK_ABSCISSA,
		x_pos + width / 2.0, buf);
    }
  }
}



void
histogram_plot (const struct histogram *hist,
		const char *label,
		const struct moments1 *m)
{
  double mean, var, n;

  moments1_calculate (m, &n, &mean, &var, NULL,  NULL);

  histogram_plot_n (hist, label, n, mean, sqrt(var), m);
}

void
histogram_plot_n (const struct histogram *hist,
		  const char *label,
		  double n, double mean, double stddev,
		  bool show_normal)
{
  int i;
  int bins;

  struct chart *ch = chart_create ();

  chart_write_title (ch, _("HISTOGRAM"));

  chart_write_ylabel (ch, _("Frequency"));
  chart_write_xlabel (ch, label);

  if ( ! hist ) /* If this happens, probably all values are SYSMIS */
    {
      chart_submit (ch);
      return;
    }
  else
    {
      bins = gsl_histogram_bins (hist->gsl_hist);
    }

  chart_write_yscale (ch, 0, gsl_histogram_max_val (hist->gsl_hist), 5);

  for ( i = 0 ; i < bins ; ++i )
    hist_draw_bar (ch, hist, i);

  histogram_write_legend (ch, n, mean, stddev);

  if (show_normal)
    {
      /* Draw the normal curve */

      double d ;
      double x_min, x_max, not_used ;
      double abscissa_scale ;
      double ordinate_scale ;
      double range ;

      gsl_histogram_get_range (hist->gsl_hist, 0, &x_min, &not_used);
      range = not_used - x_min;
      gsl_histogram_get_range (hist->gsl_hist, bins - 1, &not_used, &x_max);

      abscissa_scale = (ch->data_right - ch->data_left) / (x_max - x_min);
      ordinate_scale = (ch->data_top - ch->data_bottom) /
	gsl_histogram_max_val (hist->gsl_hist) ;

      pl_move_r (ch->lp, ch->data_left, ch->data_bottom);
      for ( d = ch->data_left;
	    d <= ch->data_right ;
	    d += (ch->data_right - ch->data_left) / 100.0)
	{
	  const double x = (d - ch->data_left) / abscissa_scale + x_min ;
	  const double y = n * range *
	    gsl_ran_gaussian_pdf (x - mean, stddev);

	  pl_fcont_r (ch->lp,  d,  ch->data_bottom  + y * ordinate_scale);

	}
      pl_endpath_r (ch->lp);
    }

  chart_submit (ch);
}


