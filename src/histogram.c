/* PSPP - computes sample statistics.
   Copyright (C) 2004 Free Software Foundation, Inc.
   Written by John Darrington <john@darrington.wattle.id.au>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <stdio.h>
#include <plot.h>
#include <stdarg.h>
#include <math.h>
#include "hash.h"

#include "var.h"
#include "chart.h"

/* Number of bins in which to divide data */
#define BINS 15

/* The approximate no of ticks on the y axis */
#define YTICKS 10

#define M_PI ( 22.0 / 7.0 )


static double gaussian(double x, double mu, double sigma ) ;


static double
gaussian(double x, double mu, double sigma )
{
  return (exp( - (( x - mu )* (x - mu) / (2.0 * sigma * sigma)  ))
    / ( sigma * sqrt( M_PI * 2.0) ))  ;
}


/* Adjust tick to be a sensible value */
void adjust_tick(double *tick);


/* Write the legend of the chart */
static void
write_legend(struct chart *ch, struct normal_curve *norm)
{
  char buf[100];
  pl_savestate_r(ch->lp);

  sprintf(buf,"N = %.2f",norm->N);
  pl_move_r(ch->lp, ch->legend_left, ch->data_bottom);
  pl_alabel_r(ch->lp,0,'b',buf);

  sprintf(buf,"Mean = %.1f",norm->mean);
  pl_fmove_r(ch->lp,ch->legend_left,ch->data_bottom + ch->font_size * 1.5);
  pl_alabel_r(ch->lp,0,'b',buf);

  sprintf(buf,"Std. Dev = %.2f",norm->stddev);
  pl_fmove_r(ch->lp,ch->legend_left,ch->data_bottom + ch->font_size * 1.5 * 2);
  pl_alabel_r(ch->lp,0,'b',buf);

  pl_restorestate_r(ch->lp);    
}




/* Draw a histogram.
   If show_normal is non zero then superimpose a normal curve
*/
void
draw_histogram(struct chart *ch, 
	       const struct variable *var,
	       const char *title, 
	       struct normal_curve *norm,
	       int show_normal)
{

  double d;
  int count;

  double x_min = DBL_MAX;
  double x_max = -DBL_MAX;
  double y_min = DBL_MAX;
  double y_max = -DBL_MAX;
  
  double y_tick ;


  double ordinate_values[BINS];

  const struct freq_tab *frq_tab = &var->p.frq.tab ;

  struct hsh_iterator hi;
  struct hsh_table *fh = frq_tab->data;
  struct freq *frq;

  double interval_size = fabs(ch->data_right - ch->data_left) / ( BINS );

  /* Find out the extremes of the x value 
     FIXME: These don't need to be calculated here, since the 
     calling routine should know them */

  for ( frq = hsh_first(fh,&hi); frq != 0; frq = hsh_next(fh,&hi) ) 
    {
      if ( frq->v.f < x_min ) x_min = frq->v.f ;
      if ( frq->v.f > x_max ) x_max = frq->v.f ;
    }

  double x_interval = fabs(x_max - x_min) / ( BINS - 1);

  
  double abscissa_scale = 
    fabs( (ch->data_right -  ch->data_left) / (x_max - x_min) ) ;


  /* Find out the bin values */
  for ( count = 0, d = x_min; d <= x_max ; d += x_interval, ++count )
    {

      double y = 0;

      for ( frq = hsh_first(fh,&hi); frq != 0; frq = hsh_next(fh,&hi) ) 
	{
	  if ( frq->v.f >= d && frq->v.f < d + x_interval )
	    y += frq->c;
	}

      ordinate_values[count] = y ;

      if ( y > y_max ) y_max = y ;
      if ( y < y_min ) y_min = y;
    }

  y_tick = ( y_max - y_min ) / (double) (YTICKS - 1) ;

  adjust_tick(&y_tick);

  y_min = floor( y_min / y_tick ) * y_tick ;
  y_max = ceil( y_max / y_tick ) * y_tick ;

  double ordinate_scale =
    fabs(ch->data_top -  ch->data_bottom) / fabs(y_max - y_min) ;
  

  /* Move to data bottom-left */
  pl_move_r(ch->lp, ch->data_left, ch->data_bottom);

  pl_savestate_r(ch->lp);
  pl_fillcolorname_r(ch->lp, ch->fill_colour); 
  pl_filltype_r(ch->lp,1);

  /* Draw the histogram */
  for ( count = 0, d = x_min; d <= x_max ; d += x_interval, ++count )
    {
      const double x = count * interval_size ;
      pl_savestate_r(ch->lp);

      draw_tick (ch, TICK_ABSCISSA, x + (interval_size / 2.0 ), "%.1f",d);

      pl_fboxrel_r(ch->lp, x, 0, x + interval_size, 
		   ordinate_values[count] * ordinate_scale );

      pl_restorestate_r(ch->lp);

    }
  pl_restorestate_r(ch->lp);

  /* Put the y axis on */
  for ( d = y_min; d <= y_max ; d += y_tick )
    {
      draw_tick (ch, TICK_ORDINATE, (d - y_min ) * ordinate_scale, "%g", d);
    }

  /* Write the abscissa label */
  pl_move_r(ch->lp,ch->data_left, ch->abscissa_top);
  pl_alabel_r(ch->lp,0,'t',var->label ? var->label : var->name);

 
  /* Write the ordinate label */
  pl_savestate_r(ch->lp);
  pl_move_r(ch->lp,ch->data_bottom, ch->ordinate_right);
  pl_textangle_r(ch->lp,90);
  pl_alabel_r(ch->lp,0,0,"Frequency");

  pl_restorestate_r(ch->lp);


  chart_write_title(ch, title);


  /* Write the legend */
  write_legend(ch,norm);


  if ( show_normal  )
  {
    /* Draw the normal curve */
    double d;

    pl_move_r(ch->lp, ch->data_left, ch->data_bottom);    
    for( d = ch->data_left; 
	 d <= ch->data_right ; 
	 d += (ch->data_right - ch->data_left) / 100.0)
      {
	const double x = (d  - ch->data_left  - interval_size / 2.0  ) / 
	  abscissa_scale  + x_min ; 
	
	pl_fcont_r(ch->lp,  d,
		   ch->data_bottom +
		   norm->N * gaussian(x, norm->mean, norm->stddev) 
		   * ordinate_scale);

      }
    pl_endpath_r(ch->lp);
  }

}



double 
log10(double x)
{
  return log(x) / log(10.0) ;
}

  
/* Adjust tick to be a sensible value */
void
adjust_tick(double *tick)
{
    int i;
    const double standard_ticks[] = {1, 2, 5};

    const double factor = pow(10,ceil(log10(standard_ticks[0] / *tick))) ;

    for (i = 2  ; i >=0 ; --i) 
      {
	if ( *tick > standard_ticks[i] / factor ) 
	  {
	    *tick = standard_ticks[i] / factor ;
	    break;
	  }
      }
    
  }

