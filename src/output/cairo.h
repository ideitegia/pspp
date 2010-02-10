/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010 Free Software Foundation, Inc.

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

#ifndef OUTPUT_CAIRO_H
#define OUTPUT_CAIRO_H 1

#ifdef HAVE_CAIRO

#include <cairo/cairo.h>

struct chart_item;
struct output_item;

/* Used by PSPPIRE to render in the GUI. */
struct xr_driver *xr_create_driver (cairo_t *);
struct xr_rendering *xr_rendering_create (struct xr_driver *,
                                          const struct output_item *,
                                          cairo_t *);
void xr_rendering_measure (struct xr_rendering *, int *w, int *h);
void xr_rendering_draw (struct xr_rendering *, cairo_t *);

/* Render charts with Cairo. */
void xr_draw_chart (const struct chart_item *, cairo_t *,
                    double x, double y, double width, double height);
char *xr_draw_png_chart (const struct chart_item *,
                         const char *file_name_template, int number);

#endif  /* HAVE_CAIRO */

#endif /* output/cairo.h */
