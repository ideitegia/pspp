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



static const    double y_min = 15;
static const    double y_max = 120.0;
static const    double y_tick = 20.0;



static const double x_min = -11.0;
static const double x_max = 19.0;
static const double x_tick = 5.0;


struct datum 
{
  double x;
  double y;
};


static const struct datum data1[]=
  {
    { -8.0, 29 },
    { -3.7, 45 },
    { -3.3, 67 },
    { -0.8, 89 },
    { -0.2, 93 },
    { 1.0,  100},
    { 2.3,  103},
    { 4.0,  103.4},
    { 5.2,  104},
    { 5.9,  106},
    { 10.3, 106},
    { 13.8, 108},
    { 15.8, 109},
  };




static const struct datum data2[]=
  {
    { -9.1, 20 },
    { -8.2, 17 },
    { -5.0, 19 },
    { -3.7, 25 },
    { -1.6, 49 },
    { -1.3, 61 },
    { -1.1, 81 },
    { 3.5,  91},
    { 5.4,  93},
    { 9.3,  94},
    { 14.3,  92}
  };




struct dataset
{
  const struct datum *data;
  int n_data;
  char *label;
};



#define DATASETS 2

static const struct dataset dataset[DATASETS] = 
  {
    {data1, 13, "male"},
    {data2, 11, "female"},
  };



typedef void (*plot_func) (struct chart *ch,  const struct dataset *dataset);


void plot_line(struct chart *ch,  const struct dataset *dataset);

void plot_scatter(struct chart *ch,  const struct dataset *dataset);



static void
write_legend(struct chart *chart, const char *heading, int n);

void draw_cartesian(struct chart *ch, const char *title, 
		    const char *xlabel, const char *ylabel, plot_func pf);



void
draw_scatterplot(struct chart *ch, const char *title, 
		 const char *xlabel, const char *ylabel)
{
  draw_cartesian(ch, title, xlabel, ylabel, plot_scatter);
}


void
draw_lineplot(struct chart *ch, const char *title, 
		 const char *xlabel, const char *ylabel)
{
  draw_cartesian(ch, title, xlabel, ylabel, plot_scatter);
}


void
draw_cartesian(struct chart *ch, const char *title, 
		 const char *xlabel, const char *ylabel, plot_func pf)
{
  double x;
  double y;
  

  int d;


  const double ordinate_scale = 
    fabs(ch->data_top -  ch->data_bottom) 
    / fabs(y_max - y_min) ;


  const double abscissa_scale =
    fabs(ch->data_right - ch->data_left) 
    / 
    fabs(x_max - x_min);


  /* Move to data bottom-left */
  pl_move_r(ch->lp, ch->data_left, ch->data_bottom);

  pl_savestate_r(ch->lp);


  for(x = x_tick * ceil(x_min / x_tick )  ; 
      x < x_max;    
      x += x_tick )
      draw_tick (ch, TICK_ABSCISSA, (x - x_min) * abscissa_scale, "%g", x);

  for(y = y_tick * ceil(y_min / y_tick )  ; 
      y < y_max;    
      y += y_tick )
      draw_tick (ch, TICK_ORDINATE, (y - y_min) * ordinate_scale, "%g", y);

  pl_savestate_r(ch->lp);

  for (d = 0 ; d < DATASETS ; ++d ) 
    {
      pl_pencolorname_r(ch->lp,data_colour[d]);
      pf(ch, &dataset[d]);
    }
  
  pl_restorestate_r(ch->lp);

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

  write_legend(ch,"Key:",DATASETS);

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



void
plot_line(struct chart *ch,  const struct dataset *dataset)
{
  int i;

  const struct datum *data = dataset->data;

  const double ordinate_scale = 
    fabs(ch->data_top -  ch->data_bottom) 
    / fabs(y_max - y_min) ;


  const double abscissa_scale =
    fabs(ch->data_right - ch->data_left) 
    / 
    fabs(x_max - x_min);


  for( i = 0 ; i < dataset->n_data ; ++i ) 
    {
      const double x = 
	(data[i].x - x_min) * abscissa_scale + ch->data_left ; 
      const double y = 
	(data[i].y - y_min) * ordinate_scale + ch->data_bottom;

      if (i == 0 ) 
	pl_move_r(ch->lp, x, y );
      else
	pl_fcont_r(ch->lp, x, y);
    }
  pl_endpath_r(ch->lp);

}




void
plot_scatter(struct chart *ch,  const struct dataset *dataset)
{
  int i;

  const struct datum *data = dataset->data;

  const double ordinate_scale = 
    fabs(ch->data_top -  ch->data_bottom) 
    / fabs(y_max - y_min) ;


  const double abscissa_scale =
    fabs(ch->data_right - ch->data_left) 
    / 
    fabs(x_max - x_min);


  for( i = 0 ; i < dataset->n_data ; ++i ) 
    {
      const double x = 
	(data[i].x - x_min) * abscissa_scale + ch->data_left ; 
      const double y = 
	(data[i].y - y_min) * ordinate_scale + ch->data_bottom;

      pl_fmarker_r(ch->lp, x, y, 6, 15);
    }
  
}
