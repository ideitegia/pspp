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


#include <math.h>
#include "chart.h"
#include <assert.h>



struct dataset
{
  int n_data;
  char *label;
};




#define DATASETS 2

static const struct dataset dataset[DATASETS] = 
  {
    { 13, "male"},
    { 11, "female"},
  };




static void
write_legend(struct chart *chart, const char *heading, int n);



/* Write the abscissa label */
void 
chart_write_xlabel(struct chart *ch, const char *label)
{

  pl_savestate_r(ch->lp);

  pl_move_r(ch->lp,ch->data_left, ch->abscissa_top);
  pl_alabel_r(ch->lp,0,'t',label);

  pl_restorestate_r(ch->lp);

}

/* Set the scale for the abscissa */
void 
chart_write_xscale(struct chart *ch, double min, double max, double tick)
{
  double x;

  ch->x_max = ceil( max / tick ) * tick ; 
  ch->x_min = floor ( min / tick ) * tick ;

  ch->abscissa_scale = fabs(ch->data_right - ch->data_left) / 
    fabs(ch->x_max - ch->x_min);

  for(x = ch->x_min ; x <= ch->x_max; x += tick )
      draw_tick (ch, TICK_ABSCISSA, (x - ch->x_min) * ch->abscissa_scale, "%g", x);

}


/* Set the scale for the ordinate */
void 
chart_write_yscale(struct chart *ch, double min, double max, double tick)
{
  double y;

  ch->y_max = ceil( max / tick ) * tick ; 
  ch->y_min = floor ( min / tick ) * tick ;

  ch->ordinate_scale = 
    fabs(ch->data_top -  ch->data_bottom) / fabs(ch->y_max - ch->y_min) ;

  for(y = ch->y_min ; y <= ch->y_max; y += tick )
    {
    draw_tick (ch, TICK_ORDINATE, 
	       (y - ch->y_min) * ch->ordinate_scale, "%g", y);
    }

}



/* Write the ordinate label */
void 
chart_write_ylabel(struct chart *ch, const char *label)
{
  pl_savestate_r(ch->lp);

  pl_move_r(ch->lp, ch->data_bottom, ch->ordinate_right);
  pl_textangle_r(ch->lp, 90);
  pl_alabel_r(ch->lp, 0, 0, label);

  pl_restorestate_r(ch->lp);
}



static void
write_legend(struct chart *chart, const char *heading, 
	     int n)
{
  int ds;

  pl_savestate_r(chart->lp);

  pl_filltype_r(chart->lp,1);

  pl_move_r(chart->lp, chart->legend_left, 
	    chart->data_bottom + chart->font_size * n * 1.5);

  pl_alabel_r(chart->lp,0,'b',heading);

  for (ds = 0 ; ds < n ; ++ds ) 
    {
      pl_fmove_r(chart->lp,
		 chart->legend_left,
		 chart->data_bottom + chart->font_size * ds  * 1.5);

      pl_savestate_r(chart->lp);    
      pl_fillcolorname_r(chart->lp,data_colour[ds]);
      pl_fboxrel_r (chart->lp,
		    0,0,
		    chart->font_size, chart->font_size);
      pl_restorestate_r(chart->lp);    

      pl_fmove_r(chart->lp,
		 chart->legend_left + chart->font_size * 1.5,
		 chart->data_bottom + chart->font_size * ds  * 1.5);

      pl_alabel_r(chart->lp,'l','b',dataset[ds].label);
    }


  pl_restorestate_r(chart->lp);    
}


/* Plot a data point */
void
chart_datum(struct chart *ch, int dataset, double x, double y)
{
  const double x_pos = 
    (x - ch->x_min) * ch->abscissa_scale + ch->data_left ; 

  const double y_pos = 
    (y - ch->y_min) * ch->ordinate_scale + ch->data_bottom ;


  pl_savestate_r(ch->lp);    
  
  pl_fmarker_r(ch->lp, x_pos, y_pos, 6, 15);

  pl_restorestate_r(ch->lp);    

}

/* Draw a line with slope SLOPE and intercept INTERCEPT.
   between the points limit1 and limit2.
   If lim_dim is CHART_DIM_Y then the limit{1,2} are on the 
   y axis otherwise the x axis
*/
void
chart_line(struct chart *ch, double slope, double intercept, 
	   double limit1, double limit2, enum CHART_DIM lim_dim)
{
  double x1, y1;
  double x2, y2 ;

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
