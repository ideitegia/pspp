/* PSPP - a program for statistical analysis.
   Copyright (C) 2009 Free Software Foundation, Inc.

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

#include <libpspp/assertion.h>
#include <libpspp/start-date.h>
#include <libpspp/version.h>
#include <output/afm.h>
#include <output/chart.h>
#include <output/manager.h>
#include <output/output.h>

#include <cairo/cairo-pdf.h>
#include <cairo/cairo-ps.h>
#include <cairo/cairo-svg.h>
#include <cairo/cairo.h>
#include <pango/pango-font.h>
#include <pango/pango-layout.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <stdlib.h>

#include "error.h"
#include "intprops.h"
#include "minmax.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Cairo driver options: (defaults listed first)

   output-file="pspp.pdf"
   output-type=pdf|ps|png|svg
   paper-size=letter (see "papersize" file)
   orientation=portrait|landscape
   headers=on|off

   left-margin=0.5in
   right-margin=0.5in
   top-margin=0.5in
   bottom-margin=0.5in

   prop-font=Times-Roman
   emph-font=Times-Italic
   fixed-font=Courier
   font-size=10000

   line-gutter=1pt
   line-spacing=1pt
   line-width=0.5pt
 */

/* Measurements.  We use the same scale as Pango, for simplicity. */
#define XR_POINT PANGO_SCALE
#define XR_INCH (XR_POINT * 72)

/* Output types. */
enum xr_output_type
  {
    XR_PDF,
    XR_PS,
    XR_SVG
  };

/* A font for use with Cairo. */
struct xr_font
  {
    char *string;
    PangoFontDescription *desc;
    PangoLayout *layout;
    PangoFontMetrics *metrics;
  };

/* Cairo output driver extension record. */
struct xr_driver_ext
  {
    char *file_name;            /* Output file name. */
    enum xr_output_type file_type; /* Type of output file. */
    cairo_t *cairo;

    bool draw_headers;          /* Draw headers at top of page? */
    int page_number;		/* Current page number. */

    bool portrait;              /* Portrait mode? */
    int paper_width;            /* Width of paper before dropping margins. */
    int paper_length;           /* Length of paper before dropping margins. */
    int left_margin;		/* Left margin in XR units. */
    int right_margin;		/* Right margin in XR units. */
    int top_margin;		/* Top margin in XR units. */
    int bottom_margin;		/* Bottom margin in XR units. */

    int line_gutter;		/* Space around lines. */
    int line_space;		/* Space between lines. */
    int line_width;		/* Width of lines. */

    struct xr_font fonts[OUTP_FONT_CNT];
  };

static bool handle_option (struct outp_driver *this, const char *key,
                           const struct string *val);
static void draw_headers (struct outp_driver *this);

static bool load_font (struct outp_driver *this, struct xr_font *);
static void free_font (struct xr_font *);
static int text_width (struct outp_driver *, const char *, enum outp_font);

/* Driver initialization. */

static bool
xr_open_driver (struct outp_driver *this, struct substring options)
{
  cairo_surface_t *surface;
  cairo_status_t status;
  struct xr_driver_ext *x;
  double width_pt, length_pt;
  size_t i;

  this->width = this->length = 0;
  this->font_height = XR_POINT * 10;

  this->ext = x = xmalloc (sizeof *x);
  x->file_name = xstrdup ("pspp.pdf");
  x->file_type = XR_PDF;
  x->draw_headers = true;
  x->page_number = 0;
  x->portrait = true;
  outp_get_paper_size ("", &x->paper_width, &x->paper_length);
  x->left_margin = XR_INCH / 2;
  x->right_margin = XR_INCH / 2;
  x->top_margin = XR_INCH / 2;
  x->bottom_margin = XR_INCH / 2;
  x->line_gutter = XR_POINT;
  x->line_space = XR_POINT;
  x->line_width = XR_POINT / 2;
  x->fonts[OUTP_FIXED].string = xstrdup ("monospace");
  x->fonts[OUTP_PROPORTIONAL].string = xstrdup ("serif");
  x->fonts[OUTP_EMPHASIS].string = xstrdup ("serif italic");
  for (i = 0; i < OUTP_FONT_CNT; i++)
    {
      struct xr_font *font = &x->fonts[i];
      font->desc = NULL;
      font->metrics = NULL;
      font->layout = NULL;
    }

  outp_parse_options (options, handle_option, this);

  if (x->portrait)
    {
      this->width = x->paper_width;
      this->length = x->paper_length;
    }
  else
    {
      this->width = x->paper_length;
      this->length = x->paper_width;
    }
  if (x->draw_headers)
    x->top_margin += 3 * this->font_height;
  this->width -= x->left_margin + x->right_margin;
  this->length -= x->top_margin + x->bottom_margin;

  width_pt = x->paper_width / (double) XR_POINT;
  length_pt = x->paper_length / (double) XR_POINT;
  if (x->file_type == XR_PDF)
    surface = cairo_pdf_surface_create (x->file_name, width_pt, length_pt);
  else if (x->file_type == XR_PS)
    surface = cairo_ps_surface_create (x->file_name, width_pt, length_pt);
  else if (x->file_type == XR_SVG)
    surface = cairo_svg_surface_create (x->file_name, width_pt, length_pt);
  else
    NOT_REACHED ();

  status = cairo_surface_status (surface);
  if (status != CAIRO_STATUS_SUCCESS)
    {
      error (0, 0, _("opening output file \"%s\": %s"),
             x->file_name, cairo_status_to_string (status));
      cairo_surface_destroy (surface);
      goto error;
    }

  x->cairo = cairo_create (surface);
  cairo_surface_destroy (surface);

  cairo_scale (x->cairo, 1.0 / PANGO_SCALE, 1.0 / PANGO_SCALE);
  cairo_translate (x->cairo, x->left_margin, x->top_margin);
  cairo_set_line_width (x->cairo, x->line_width);

  for (i = 0; i < OUTP_FONT_CNT; i++)
    if (!load_font (this, &x->fonts[i]))
      goto error;

  if (this->length / this->font_height < 15)
    {
      error (0, 0, _("The defined page is not long "
                     "enough to hold margins and headers, plus least 15 "
                     "lines of the default fonts.  In fact, there's only "
                     "room for %d lines."),
             this->length / this->font_height);
      goto error;
    }

  this->fixed_width = text_width (this, "0", OUTP_FIXED);
  this->prop_em_width = text_width (this, "0", OUTP_PROPORTIONAL);

  this->horiz_line_width[OUTP_L_NONE] = 0;
  this->horiz_line_width[OUTP_L_SINGLE] = 2 * x->line_gutter + x->line_width;
  this->horiz_line_width[OUTP_L_DOUBLE] = (2 * x->line_gutter + x->line_space
                                           + 2 * x->line_width);
  memcpy (this->vert_line_width, this->horiz_line_width,
          sizeof this->vert_line_width);

  return true;

 error:
  this->class->close_driver (this);
  return false;
}

static bool
xr_close_driver (struct outp_driver *this)
{
  struct xr_driver_ext *x = this->ext;
  bool ok = true;
  size_t i;

  if (x->cairo != NULL)
    {
      cairo_status_t status;

      cairo_surface_finish (cairo_get_target (x->cairo));
      status = cairo_status (x->cairo);
      if (status != CAIRO_STATUS_SUCCESS)
        error (0, 0, _("writing output file \"%s\": %s"),
               x->file_name, cairo_status_to_string (status));
      cairo_destroy (x->cairo);
    }

  free (x->file_name);
  for (i = 0; i < OUTP_FONT_CNT; i++)
    free_font (&x->fonts[i]);
  free (x);

  return ok;
}

/* Generic option types. */
enum
{
  output_file_arg,
  output_type_arg,
  paper_size_arg,
  orientation_arg,
  line_style_arg,
  boolean_arg,
  dimension_arg,
  string_arg,
  nonneg_int_arg
};

/* All the options that the Cairo driver supports. */
static const struct outp_option option_tab[] =
{
  {"output-file",		output_file_arg,0},
  {"output-type",               output_type_arg,0},
  {"paper-size",		paper_size_arg, 0},
  {"orientation",		orientation_arg,0},

  {"headers",			boolean_arg,	1},

  {"prop-font", 		string_arg,	OUTP_PROPORTIONAL},
  {"emph-font", 		string_arg,	OUTP_EMPHASIS},
  {"fixed-font",		string_arg,	OUTP_FIXED},

  {"left-margin",		dimension_arg,	0},
  {"right-margin",		dimension_arg,	1},
  {"top-margin",		dimension_arg,	2},
  {"bottom-margin",		dimension_arg,	3},
  {"font-size",			dimension_arg,	4},
  {"line-width",		dimension_arg,	5},
  {"line-gutter",		dimension_arg,	6},
  {"line-width",		dimension_arg,	7},
  {NULL, 0, 0},
};

static bool
handle_option (struct outp_driver *this, const char *key,
               const struct string *val)
{
  struct xr_driver_ext *x = this->ext;
  int subcat;
  char *value = ds_cstr (val);

  switch (outp_match_keyword (key, option_tab, &subcat))
    {
    case -1:
      error (0, 0,
             _("unknown configuration parameter `%s' for Cairo device "
               "driver"), key);
      break;
    case output_file_arg:
      free (x->file_name);
      x->file_name = xstrdup (value);
      break;
    case output_type_arg:
      if (!strcmp (value, "pdf"))
        x->file_type = XR_PDF;
      else if (!strcmp (value, "ps"))
        x->file_type = XR_PS;
      else if (!strcmp (value, "svg"))
        x->file_type = XR_SVG;
      else
        {
          error (0, 0, _("unknown Cairo output type \"%s\""), value);
          return false;
        }
      break;
    case paper_size_arg:
      outp_get_paper_size (value, &x->paper_width, &x->paper_length);
      break;
    case orientation_arg:
      if (!strcmp (value, "portrait"))
	x->portrait = true;
      else if (!strcmp (value, "landscape"))
	x->portrait = false;
      else
	error (0, 0, _("unknown orientation `%s' (valid orientations are "
                       "`portrait' and `landscape')"), value);
      break;
    case boolean_arg:
      if (!strcmp (value, "on") || !strcmp (value, "true")
          || !strcmp (value, "yes") || atoi (value))
        x->draw_headers = true;
      else if (!strcmp (value, "off") || !strcmp (value, "false")
               || !strcmp (value, "no") || !strcmp (value, "0"))
        x->draw_headers = false;
      else
        {
          error (0, 0, _("boolean value expected for %s"), key);
          return false;
        }
      break;
    case dimension_arg:
      {
	int dimension = outp_evaluate_dimension (value);

	if (dimension <= 0)
          break;
	switch (subcat)
	  {
	  case 0:
	    x->left_margin = dimension;
	    break;
	  case 1:
	    x->right_margin = dimension;
	    break;
	  case 2:
	    x->top_margin = dimension;
	    break;
	  case 3:
	    x->bottom_margin = dimension;
	    break;
	  case 4:
	    this->font_height = dimension;
	    break;
	  case 5:
	    x->line_width = dimension;
	    break;
	  case 6:
	    x->line_gutter = dimension;
	    break;
	  case 7:
	    x->line_width = dimension;
	    break;
	  default:
	    NOT_REACHED ();
	  }
      }
      break;
    case string_arg:
      free (x->fonts[subcat].string);
      x->fonts[subcat].string = ds_xstrdup (val);
      break;
    default:
      NOT_REACHED ();
    }

  return true;
}

/* Basic file operations. */

static void
xr_open_page (struct outp_driver *this)
{
  struct xr_driver_ext *x = this->ext;

  x->page_number++;

  if (x->draw_headers)
    draw_headers (this);
}

static void
xr_close_page (struct outp_driver *this)
{
  struct xr_driver_ext *x = this->ext;
  cairo_show_page (x->cairo);
}

static void
xr_submit (struct outp_driver *this UNUSED, struct som_entity *s)
{
  switch (s->type)
    {
    case SOM_CHART:
      break;
    default:
      NOT_REACHED ();
    }
}

/* Draws a line from (x0,y0) to (x1,y1). */
static void
dump_line (struct outp_driver *this, int x0, int y0, int x1, int y1)
{
  struct xr_driver_ext *x = this->ext;
  cairo_new_path (x->cairo);
  cairo_move_to (x->cairo, x0, y0);
  cairo_line_to (x->cairo, x1, y1);
  cairo_stroke (x->cairo);
}

/* Draws a horizontal line X0...X2 at Y if LEFT says so,
   shortening it to X0...X1 if SHORTEN is true.
   Draws a horizontal line X1...X3 at Y if RIGHT says so,
   shortening it to X2...X3 if SHORTEN is true. */
static void
horz_line (struct outp_driver *this,
           int x0, int x1, int x2, int x3, int y,
           enum outp_line_style left, enum outp_line_style right,
           bool shorten)
{
  if (left != OUTP_L_NONE && right != OUTP_L_NONE && !shorten)
    dump_line (this, x0, y, x3, y);
  else
    {
      if (left != OUTP_L_NONE)
        dump_line (this, x0, y, shorten ? x1 : x2, y);
      if (right != OUTP_L_NONE)
        dump_line (this, shorten ? x2 : x1, y, x3, y);
    }
}

/* Draws a vertical line Y0...Y2 at X if TOP says so,
   shortening it to Y0...Y1 if SHORTEN is true.
   Draws a vertical line Y1...Y3 at X if BOTTOM says so,
   shortening it to Y2...Y3 if SHORTEN is true. */
static void
vert_line (struct outp_driver *this,
           int y0, int y1, int y2, int y3, int x,
           enum outp_line_style top, enum outp_line_style bottom,
           bool shorten)
{
  if (top != OUTP_L_NONE && bottom != OUTP_L_NONE && !shorten)
    dump_line (this, x, y0, x, y3);
  else
    {
      if (top != OUTP_L_NONE)
        dump_line (this, x, y0, x, shorten ? y1 : y2);
      if (bottom != OUTP_L_NONE)
        dump_line (this, x, shorten ? y2 : y1, x, y3);
    }
}

/* Draws a generalized intersection of lines in the rectangle
   (X0,Y0)-(X3,Y3).  The line coming from the top to the center
   is of style TOP, from left to center of style LEFT, from
   bottom to center of style BOTTOM, and from right to center of
   style RIGHT. */
static void
xr_line (struct outp_driver *this,
         int x0, int y0, int x3, int y3,
         enum outp_line_style top, enum outp_line_style left,
         enum outp_line_style bottom, enum outp_line_style right)
{
  /* The algorithm here is somewhat subtle, to allow it to handle
     all the kinds of intersections that we need.

     Three additional ordinates are assigned along the x axis.  The
     first is xc, midway between x0 and x3.  The others are x1 and
     x2; for a single vertical line these are equal to xc, and for
     a double vertical line they are the ordinates of the left and
     right half of the double line.

     yc, y1, and y2 are assigned similarly along the y axis.

     The following diagram shows the coordinate system and output
     for double top and bottom lines, single left line, and no
     right line:

                 x0       x1 xc  x2      x3
               y0 ________________________
                  |        #     #       |
                  |        #     #       |
                  |        #     #       |
                  |        #     #       |
                  |        #     #       |
     y1 = y2 = yc |#########     #       |
                  |        #     #       |
                  |        #     #       |
                  |        #     #       |
                  |        #     #       |
               y3 |________#_____#_______|
  */
  struct xr_driver_ext *ext = this->ext;

  /* Offset from center of each line in a pair of double lines. */
  int double_line_ofs = (ext->line_space + ext->line_width) / 2;

  /* Are the lines along each axis single or double?
     (It doesn't make sense to have different kinds of line on the
     same axis, so we don't try to gracefully handle that case.) */
  bool double_vert = top == OUTP_L_DOUBLE || bottom == OUTP_L_DOUBLE;
  bool double_horz = left == OUTP_L_DOUBLE || right == OUTP_L_DOUBLE;

  /* When horizontal lines are doubled,
     the left-side line along y1 normally runs from x0 to x2,
     and the right-side line along y1 from x3 to x1.
     If the top-side line is also doubled, we shorten the y1 lines,
     so that the left-side line runs only to x1,
     and the right-side line only to x2.
     Otherwise, the horizontal line at y = y1 below would cut off
     the intersection, which looks ugly:
               x0       x1     x2      x3
             y0 ________________________
                |        #     #       |
                |        #     #       |
                |        #     #       |
                |        #     #       |
             y1 |#########     ########|
                |                      |
                |                      |
             y2 |######################|
                |                      |
                |                      |
             y3 |______________________|
     It is more of a judgment call when the horizontal line is
     single.  We actually choose to cut off the line anyhow, as
     shown in the first diagram above.
  */
  bool shorten_y1_lines = top == OUTP_L_DOUBLE;
  bool shorten_y2_lines = bottom == OUTP_L_DOUBLE;
  bool shorten_yc_line = shorten_y1_lines && shorten_y2_lines;
  int horz_line_ofs = double_vert ? double_line_ofs : 0;
  int xc = (x0 + x3) / 2;
  int x1 = xc - horz_line_ofs;
  int x2 = xc + horz_line_ofs;

  bool shorten_x1_lines = left == OUTP_L_DOUBLE;
  bool shorten_x2_lines = right == OUTP_L_DOUBLE;
  bool shorten_xc_line = shorten_x1_lines && shorten_x2_lines;
  int vert_line_ofs = double_horz ? double_line_ofs : 0;
  int yc = (y0 + y3) / 2;
  int y1 = yc - vert_line_ofs;
  int y2 = yc + vert_line_ofs;

  if (!double_horz)
    horz_line (this, x0, x1, x2, x3, yc, left, right, shorten_yc_line);
  else
    {
      horz_line (this, x0, x1, x2, x3, y1, left, right, shorten_y1_lines);
      horz_line (this, x0, x1, x2, x3, y2, left, right, shorten_y2_lines);
    }

  if (!double_vert)
    vert_line (this, y0, y1, y2, y3, xc, top, bottom, shorten_xc_line);
  else
    {
      vert_line (this, y0, y1, y2, y3, x1, top, bottom, shorten_x1_lines);
      vert_line (this, y0, y1, y2, y3, x2, top, bottom, shorten_x2_lines);
    }
}

/* Writes STRING at location (X,Y) trimmed to the given MAX_WIDTH
   and with the given JUSTIFICATION for THIS driver. */
static int
draw_text (struct outp_driver *this,
           const char *string, int x, int y, int max_width,
           enum outp_justification justification)
{
  struct outp_text text;
  int width;

  text.font = OUTP_PROPORTIONAL;
  text.justification = justification;
  text.string = ss_cstr (string);
  text.h = max_width;
  text.v = this->font_height;
  text.x = x;
  text.y = y;
  this->class->text_metrics (this, &text, &width, NULL);
  this->class->text_draw (this, &text);
  return width;
}

/* Writes STRING at location (X,Y) trimmed to the given MAX_WIDTH
   and with the given JUSTIFICATION for THIS driver. */
static int
text_width (struct outp_driver *this, const char *string, enum outp_font font)
{
  struct outp_text text;
  int width;

  text.font = font;
  text.justification = OUTP_LEFT;
  text.string = ss_cstr (string);
  text.h = INT_MAX;
  text.v = this->font_height;
  text.x = 0;
  text.y = 0;
  this->class->text_metrics (this, &text, &width, NULL);
  return width;
}

/* Writes LEFT left-justified and RIGHT right-justified within
   (X0...X1) at Y.  LEFT or RIGHT or both may be null. */
static void
draw_header_line (struct outp_driver *this,
                  const char *left, const char *right,
                  int x0, int x1, int y)
{
  int right_width = 0;
  if (right != NULL)
    right_width = (draw_text (this, right, x0, y, x1 - x0, OUTP_RIGHT)
                   + this->prop_em_width);
  if (left != NULL)
    draw_text (this, left, x0, y, x1 - x0 - right_width, OUTP_LEFT);
}

/* Draw top of page headers for THIS driver. */
static void
draw_headers (struct outp_driver *this)
{
  struct xr_driver_ext *ext = this->ext;
  char *r1, *r2;
  int x0, x1;
  int y;

  y = -3 * this->font_height;
  x0 = this->prop_em_width;
  x1 = this->width - this->prop_em_width;

  /* Draw box. */
  cairo_rectangle (ext->cairo, 0, y, this->width,
                   2 * (this->font_height
                        + ext->line_width + ext->line_gutter));
  cairo_save (ext->cairo);
  cairo_set_source_rgb (ext->cairo, 0.9, 0.9, 0.9);
  cairo_fill_preserve (ext->cairo);
  cairo_restore (ext->cairo);
  cairo_stroke (ext->cairo);

  y += ext->line_width + ext->line_gutter;

  r1 = xasprintf (_("%s - Page %d"), get_start_date (), ext->page_number);
  r2 = xasprintf ("%s - %s", version, host_system);

  draw_header_line (this, outp_title, r1, x0, x1, y);
  y += this->font_height;

  draw_header_line (this, outp_subtitle, r2, x0, x1, y);

  free (r1);
  free (r2);
}

/* Format TEXT on THIS driver.
   If DRAW is nonzero, draw the text.
   The width of the widest line is stored into *WIDTH, if WIDTH
   is nonnull.
   The total height of the text written is stored into *HEIGHT,
   if HEIGHT is nonnull. */
static void
text (struct outp_driver *this, const struct outp_text *text, bool draw,
      int *width, int *height)
{
  struct xr_driver_ext *ext = this->ext;
  struct xr_font *font = &ext->fonts[text->font];

  pango_layout_set_text (font->layout,
                         text->string.string, text->string.length);
  pango_layout_set_alignment (
    font->layout,
    (text->justification == OUTP_RIGHT ? PANGO_ALIGN_RIGHT
     : text->justification == OUTP_LEFT ? PANGO_ALIGN_LEFT
     : PANGO_ALIGN_CENTER));
  pango_layout_set_width (font->layout, text->h == INT_MAX ? -1 : text->h);
  pango_layout_set_wrap (font->layout, PANGO_WRAP_WORD_CHAR);
  /* XXX need to limit number of lines to those that fit in text->v. */

  if (draw)
    {
      int x = text->x;
      if (text->justification != OUTP_LEFT && text->h != INT_MAX)
        {
          int w, h, excess;
          pango_layout_get_size (font->layout, &w, &h);
          excess = text->h - w;
          if (excess > 0)
            {
              if (text->justification == OUTP_CENTER)
                x += excess / 2;
              else
                x += excess;
            }
        }
      cairo_save (ext->cairo);
      cairo_translate (ext->cairo, text->x, text->y);
      cairo_scale (ext->cairo, PANGO_SCALE, PANGO_SCALE);
      pango_cairo_show_layout (ext->cairo, font->layout);
      cairo_restore (ext->cairo);
      pango_cairo_update_layout (ext->cairo, font->layout);
    }

  if (width != NULL || height != NULL)
    {
      int w, h;
      pango_layout_get_size (font->layout, &w, &h);
      if (width != NULL)
        *width = w;
      if (height != NULL)
        *height = h;
    }
}

static void
xr_text_metrics (struct outp_driver *this, const struct outp_text *t,
                 int *width, int *height)
{
  text (this, t, false, width, height);
}

static void
xr_text_draw (struct outp_driver *this, const struct outp_text *t)
{
  assert (this->page_open);
  text (this, t, true, NULL, NULL);
}

static void
xr_chart_initialise (struct outp_driver *this UNUSED, struct chart *ch UNUSED)
{
#ifdef NO_CHARTS
  ch->lp = NULL;
#else
  /* XXX libplot doesn't support Cairo yet. */
#endif
}

static void
xr_chart_finalise (struct outp_driver *this UNUSED, struct chart *ch UNUSED)
{
#ifndef NO_CHARTS
  /* XXX libplot doesn't support Cairo yet. */
#endif
}

/* Attempts to load FONT, initializing its other members based on
   its 'string' member and the information in THIS.  Returns true
   if successful, otherwise false. */
static bool
load_font (struct outp_driver *this, struct xr_font *font)
{
  struct xr_driver_ext *x = this->ext;
  PangoContext *context;
  PangoLanguage *language;

  font->desc = pango_font_description_from_string (font->string);
  if (font->desc == NULL)
    {
      error (0, 0, _("\"%s\": bad font specification"), font->string);
      return false;
    }
  pango_font_description_set_absolute_size (font->desc, this->font_height);

  font->layout = pango_cairo_create_layout (x->cairo);
      pango_cairo_update_layout (x->cairo, font->layout);
  pango_layout_set_font_description (font->layout, font->desc);

  language = pango_language_get_default ();
  context = pango_layout_get_context (font->layout);
  font->metrics = pango_context_get_metrics (context, font->desc, language);

  return true;
}

/* Frees FONT. */
static void
free_font (struct xr_font *font)
{
  free (font->string);
  if (font->desc != NULL)
    pango_font_description_free (font->desc);
  pango_font_metrics_unref (font->metrics);
  g_object_unref (font->layout);
}

/* Cairo driver class. */
const struct outp_class cairo_class =
{
  "cairo",
  0,

  xr_open_driver,
  xr_close_driver,

  xr_open_page,
  xr_close_page,
  NULL,

  xr_submit,

  xr_line,
  xr_text_metrics,
  xr_text_draw,

  xr_chart_initialise,
  xr_chart_finalise
};
