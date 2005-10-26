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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>
#include <stdio.h>
#include <plot.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <float.h>
#include <assert.h>
#include <math.h>

#include "chart.h"
#include "str.h"
#include "alloc.h"
#include "som.h"
#include "output.h"


const char *data_colour[] = {
  "brown",
  "red",
  "orange",
  "yellow",
  "green",
  "blue",
  "violet",
  "grey",
  "pink"
};



struct chart *
chart_create(void)
{
  struct chart *chart;
  struct outp_driver *d;

  d = outp_drivers (NULL);
  if (d == NULL)
    return NULL;
  
  chart = xmalloc (sizeof *chart);
  d->class->initialise_chart(d, chart);
  if (!chart->lp) 
    {
      free (chart);
      return NULL; 
    }

  if (pl_openpl_r (chart->lp) < 0)      /* open Plotter */
    return NULL;
  
  pl_fspace_r (chart->lp, 0.0, 0.0, 1000.0, 1000.0); /* set coordinate system */
  pl_flinewidth_r (chart->lp, 0.25);    /* set line thickness */
  pl_pencolorname_r (chart->lp, "black"); 

  pl_erase_r (chart->lp);               /* erase graphics display */
  pl_filltype_r(chart->lp,0);

  pl_savestate_r(chart->lp);

  /* Set default chartetry */
  chart->data_top =   900;
  chart->data_right = 800;
  chart->data_bottom = 120;
  chart->data_left = 150;
  chart->abscissa_top = 70;
  chart->ordinate_right = 120;
  chart->title_bottom = 920;
  chart->legend_left = 810;
  chart->legend_right = 1000;
  chart->font_size = 0;
  strcpy(chart->fill_colour,"red");

  /* Get default font size */
  if ( !chart->font_size) 
    chart->font_size = pl_fontsize_r(chart->lp, -1);

  /* Draw the data area */
  pl_box_r(chart->lp, 
	   chart->data_left, chart->data_bottom, 
	   chart->data_right, chart->data_top);

  return chart;
}

/* Draw a tick mark at position
   If label is non zero, then print it at the tick mark
*/
void
draw_tick(struct chart *chart, 
	  enum tick_orientation orientation, 
	  double position, 
	  const char *label, ...)
{
  const int tickSize = 10;

  assert(chart);

  pl_savestate_r(chart->lp);

  pl_move_r(chart->lp, chart->data_left, chart->data_bottom);

  if ( orientation == TICK_ABSCISSA ) 
    pl_flinerel_r(chart->lp, position, 0, position, -tickSize);
  else if (orientation == TICK_ORDINATE ) 
      pl_flinerel_r(chart->lp, 0, position, -tickSize, position);
  else
    assert(0);

  if ( label ) {
    char buf[10];
    va_list ap;
    va_start(ap,label);
    vsnprintf(buf,10,label,ap);

    if ( orientation == TICK_ABSCISSA ) 
      pl_alabel_r(chart->lp, 'c','t', buf);
    else if (orientation == TICK_ORDINATE ) 
      {
	if ( fabs(position) < DBL_EPSILON )
	    pl_moverel_r(chart->lp, 0, 10);

	pl_alabel_r(chart->lp, 'r','c', buf);
      }

    va_end(ap);
  }
    
  pl_restorestate_r(chart->lp);
}




/* Write the title on a chart*/
void  
chart_write_title(struct chart *chart, const char *title, ...)
{
  va_list ap;
  char buf[100];

  if ( ! chart ) 
	  return ;

  pl_savestate_r(chart->lp);
  pl_ffontsize_r(chart->lp,chart->font_size * 1.5);
  pl_move_r(chart->lp,chart->data_left, chart->title_bottom);

  va_start(ap,title);
  vsnprintf(buf,100,title,ap);
  pl_alabel_r(chart->lp,0,0,buf);
  va_end(ap);

  pl_restorestate_r(chart->lp);
}


extern struct som_table_class tab_table_class;

void
chart_submit(struct chart *chart)
{
  struct som_entity s;
  struct outp_driver *d;

  if ( ! chart ) 
     return ;

  pl_restorestate_r(chart->lp);

  s.class = &tab_table_class;
  s.ext = chart;
  s.type = SOM_CHART;
  som_submit (&s);
  
  if (pl_closepl_r (chart->lp) < 0)     /* close Plotter */
    {
      fprintf (stderr, "Couldn't close Plotter\n");
    }

  pl_deletepl_r(chart->lp);

  pl_deleteplparams(chart->pl_params);

  d = outp_drivers (NULL);
  d->class->finalise_chart(d, chart);
  free(chart);
}


/* Set the scale for the abscissa */
void 
chart_write_xscale(struct chart *ch, double min, double max, int ticks)
{
  double x;

  const double tick_interval = 
    chart_rounded_tick( (max - min) / (double) ticks);

  assert ( ch );


  ch->x_max = ceil( max / tick_interval ) * tick_interval ; 
  ch->x_min = floor ( min / tick_interval ) * tick_interval ;


  ch->abscissa_scale = fabs(ch->data_right - ch->data_left) / 
    fabs(ch->x_max - ch->x_min);

  for(x = ch->x_min ; x <= ch->x_max; x += tick_interval )
    {
      draw_tick (ch, TICK_ABSCISSA, 
		 (x - ch->x_min) * ch->abscissa_scale, "%g", x);
    }

}


/* Set the scale for the ordinate */
void 
chart_write_yscale(struct chart *ch, double smin, double smax, int ticks)
{
  double y;

  const double tick_interval = 
    chart_rounded_tick( (smax - smin) / (double) ticks);


  if ( !ch ) 
	  return;

  ch->y_max = ceil  ( smax / tick_interval ) * tick_interval ; 
  ch->y_min = floor ( smin / tick_interval ) * tick_interval ;

  ch->ordinate_scale = 
    fabs(ch->data_top -  ch->data_bottom) / fabs(ch->y_max - ch->y_min) ;

  for(y = ch->y_min ; y <= ch->y_max; y += tick_interval )
    {
    draw_tick (ch, TICK_ORDINATE, 
	       (y - ch->y_min) * ch->ordinate_scale, "%g", y);
    }

}

