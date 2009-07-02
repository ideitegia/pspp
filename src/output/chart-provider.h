/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2009 Free Software Foundation, Inc.

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

#ifndef OUTPUT_CHART_PROVIDER_H
#define OUTPUT_CHART_PROVIDER_H 1

#include <stdbool.h>
#include <output/chart.h>

struct chart_class
  {
    void (*draw) (const struct chart *, plPlotter *);
    void (*destroy) (struct chart *);
  };

struct chart
  {
    const struct chart_class *class;
    int ref_cnt;
  };

void chart_init (struct chart *, const struct chart_class *);
bool chart_create_file (const char *type, const char *file_name_tmpl,
                        int number, plPlotterParams *,
                        char **file_namep, plPlotter **lpp);

/* The geometry of a chart. */
struct chart_geometry
  {
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

void chart_geometry_init (plPlotter *, struct chart_geometry *);
void chart_geometry_free (plPlotter *);

#endif /* output/chart-provider.h */
