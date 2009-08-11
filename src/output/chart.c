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

#include <config.h>

#include <output/chart.h>
#include <output/chart-provider.h>

#include <assert.h>
#include <cairo/cairo.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libpspp/str.h>
#include <output/manager.h>
#include <output/output.h>

#include "error.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

extern struct som_table_class tab_table_class;

void
chart_init (struct chart *chart, const struct chart_class *class)
{
  chart->class = class;
  chart->ref_cnt = 1;
}

void
chart_geometry_init (cairo_t *cr, struct chart_geometry *geom,
                     double width, double length)
{
  /* Set default chartetry. */
  geom->data_top = 0.900 * length;
  geom->data_right = 0.800 * width;
  geom->data_bottom = 0.120 * length;
  geom->data_left = 0.150 * width;
  geom->abscissa_top = 0.070 * length;
  geom->ordinate_right = 0.120 * width;
  geom->title_bottom = 0.920 * length;
  geom->legend_left = 0.810 * width;
  geom->legend_right = width;
  geom->font_size = 15.0;
  geom->in_path = false;
  geom->dataset = NULL;
  geom->n_datasets = 0;

  geom->fill_colour.red = 255;
  geom->fill_colour.green = 0;
  geom->fill_colour.blue = 0;

  cairo_set_line_width (cr, 1.0);

  cairo_rectangle (cr, geom->data_left, geom->data_bottom,
                   geom->data_right - geom->data_left,
                   geom->data_top - geom->data_bottom);
  cairo_stroke (cr);
}

void
chart_geometry_free (cairo_t *cr UNUSED, struct chart_geometry *geom)
{
  int i;

  for (i = 0 ; i < geom->n_datasets; ++i)
    free (geom->dataset[i]);
  free (geom->dataset);
}

void
chart_draw (const struct chart *chart, cairo_t *cr,
            struct chart_geometry *geom)
{
  chart->class->draw (chart, cr, geom);
}

char *
chart_draw_png (const struct chart *chart, const char *file_name_template,
                int number)
{
  const int width = 640;
  const int length = 480;

  struct chart_geometry geom;
  cairo_surface_t *surface;
  cairo_status_t status;
  const char *number_pos;
  char *file_name;
  cairo_t *cr;

  number_pos = strchr (file_name_template, '#');
  if (number_pos != NULL)
    file_name = xasprintf ("%.*s%d%s", (int) (number_pos - file_name_template),
                           file_name_template, number, number_pos + 1);
  else
    file_name = xstrdup (file_name_template);

  surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24, width, length);
  cr = cairo_create (surface);

  cairo_translate (cr, 0.0, length);
  cairo_scale (cr, 1.0, -1.0);

  cairo_save (cr);
  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_rectangle (cr, 0, 0, width, length);
  cairo_fill (cr);
  cairo_restore (cr);

  cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);

  chart_geometry_init (cr, &geom, width, length);
  chart_draw (chart, cr, &geom);
  chart_geometry_free (cr, &geom);

  status = cairo_surface_write_to_png (surface, file_name);
  if (status != CAIRO_STATUS_SUCCESS)
    error (0, 0, _("writing output file \"%s\": %s"),
           file_name, cairo_status_to_string (status));

  cairo_destroy (cr);
  cairo_surface_destroy (surface);

  return file_name;
}


struct chart *
chart_ref (const struct chart *chart_)
{
  struct chart *chart = CONST_CAST (struct chart *, chart_);
  chart->ref_cnt++;
  return chart;
}

void
chart_unref (struct chart *chart)
{
  if (chart != NULL)
    {
      assert (chart->ref_cnt > 0);
      if (--chart->ref_cnt == 0)
        chart->class->destroy (chart);
    }
}

void
chart_submit (struct chart *chart)
{
#ifdef HAVE_CAIRO
  struct outp_driver *d;

  for (d = outp_drivers (NULL); d; d = outp_drivers (d))
    if (d->class->output_chart != NULL)
      d->class->output_chart (d, chart);
#endif

  chart_unref (chart);
}
