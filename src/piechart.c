/* PSPP - draws pie charts of sample statistics

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
#include <float.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "value-labels.h"


/* Pie charts of course need to know Pi :) */
#define M_PI ( 22.0 / 7.0 ) 


#define min(A,B) ((A>B)?B:A)


/* Draw a single slice of the pie */
void
draw_segment(struct chart *ch, 
	     double centre_x, double centre_y, 
	     double radius,
	     double start_angle, double segment_angle,
	     const char *colour) ;


/* Draw a pie chart */
void
draw_piechart(struct chart *ch, const struct variable *var)
{
  int i;

  const struct freq_tab *frq_tab = &var->p.frq.tab ;

  const int n_data = frq_tab->n_valid;
  const double left_label = ch->data_left + 
    (ch->data_right - ch->data_left)/10.0;

  const double right_label = ch->data_right - 
    (ch->data_right - ch->data_left)/10.0;

  const double centre_x = (ch->data_right + ch->data_left ) / 2.0 ;
  const double centre_y = (ch->data_top + ch->data_bottom ) / 2.0 ;

  const double radius = min( 
			    5.0 / 12.0 * (ch->data_top - ch->data_bottom),
			    1.0 / 4.0 * (ch->data_right - ch->data_left)
			    );


  chart_write_title(ch, var->label ? var->label: var->name);

    
  for (i = 0 ; i < n_data ; ++i ) 
    {
      static double angle=0.0;
      const struct freq frq = frq_tab->valid[i];

      const double segment_angle = 
	frq.c / frq_tab->valid_cases * 2 * M_PI ;

      char *label = val_labs_find (var->val_labs, frq.v );
      if ( !label ) 
	{
	  static char l[20];
	  snprintf(l,20,"%g",frq.v.f);
	  label = l;
	}

      const double label_x = centre_x - 
	radius * sin(angle + segment_angle/2.0);

      const double label_y = centre_y + 
	radius * cos(angle + segment_angle/2.0);

      /* Fill the segment */
      draw_segment(ch,
		   centre_x, centre_y, radius, 
		   angle, segment_angle,
		   data_colour[i]);
	
      /* Now add the labels */
      if ( label_x < centre_x ) 
	{
	  pl_line_r(ch->lp, label_x, label_y,
		    left_label, label_y );
	  pl_moverel_r(ch->lp,0,5);
	  pl_alabel_r(ch->lp,0,0,label);
	}
      else
	{
	  pl_line_r(ch->lp, 
		    label_x, label_y,
		    right_label, label_y
		    );
	  pl_moverel_r(ch->lp,0,5);
	  pl_alabel_r(ch->lp,'r',0,label);
	}

      angle += segment_angle;

    }

  /* Draw an outline to the pie */
  pl_filltype_r(ch->lp,0);
  pl_fcircle_r (ch->lp, centre_x, centre_y, radius);

}



void
fill_segment(struct chart *ch, 
	     double x0, double y0, 
	     double radius,
	     double start_angle, double segment_angle) ;


/* Fill a segment with the current fill colour */
void
fill_segment(struct chart *ch, 
	     double x0, double y0, 
	     double radius,
	     double start_angle, double segment_angle)
{

  const double start_x  = x0 - radius * sin(start_angle);
  const double start_y  = y0 + radius * cos(start_angle);

  const double stop_x   = 
    x0 - radius * sin(start_angle + segment_angle); 

  const double stop_y   = 
    y0 + radius * cos(start_angle + segment_angle);

  assert(segment_angle <= 2 * M_PI);
  assert(segment_angle >= 0);

  if ( segment_angle > M_PI ) 
    {
      /* Then we must draw it in two halves */
      fill_segment(ch, x0, y0, radius, start_angle, segment_angle / 2.0 );
      fill_segment(ch, x0, y0, radius, start_angle + segment_angle / 2.0,
		   segment_angle / 2.0 );
    }
  else
    {
      pl_move_r(ch->lp, x0, y0);

      pl_cont_r(ch->lp, stop_x, stop_y);
      pl_cont_r(ch->lp, start_x, start_y);

      pl_arc_r(ch->lp,
	       x0, y0,
	       stop_x, stop_y,
	       start_x, start_y
	       );

      pl_endpath_r(ch->lp);
    }
}



/* Draw a single slice of the pie */
void
draw_segment(struct chart *ch, 
	     double x0, double y0, 
	     double radius,
	     double start_angle, double segment_angle, 
	     const char *colour)
{
  const double start_x  = x0 - radius * sin(start_angle);
  const double start_y  = y0 + radius * cos(start_angle);

  pl_savestate_r(ch->lp);

  pl_savestate_r(ch->lp);
  pl_colorname_r(ch->lp, colour);
  
  pl_pentype_r(ch->lp,1);
  pl_filltype_r(ch->lp,1);

  fill_segment(ch, x0, y0, radius, start_angle, segment_angle);
  pl_restorestate_r(ch->lp);

  /* Draw line dividing segments */
  pl_pentype_r(ch->lp, 1);
  pl_fline_r(ch->lp, x0, y0, start_x, start_y);
	

  pl_restorestate_r(ch->lp);
}
