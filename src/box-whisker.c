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


#include "chart.h"
#include <math.h>

/* Draw a box-and-whiskers plot
*/

struct data_stats 
{
  double ptile0  ;
  double ptile25 ;
  double median  ;
  double ptile75 ;

  double ptile100;

  double outlier ;
};


const struct data_stats stats1 = {
  40,
  45,
  54,
  60,
  70,

  33
};

const struct data_stats stats2 = {
  30,
  40,
  45,
  54,
  60,


  72
};





static const double y_min = 25;
static const double y_max = 75;
static const double y_tick = 10;



#define min(A,B) ((A>B)?B:A)


void draw_box_and_whiskers(struct chart *ch,
			   double box_centre, const struct data_stats *s, 
			   const char *name);


static double ordinate_scale;

void
draw_box_whisker_chart(struct chart *ch, const char *title)
{
  double d;

  ordinate_scale = fabs(ch->data_top -  ch->data_bottom) / fabs(y_max - y_min) ;


  chart_write_title(ch, title);

  

  /* Move to data bottom-left */
  pl_move_r(ch->lp, 
		  ch->data_left, ch->data_bottom);

  for ( d = y_min; d <= y_max ; d += y_tick )
    {
      draw_tick (ch, TICK_ORDINATE, (d - y_min ) * ordinate_scale, "%g", d);
    }

  draw_box_and_whiskers(ch,
			ch->data_left  + 1.0/4.0 * (ch->data_right - ch->data_left) ,
			&stats1,"Stats1"
			);

  draw_box_and_whiskers(ch,
			ch->data_left + 3.0/4.0 * (ch->data_right - ch->data_left),
			&stats2,"Stats2"
			);


}


void 
draw_box_and_whiskers(struct chart *ch,
		      double box_centre, const struct data_stats *s,
		      const char *name)
{

  const double box_width = (ch->data_right - ch->data_left) / 4.0;

  const double box_left = box_centre - box_width / 2.0;

  const double box_right = box_centre + box_width / 2.0;


  const double box_bottom = 
    ch->data_bottom + ( s->ptile25 - y_min ) * ordinate_scale;


  const double box_top = 
    ch->data_bottom + ( s->ptile75 - y_min ) * ordinate_scale;


  const double iq_range = s->ptile75 - s->ptile25;

  const double bottom_whisker = 
    ch->data_bottom + (min(s->ptile0,s->ptile25 + iq_range*1.5)  - y_min ) * 
    ordinate_scale;

  const double top_whisker =
    ch->data_bottom + (min(s->ptile100,s->ptile75 + iq_range*1.5)  - y_min ) * 
    ordinate_scale;
	
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
	     ch->data_bottom + ( s->median - y_min ) * ordinate_scale,
	     box_right,   
	     ch->data_bottom + ( s->median - y_min ) * ordinate_scale);
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


  /* Draw an outlier */
  pl_fcircle_r(ch->lp,
	       box_centre, 
	       ch->data_bottom + (s->outlier - y_min ) * ordinate_scale,
	       5);

  pl_moverel_r(ch->lp, 10,0);
  pl_alabel_r(ch->lp,'l','c',"123");


  /* Draw  tick  mark on x axis */
  draw_tick(ch, TICK_ABSCISSA, box_centre - ch->data_left, name);

  pl_restorestate_r(ch->lp);

}

