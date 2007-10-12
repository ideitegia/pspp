/* PSPP - a program for statistical analysis.
   Copyright (C) 2004 Free Software Foundation, Inc.

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
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <float.h>
#include <assert.h>
#include <math.h>

#include <math/chart-geometry.h>
#include <libpspp/str.h>
#include "manager.h"
#include "output.h"

#include "xalloc.h"

#ifndef CHART_H
#define CHART_H

#ifndef NO_CHARTS
#include <plot.h>
#endif

struct chart {

#ifndef NO_CHARTS
  plPlotter *lp ;
  plPlotterParams *pl_params;
#else
  void *lp;
#endif
  char *file_name;
  FILE *file;

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

  /* Stuff Particular to Cartesians (and Boxplots ) */
  double ordinate_scale;
  double abscissa_scale;
  double x_min;
  double x_max;
  double y_min;
  double y_max;
};



struct chart * chart_create(void);
void chart_submit(struct chart *ch);

/* Helper functions for output drivers that put each chart into a
   separate file. */
void chart_init_separate (struct chart *, const char *type,
                          const char *file_name_tmpl, int number);

void chart_finalise_separate (struct chart *);

#endif
