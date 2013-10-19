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

#include <stdbool.h>

#ifdef HAVE_CAIRO

#include <cairo/cairo.h>

struct chart_item;
struct output_item;
struct string_map;

/* Creating and destroying Cairo output drivers. */
struct xr_driver *xr_driver_create (cairo_t *, struct string_map *options);
void xr_driver_destroy (struct xr_driver *);


/* Functions for rendering a single output item to a Cairo context.
   Output items are never broken across multiple pages.
   Used by PSPPIRE to render in the GUI. */
struct xr_rendering *xr_rendering_create (struct xr_driver *,
                                          const struct output_item *,
                                          cairo_t *);

void xr_rendering_apply_options (struct xr_rendering *, struct string_map *o);
void xr_rendering_measure (struct xr_rendering *, int *w, int *h);
void xr_rendering_draw (struct xr_rendering *, cairo_t *,
                        int x, int y, int w, int h);

/* Functions for rendering a series of output items to a series of Cairo
   contexts, with pagination, possibly including headers.

   The intended usage pattern is this:

     * Create an xr_driver with xr_driver_create().  The cairo_t passed in must
       accurately reflect the properties of the output (e.g. for the purpose of
       page size and font selection) but need not be used for rendering.

     * Call xr_driver_next_page() to set up the first real output page's
       cairo_t.  (You can skip this step if the cairo_t passed to
       xr_driver_create() can be used.)

     * Then, for each output_item:

       - Pass the output item to xr_driver_output_item().  As much output as
         fits will be rendered on the current page.

       - Then, as long as xr_driver_need_new_page() returns true, obtain a new
         page for rendering and pass it to xr_driver_next_page().  As much
         output as fits on the new page will be rendered on it.

     * When you're done, destroy the output driver with xr_driver_destroy().

   These functions may also be used for counting pages without actually
   rendering output.  Follow the same steps, except pass NULL as the cairo_t to
   xr_driver_next_page().  (But xr_driver_create() still needs a valid cairo_t
   for page setup.)

   (If the cairo_t that you pass to xr_driver_create() won't remain valid, be
   sure to clear it out one way or another before calling xr_driver_destroy(),
   so that xr_driver_destroy() won't destroy it itself.)
*/
void xr_driver_next_page (struct xr_driver *, cairo_t *);
void xr_driver_output_item (struct xr_driver *, const struct output_item *);
bool xr_driver_need_new_page (const struct xr_driver *);
bool xr_driver_is_page_blank (const struct xr_driver *);

struct xr_color
{
  double red;
  double green;
  double blue;
};

struct output_driver;
struct string_map;

void parse_color (struct output_driver *d, struct string_map *options,
		  const char *key, const char *default_value,
		  struct xr_color *color);


/* Render charts with Cairo. */
char *xr_draw_png_chart (const struct chart_item *,
                         const char *file_name_template, int number,
                         const struct xr_color *fg,
			 const struct xr_color *bg);


#endif  /* HAVE_CAIRO */

#endif /* output/cairo.h */
