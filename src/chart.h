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


#ifndef CHART_H
#define CHART_H

#include <stdio.h>
#include <plot.h>
#include <gsl/gsl_histogram.h>

#include "var.h"


/* Array of standard colour names */
extern const char *data_colour[];


struct chart {

  plPlotter *lp ;
  plPlotterParams *pl_params;

  /* The geometry of the chart 
     See diagram at the foot of this file.
   */
  
  int data_top   ;
  int data_right ;
  int data_bottom;
  int data_left  ;

  int abscissa_top;

  int ordinate_right ;

  int title_bottom ;

  int legend_left ;
  int legend_right ;

  
  /* Default font size for the plot (if zero, then use plotter default) */
  int font_size; 

  char fill_colour[10];

  /* Stuff Particular to Cartesians */
  double ordinate_scale;
  double abscissa_scale;
  double x_min;
  double x_max;
  double y_min;
  double y_max;

};


int  chart_initialise(struct chart *ch);

void chart_finalise(struct chart *ch);


double chart_rounded_tick(double tick);

void chart_write_xlabel(struct chart *ch, const char *label);
void chart_write_ylabel(struct chart *ch, const char *label);

void chart_write_title(struct chart *ch, const char *title, ...);

enum tick_orientation {
  TICK_ABSCISSA=0,
  TICK_ORDINATE
};

void draw_tick(struct chart *ch, enum tick_orientation orientation, 
	       double position, const char *label, ...);



enum  bar_opts {
  BAR_GROUPED =  0,
  BAR_STACKED,
  BAR_RANGE
};


void draw_barchart(struct chart *ch, const char *title, 
		   const char *xlabel, const char *ylabel, enum bar_opts opt);

void draw_box_whisker_chart(struct chart *ch, const char *title);



struct normal_curve
{
  double N ;
  double mean ;
  double stddev ;
};


void histogram_write_legend(struct chart *ch, const struct normal_curve *norm);


/* Plot a gsl_histogram */
void histogram_plot(const gsl_histogram *hist, const char *factorname,
		    const struct normal_curve *norm, short show_normal);


/* Create a gsl_histogram and set it's parameters based upon 
   x_min, x_max and bins. 
   The caller is responsible for freeing the histogram.
*/
gsl_histogram * histogram_create(double bins, double x_min, double x_max) ;





struct slice {
  const char *label;
  double magnetude;
};




/* Draw a piechart */
void piechart_plot(const char *title,
		   const struct slice *slices, int n_slices);

void draw_scatterplot(struct chart *ch);


void draw_lineplot(struct chart *ch);


/* Set the scale on chart CH.
   The scale extends from MIN to MAX .
   TICK is the approximate number of tick marks.
*/

void chart_write_xscale(struct chart *ch, 
			double min, double max, int ticks);

void chart_write_yscale(struct chart *ch, 
			double min, double max, int ticks);


void chart_datum(struct chart *ch, int dataset, double x, double y);




enum CHART_DIM
  {
    CHART_DIM_X,
    CHART_DIM_Y
  };


void chart_line(struct chart *ch, double slope, double intercept, 
		double limit1, double limit2, enum CHART_DIM limit_d);


#endif

#if 0
The anatomy of a chart is as follows.

+-------------------------------------------------------------+
|	     +----------------------------------+	      |
|	     |				        |	      |
|	     |		Title		        |	      |
|	     |				        |	      |
|      	     +----------------------------------+	      |
|+----------++----------------------------------++-----------+|
||	    ||				        ||	     ||
||	    ||				        ||	     ||
||	    ||				        ||	     ||
||	    ||				        ||	     ||
||	    ||		      		        ||	     ||
||	    ||		      		        ||	     ||
||	    ||		      		        ||	     ||
||	    ||		      		        ||	     ||
||	    ||		      		        ||	     ||
||	    ||		      		        ||	     ||
|| Ordinate ||		  Data 		        ||  Legend   ||
||	    ||		      		        ||	     ||
||	    ||		      		        ||	     ||
||	    ||		      		        ||	     ||
||	    ||		      		        ||	     ||
||	    ||		      		        ||	     ||
||	    ||		      		        ||	     ||
||	    ||		      		        ||	     ||
||	    ||		      		        ||	     ||
||	    ||		      		        ||	     ||
||	    ||		      		        ||	     ||	      
|+----------++----------------------------------++-----------+|	  --  
|	     +----------------------------------+	      | -  ^  data_bottom
|	     |		Abscissa	        |	      | ^  |  		 
|	     |		      		        |	      | | abscissa_top
|	     +----------------------------------+	      | v  v  
+-------------------------------------------------------------+ ----  
			      			
ordinate_right		      			||	     |
|           |                                   ||	     |
|<--------->|                                   ||	     |
|            |                                  ||	     |
| data_left  |                                  ||	     |
|<---------->|                                  ||	     |
|                                               ||	     |
|               data_right                      ||	     |
|<--------------------------------------------->||	     |
|		   legend_left 	 		 |	     |
|<---------------------------------------------->|	     |
|		     legend_right		 	     |
|<---------------------------------------------------------->|
							     
#endif
