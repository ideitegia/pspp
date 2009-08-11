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

#include <cairo/cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <output/chart.h>

struct chart_colour
  {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
  };

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

    /* Legend. */
    int legend_left ;
    int legend_right ;
    const char **dataset;
    int n_datasets;

    /* Default font size for the plot. */
    double font_size;

    struct chart_colour fill_colour;

    /* Stuff Particular to Cartesians (and Boxplots ) */
    double ordinate_scale;
    double abscissa_scale;
    double x_min;
    double x_max;
    double y_min;
    double y_max;
    bool in_path;
  };

struct chart_class
  {
    void (*draw) (const struct chart *, cairo_t *, struct chart_geometry *);
    void (*destroy) (struct chart *);
  };

struct chart
  {
    const struct chart_class *class;
    int ref_cnt;
  };

void chart_init (struct chart *, const struct chart_class *);

void chart_geometry_init (cairo_t *, struct chart_geometry *,
                          double width, double length);
void chart_geometry_free (cairo_t *, struct chart_geometry *);

void chart_draw (const struct chart *, cairo_t *, struct chart_geometry *);
char *chart_draw_png (const struct chart *, const char *file_name_template,
                      int number);

#endif /* output/chart-provider.h */
