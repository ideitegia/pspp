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


#include <stdio.h>
#include <plot.h>
#include <stdarg.h>
#include <math.h>
#include "chart.h"

#define CATAGORIES 6
#define SUB_CATAGORIES 3



static const    double x_min = 0;
static const    double x_max = 15.0;

static const char *cat_labels[] = 
  {
    "Age",
    "Intelligence",
    "Wealth",
    "Emotional",
    "cat 5",
    "cat 6",
    "cat 7",
    "cat 8",
    "cat 9",
    "cat 10",
    "cat 11"
  };




/* Subcatagories */
static const double data1[] =
{
  28,83,
  34,
  29,13,
   9,4,
   3,3,
   2,0, 
   1,0,
   0,
   1,1
};


static const double data2[] =
{
  45,13,
   9,4,
   3,43,
   2,0, 
   1,20,
   0,0,
  1,1,
  0,0
};

static const double data3[] =
  {
    23,18,
    0, 45,23, 9, 40, 24,4, 8
  };


static const char subcat_name[]="Gender";


struct subcat {
  const double *data;
  const char *label;
};

static const struct subcat sub_catagory[SUB_CATAGORIES] = 
  {
    {data1, "male"},
    {data2, "female"},
    {data3, "47xxy"} 
  };



static const    double y_min = 0;
static const    double y_max = 120.0;
static const    double y_tick = 20.0;



static void write_legend(struct chart *chart) ;


void
draw_barchart(struct chart *ch, const char *title, 
	      const char *xlabel, const char *ylabel, enum bar_opts opt)
{

  double d;
  int i;

  double interval_size = fabs(ch->data_right - ch->data_left) / ( CATAGORIES );
  
  double bar_width = interval_size / 1.1 ;

  if ( opt != BAR_STACKED ) 
      bar_width /= SUB_CATAGORIES;

  double ordinate_scale = fabs(ch->data_top -  ch->data_bottom) / fabs(y_max - y_min) ;

  /* Move to data bottom-left */
  pl_move_r(ch->lp, ch->data_left, ch->data_bottom);

  pl_savestate_r(ch->lp);
  pl_filltype_r(ch->lp,1);

  /* Draw the data */
  for (i = 0 ; i < CATAGORIES ; ++i ) 
    {
      int sc;
      double ystart=0.0;
      double x = i * interval_size;

      pl_savestate_r(ch->lp);

      draw_tick (ch, TICK_ABSCISSA, x + (interval_size/2 ), 
		 cat_labels[i]);

      for(sc = 0 ; sc < SUB_CATAGORIES ; ++sc ) 
	{
	  
	  pl_savestate_r(ch->lp);
	  pl_fillcolorname_r(ch->lp,data_colour[sc]);
	  
	  switch ( opt )
	    {
	    case BAR_GROUPED:
	      pl_fboxrel_r(ch->lp, 
			   x + (sc * bar_width ), 0,
			   x + (sc + 1) * bar_width, 
			     sub_catagory[sc].data[i] * ordinate_scale );
	      break;
	      

	    case BAR_STACKED:

	      pl_fboxrel_r(ch->lp, 
			   x, ystart, 
			   x + bar_width, 
			   ystart + sub_catagory[sc].data[i] * ordinate_scale );

	      ystart +=    sub_catagory[sc].data[i] * ordinate_scale ; 

	      break;

	    default:
	      break;
	    }
	  pl_restorestate_r(ch->lp);
	}

      pl_restorestate_r(ch->lp);
    }
  pl_restorestate_r(ch->lp);

  for ( d = y_min; d <= y_max ; d += y_tick )
    {

      draw_tick (ch, TICK_ORDINATE,
		 (d - y_min ) * ordinate_scale, "%g", d);
      
    }

  /* Write the abscissa label */
  pl_move_r(ch->lp,ch->data_left, ch->abscissa_top);
  pl_alabel_r(ch->lp,0,'t',xlabel);

 
  /* Write the ordinate label */
  pl_savestate_r(ch->lp);
  pl_move_r(ch->lp,ch->data_bottom, ch->ordinate_right);
  pl_textangle_r(ch->lp,90);
  pl_alabel_r(ch->lp,0,0,ylabel);
  pl_restorestate_r(ch->lp);


  chart_write_title(ch, title);

  write_legend(ch);
  

}





static void
write_legend(struct chart *chart)
{
  int sc;

  pl_savestate_r(chart->lp);

  pl_filltype_r(chart->lp,1);

  pl_move_r(chart->lp, chart->legend_left, 
	    chart->data_bottom + chart->font_size * SUB_CATAGORIES * 1.5);

  pl_alabel_r(chart->lp,0,'b',subcat_name);

  for (sc = 0 ; sc < SUB_CATAGORIES ; ++sc ) 
    {
      pl_fmove_r(chart->lp,
		 chart->legend_left,
		 chart->data_bottom + chart->font_size * sc  * 1.5);

      pl_savestate_r(chart->lp);    
      pl_fillcolorname_r(chart->lp,data_colour[sc]);
      pl_fboxrel_r (chart->lp,
		    0,0,
		    chart->font_size, chart->font_size);
      pl_restorestate_r(chart->lp);    

      pl_fmove_r(chart->lp,
		 chart->legend_left + chart->font_size * 1.5,
		 chart->data_bottom + chart->font_size * sc  * 1.5);

      pl_alabel_r(chart->lp,'l','b',sub_catagory[sc].label);
    }


  pl_restorestate_r(chart->lp);    
}
