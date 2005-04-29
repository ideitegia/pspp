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


#include "chart.h"
#include <math.h>
#include <assert.h>
#include "misc.h"

#include "factor_stats.h"


/* Draw a box-and-whiskers plot
*/

/* Draw an outlier on the plot CH
 * at CENTRELINE
 * The outlier is in (*wvp)[idx]
 * If EXTREME is non zero, then consider it to be an extreme
 * value
 */
void 
draw_outlier(struct chart *ch, double centreline, 
	     struct weighted_value **wvp, 
	     int idx,
	     short extreme);


void 
draw_outlier(struct chart *ch, double centreline, 
	     struct weighted_value **wvp, 
	     int idx,
	     short extreme
	     )
{
  char label[10];

#define MARKER_CIRCLE 4
#define MARKER_STAR 3

  pl_fmarker_r(ch->lp,
	       centreline,
	       ch->data_bottom + 
	       (wvp[idx]->v.f - ch->y_min ) * ch->ordinate_scale,
	       extreme?MARKER_STAR:MARKER_CIRCLE,
	       20);

  pl_moverel_r(ch->lp, 10,0);

  snprintf(label, 10, "%d", wvp[idx]->case_nos->num);
  
  pl_alabel_r(ch->lp, 'l', 'c', label);

}


void 
boxplot_draw_boxplot(struct chart *ch,
		     double box_centre, 
		     double box_width,
		     struct metrics *m,
		     const char *name)
{
  double whisker[2];
  int i;

  assert(m);


  const double *hinge = m->hinge;
  struct weighted_value **wvp = m->wvp;
  const int n_data = m->n_data;

  const double step = (hinge[2] - hinge[0]) * 1.5;


  const double box_left = box_centre - box_width / 2.0;

  const double box_right = box_centre + box_width / 2.0;


  const double box_bottom = 
    ch->data_bottom + ( hinge[0] - ch->y_min ) * ch->ordinate_scale;


  const double box_top = 
    ch->data_bottom + ( hinge[2] - ch->y_min ) * ch->ordinate_scale;

  /* Can't really draw a boxplot if there's no data */
  if ( n_data == 0 ) 
	  return ;

  whisker[1] = hinge[2];
  whisker[0] = wvp[0]->v.f;

  for ( i = 0 ; i < n_data ; ++i ) 
    {
      if ( hinge[2] + step >  wvp[i]->v.f) 
	whisker[1] = wvp[i]->v.f;

      if ( hinge[0] - step >  wvp[i]->v.f) 
	whisker[0] = wvp[i]->v.f;
    
    }
    
  
  const double bottom_whisker = 
    ch->data_bottom + ( whisker[0] - ch->y_min ) * ch->ordinate_scale;

  const double top_whisker = 
    ch->data_bottom + ( whisker[1] - ch->y_min ) * ch->ordinate_scale;

	
  pl_savestate_r(ch->lp);


  /* Draw the box */
  pl_savestate_r(ch->lp);
  pl_fillcolorname_r(ch->lp,ch->fill_colour);
  pl_filltype_r(ch->lp,1);
  pl_fbox_r(ch->lp, 
	    box_left,
	    box_bottom,
	    box_right,
	    box_top);

  pl_restorestate_r(ch->lp);


  
  /* Draw the median */
  pl_savestate_r(ch->lp);
  pl_linewidth_r(ch->lp,5);
  pl_fline_r(ch->lp, 
	     box_left, 
	     ch->data_bottom + ( hinge[1] - ch->y_min ) * ch->ordinate_scale,
	     box_right,   
	     ch->data_bottom + ( hinge[1] - ch->y_min ) * ch->ordinate_scale);
  pl_restorestate_r(ch->lp);


  /* Draw the bottom whisker */
  pl_fline_r(ch->lp, 
	     box_left, 
	     bottom_whisker,
	     box_right,   
	     bottom_whisker);

  /* Draw top whisker */
  pl_fline_r(ch->lp, 
	     box_left, 
	     top_whisker,
	     box_right,   
	     top_whisker);



  /* Draw centre line.
     (bottom half) */
  pl_fline_r(ch->lp, 
	     box_centre, bottom_whisker,
	     box_centre, box_bottom);

  /* (top half) */
  pl_fline_r(ch->lp, 
	     box_centre, top_whisker,
	     box_centre, box_top);

  /* Draw outliers */
  for ( i = 0 ; i < n_data ; ++i ) 
    {
      if ( wvp[i]->v.f >= hinge[2] + step ) 
	draw_outlier(ch, box_centre, wvp, i, 
		     ( wvp[i]->v.f > hinge[2] + 2 * step ) 
		     );

      if ( wvp[i]->v.f <= hinge[0] - step ) 
	draw_outlier(ch, box_centre, wvp, i, 
		     ( wvp[i]->v.f < hinge[0] - 2 * step )
		     );
    }


  /* Draw  tick  mark on x axis */
  draw_tick(ch, TICK_ABSCISSA, box_centre - ch->data_left, name);

  pl_restorestate_r(ch->lp);

}



void
boxplot_draw_yscale(struct chart *ch , double y_max, double y_min)
{
  double y_tick;
  double d;

  if ( !ch ) 
     return ;

  ch->y_max  = y_max;
  ch->y_min  = y_min;

  y_tick = chart_rounded_tick(fabs(ch->y_max - ch->y_min) / 5.0);

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
