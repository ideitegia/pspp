/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2009 Free Software Foundation, Inc.

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

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <data/file-name.h>
#include <libpspp/assertion.h>
#include <libpspp/bit-vector.h>
#include <libpspp/compiler.h>
#include <libpspp/freaderror.h>
#include <libpspp/hash.h>
#include <libpspp/misc.h>
#include <libpspp/start-date.h>
#include <libpspp/version.h>
#include <output/afm.h>
#include <output/chart-provider.h>
#include <output/chart.h>
#include <output/manager.h>
#include <output/output.h>

#include "error.h"
#include "intprops.h"
#include "minmax.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* PostScript driver options: (defaults listed first)

   output-file="pspp.ps"

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

/* The number of `psus' (PostScript driver UnitS) per inch. */
#define PSUS 72000

/* A PostScript font. */
struct font
  {
    struct afm *metrics;        /* Metrics. */
    char *embed_fn;             /* Name of file to embed. */
    char *encoding_fn;          /* Name of file with encoding. */
  };

/* PostScript output driver extension record. */
struct ps_driver_ext
  {
    char *file_name;            /* Output file name. */
    FILE *file;                 /* Output file. */

    bool draw_headers;          /* Draw headers at top of page? */
    int page_number;		/* Current page number. */

    bool portrait;              /* Portrait mode? */
    int paper_width;            /* Width of paper before dropping margins. */
    int paper_length;           /* Length of paper before dropping margins. */
    int left_margin;		/* Left margin in psus. */
    int right_margin;		/* Right margin in psus. */
    int top_margin;		/* Top margin in psus. */
    int bottom_margin;		/* Bottom margin in psus. */

    int line_gutter;		/* Space around lines. */
    int line_space;		/* Space between lines. */
    int line_width;		/* Width of lines. */

    struct font *fonts[OUTP_FONT_CNT];
    int last_font;              /* Index of last font set with setfont. */

    int doc_num;                /* %%DocumentNumber counter. */
  };

/* Transform logical y-ordinate Y into a page ordinate. */
#define YT(Y) (this->length - (Y))

static bool handle_option (void *this, const char *key,
                           const struct string *val);
static void draw_headers (struct outp_driver *this);

static void write_ps_prologue (struct outp_driver *);

static char *quote_ps_name (const char *string);

static struct font *load_font (const char *string);
static void free_font (struct font *);
static void setup_font (struct outp_driver *this, struct font *, int index);

/* Driver initialization. */

static bool
ps_open_driver (const char *name, int types, struct substring options)
{
  struct outp_driver *this;
  struct ps_driver_ext *x;
  size_t i;

  this = outp_allocate_driver (&postscript_class, name, types);
  this->width = this->length = 0;
  this->font_height = PSUS * 10 / 72;

  this->ext = x = xmalloc (sizeof *x);
  x->file_name = xstrdup ("pspp.ps");
  x->file = NULL;
  x->draw_headers = true;
  x->page_number = 0;
  x->portrait = true;
  outp_get_paper_size ("", &x->paper_width, &x->paper_length);
  x->left_margin = PSUS / 2;
  x->right_margin = PSUS / 2;
  x->top_margin = PSUS / 2;
  x->bottom_margin = PSUS / 2;
  x->line_gutter = PSUS / 72;
  x->line_space = PSUS / 72;
  x->line_width = PSUS / 144;
  for (i = 0; i < OUTP_FONT_CNT; i++)
    x->fonts[i] = NULL;
  x->doc_num = 0;

  outp_parse_options (this->name, options, handle_option, this);

  x->file = fn_open (x->file_name, "w");
  if (x->file == NULL)
    {
      error (0, errno, _("opening PostScript output file \"%s\""),
             x->file_name);
      goto error;
    }

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
  this->width -= x->left_margin + x->right_margin;
  this->length -= x->top_margin + x->bottom_margin;
  if (x->draw_headers)
    {
      int header_length = 3 * this->font_height;
      this->length -= header_length;
      x->top_margin += header_length;
    }

  for (i = 0; i < OUTP_FONT_CNT; i++)
    if (x->fonts[i] == NULL)
      {
        const char *default_fonts[OUTP_FONT_CNT];
        default_fonts[OUTP_FIXED] = "Courier.afm";
        default_fonts[OUTP_PROPORTIONAL] = "Times-Roman.afm";
        default_fonts[OUTP_EMPHASIS] = "Times-Italic.afm";
        x->fonts[i] = load_font (default_fonts[i]);
        if (x->fonts[i] == NULL)
          goto error;
      }

  if (this->length / this->font_height < 15)
    {
      error (0, 0, _("The defined PostScript page is not long "
                     "enough to hold margins and headers, plus least 15 "
                     "lines of the default fonts.  In fact, there's only "
                     "room for %d lines of each font at the default size "
                     "of %d.%03d points."),
	   this->length / this->font_height,
	   this->font_height / 1000, this->font_height % 1000);
      goto error;
    }

  this->fixed_width =
    afm_get_character (x->fonts[OUTP_FIXED]->metrics, '0')->width
    * this->font_height / 1000;
  this->prop_em_width =
    afm_get_character (x->fonts[OUTP_PROPORTIONAL]->metrics, '0')->width
    * this->font_height / 1000;

  this->horiz_line_width[OUTP_L_NONE] = 0;
  this->horiz_line_width[OUTP_L_SINGLE] = 2 * x->line_gutter + x->line_width;
  this->horiz_line_width[OUTP_L_DOUBLE] = (2 * x->line_gutter + x->line_space
                                           + 2 * x->line_width);
  memcpy (this->vert_line_width, this->horiz_line_width,
          sizeof this->vert_line_width);

  write_ps_prologue (this);

  outp_register_driver (this);
  return true;

 error:
  this->class->close_driver (this);
  outp_free_driver (this);
  return false;
}

static bool
ps_close_driver (struct outp_driver *this)
{
  struct ps_driver_ext *x = this->ext;
  bool ok = true;
  size_t i;

  if (x->file != NULL)
    {
      fprintf (x->file,
               "%%%%Trailer\n"
               "%%%%Pages: %d\n"
               "%%%%EOF\n",
               x->page_number);

      ok = fn_close (x->file_name, x->file) == 0;
      if (!ok)
        error (0, errno, _("closing PostScript output file \"%s\""),
               x->file_name);
    }

  free (x->file_name);
  for (i = 0; i < OUTP_FONT_CNT; i++)
    free_font (x->fonts[i]);
  free (x);

  return ok;
}

/* Generic option types. */
enum
{
  output_file_arg,
  paper_size_arg,
  orientation_arg,
  line_style_arg,
  boolean_arg,
  pos_int_arg,
  dimension_arg,
  string_arg,
  nonneg_int_arg
};

/* All the options that the PostScript driver supports. */
static const struct outp_option option_tab[] =
{
  {"output-file",		output_file_arg,0},
  {"paper-size",		paper_size_arg, 0},
  {"orientation",		orientation_arg,0},

  {"headers",			boolean_arg,	1},

  {"prop-font", 		string_arg,	OUTP_PROPORTIONAL},
  {"emph-font", 		string_arg,	OUTP_EMPHASIS},
  {"fixed-font",		string_arg,	OUTP_FIXED},

  {"left-margin",		pos_int_arg,	0},
  {"right-margin",		pos_int_arg,	1},
  {"top-margin",		pos_int_arg,	2},
  {"bottom-margin",		pos_int_arg,	3},
  {"font-size",			pos_int_arg,	4},

  {"line-width",		dimension_arg,	0},
  {"line-gutter",		dimension_arg,	1},
  {"line-width",		dimension_arg,	2},
  {NULL, 0, 0},
};

static bool
handle_option (void *this_, const char *key,
               const struct string *val)
{
  struct outp_driver *this = this_;
  struct ps_driver_ext *x = this->ext;
  int subcat;
  char *value = ds_cstr (val);

  switch (outp_match_keyword (key, option_tab, &subcat))
    {
    case -1:
      error (0, 0,
             _("unknown configuration parameter `%s' for PostScript device "
               "driver"), key);
      break;
    case output_file_arg:
      free (x->file_name);
      x->file_name = xstrdup (value);
      break;
    case paper_size_arg:
      outp_get_paper_size (value, &this->width, &this->length);
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
    case pos_int_arg:
      {
	char *tail;
	int arg;

	errno = 0;
	arg = strtol (value, &tail, 0);
	if (arg < 1 || errno == ERANGE || *tail)
	  {
	    error (0, 0, _("positive integer value required for `%s'"), key);
	    break;
	  }
	if ((subcat == 4 || subcat == 5) && arg < 1000)
	  {
	    error (0, 0, _("default font size must be at least 1 point (value "
                           "of 1000 for key `%s')"), key);
	    break;
	  }
	switch (subcat)
	  {
	  case 0:
	    x->left_margin = arg;
	    break;
	  case 1:
	    x->right_margin = arg;
	    break;
	  case 2:
	    x->top_margin = arg;
	    break;
	  case 3:
	    x->bottom_margin = arg;
	    break;
	  case 4:
	    this->font_height = arg;
	    break;
	  default:
	    NOT_REACHED ();
	  }
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
	    x->line_width = dimension;
	    break;
	  case 1:
	    x->line_gutter = dimension;
	    break;
	  case 2:
	    x->line_width = dimension;
	    break;
	  default:
	    NOT_REACHED ();
	  }
      }
      break;
    case string_arg:
      {
        struct font *font = load_font (value);
        if (font != NULL)
          {
            struct font **dst = &x->fonts[subcat];
            if (*dst != NULL)
              free_font (*dst);
            *dst = font;
          }
      }
      break;
    default:
      NOT_REACHED ();
    }

  return true;
}

/* Looks for a PostScript font file or config file in all the
   appropriate places.  Returns the file name on success, NULL on
   failure. */
static char *
find_ps_file (const char *name)
{
  if (fn_is_absolute (name))
    return xstrdup (name);
  else
    {
      char *base_name = xasprintf ("psfonts/%s", name);
      char *file_name = fn_search_path (base_name, config_path);
      free (base_name);
      return file_name;
    }
}

/* Basic file operations. */

/* Writes the PostScript prologue to file F. */
static void
write_ps_prologue (struct outp_driver *this)
{
  struct ps_driver_ext *x = this->ext;
  size_t embedded_cnt, preloaded_cnt;
  size_t i;

  fputs ("%!PS-Adobe-3.0\n", x->file);
  fputs ("%%Pages: (atend)\n", x->file);

  embedded_cnt = preloaded_cnt = 0;
  for (i = 0; i < OUTP_FONT_CNT; i++)
    {
      bool embed = x->fonts[i]->embed_fn != NULL;
      embedded_cnt += embed;
      preloaded_cnt += !embed;
    }
  if (preloaded_cnt > 0)
    {
      fputs ("%%DocumentNeededResources: font", x->file);
      for (i = 0; i < OUTP_FONT_CNT; i++)
        {
          struct font *f = x->fonts[i];
          if (f->embed_fn == NULL)
            fprintf (x->file, " %s", afm_get_findfont_name (f->metrics));
        }
      fputs ("\n", x->file);
    }
  if (embedded_cnt > 0)
    {
      fputs ("%%DocumentSuppliedResources: font", x->file);
      for (i = 0; i < OUTP_FONT_CNT; i++)
        {
          struct font *f = x->fonts[i];
          if (f->embed_fn != NULL)
            fprintf (x->file, " %s", afm_get_findfont_name (f->metrics));
        }
      fputs ("\n", x->file);
    }
  fputs ("%%Copyright: This prologue is public domain.\n", x->file);
  fprintf (x->file, "%%%%Creator: %s\n", version);
  fprintf (x->file, "%%%%DocumentMedia: Plain %g %g 75 white ()\n",
           x->paper_width / (PSUS / 72.0), x->paper_length / (PSUS / 72.0));
  fprintf (x->file, "%%%%Orientation: %s\n",
           x->portrait ? "Portrait" : "Landscape");
  fputs ("%%EndComments\n", x->file);
  fputs ("%%BeginDefaults\n", x->file);
  fputs ("%%PageResources: font", x->file);
  for (i = 0; i < OUTP_FONT_CNT; i++)
    fprintf (x->file, " %s", afm_get_findfont_name (x->fonts[i]->metrics));
  fputs ("\n", x->file);
  fputs ("%%EndDefaults\n", x->file);
  fputs ("%%BeginProlog\n", x->file);
  fputs ("/ED{exch def}bind def\n", x->file);
  fputs ("/L{moveto lineto stroke}bind def\n", x->file);
  fputs ("/D{moveto lineto moveto lineto stroke}bind def\n", x->file);
  fputs ("/S{show}bind def\n", x->file);
  fputs ("/GS{glyphshow}def\n", x->file);
  fputs ("/RF{\n", x->file);
  fputs (" exch dup maxlength 1 add dict begin\n", x->file);
  fputs (" {\n", x->file);
  fputs ("  1 index/FID ne{def}{pop pop}ifelse\n", x->file);
  fputs (" }forall\n", x->file);
  fputs (" /Encoding ED\n", x->file);
  fputs (" currentdict end\n", x->file);
  fputs ("}bind def\n", x->file);
  fputs ("/F{setfont}bind def\n", x->file);
  fputs ("/EP{\n", x->file);
  fputs (" pg restore\n", x->file);
  fputs (" showpage\n", x->file);
  fputs ("}bind def\n", x->file);
  fputs ("/GB{\n", x->file);
  fputs (" /y2 ED/x2 ED/y1 ED/x1 ED\n", x->file);
  fputs (" x1 y1 moveto x2 y1 lineto x2 y2 lineto x1 y2 lineto closepath\n",
         x->file);
  fputs (" gsave 0.9 setgray fill grestore stroke\n", x->file);
  fputs ("}bind def\n", x->file);
  fputs ("/K{0 rmoveto}bind def\n", x->file);
  fputs ("%%EndProlog\n", x->file);
  fputs ("%%BeginSetup\n", x->file);
  for (i = 0; i < OUTP_FONT_CNT; i++)
    setup_font (this, x->fonts[i], i);
  fputs ("%%EndSetup\n", x->file);
}

/* Returns STRING as a Postscript name, which is just '/'
   followed by STRING unless characters need to be quoted.
   The caller must free the string. */
static char *
quote_ps_name (const char *string)
{
  const char *cp;

  for (cp = string; *cp != '\0'; cp++)
    {
      unsigned char c = *cp;
      if (!isalpha (c) && strchr ("^_|!$&:;.,-+", c) == NULL
          && (cp == string || !isdigit (c)))
        {
          struct string out = DS_EMPTY_INITIALIZER;
          ds_put_char (&out, '<');
	  for (cp = string; *cp != '\0'; cp++)
            {
              c = *cp;
              ds_put_format (&out, "%02x", c);
            }
	  ds_put_cstr (&out, ">cvn");
          return ds_cstr (&out);
        }
    }
  return xasprintf ("/%s", string);
}

static void
ps_open_page (struct outp_driver *this)
{
  struct ps_driver_ext *x = this->ext;

  /* Assure page independence. */
  x->last_font = -1;

  x->page_number++;

  fprintf (x->file,
	   "%%%%Page: %d %d\n"
	   "%%%%BeginPageSetup\n"
	   "/pg save def 0.001 dup scale\n",
	   x->page_number, x->page_number);

  if (!x->portrait)
    fprintf (x->file,
	     "%d 0 translate 90 rotate\n",
	     x->paper_width);

  if (x->bottom_margin != 0 || x->left_margin != 0)
    fprintf (x->file,
	     "%d %d translate\n",
	     x->left_margin, x->bottom_margin);

  fprintf (x->file,
	   "/LW %d def %d setlinewidth\n"
	   "%%%%EndPageSetup\n",
	   x->line_width, x->line_width);

  if (x->draw_headers)
    draw_headers (this);
}

static void
ps_close_page (struct outp_driver *this)
{
  struct ps_driver_ext *x = this->ext;
  fputs ("%%PageTrailer\n"
         "EP\n",
         x->file);
}

static void
ps_output_chart (struct outp_driver *this, const struct chart *chart)
{
  struct ps_driver_ext *x = this->ext;
  struct chart_geometry geom;
  plPlotterParams *params;
  int x_origin, y_origin;
  char buf[BUFSIZ];
  char *page_size;
  plPlotter *lp;
  FILE *file;
  int size;

  /* Create temporary file for chart. */
  file = tmpfile ();
  if (file == NULL)
    {
      error (0, errno, _("failed to create temporary file"));
      return;
    }

  /* Create plotter for chart. */
  size = this->width < this->length ? this->width : this->length;
  x_origin = x->left_margin + (size - this->width) / 2;
  y_origin = x->bottom_margin + (size - this->length) / 2;
  page_size = xasprintf ("a,xsize=%.3f,ysize=%.3f,xorigin=%.3f,yorigin=%.3f",
                         (double) size / PSUS, (double) size / PSUS,
                         (double) x_origin / PSUS, (double) y_origin / PSUS);

  params = pl_newplparams ();
  pl_setplparam (params, "PAGESIZE", page_size);
  free (page_size);
  lp = pl_newpl_r ("ps", 0, file, stderr, params);
  pl_deleteplparams (params);

  if (lp == NULL)
    {
      fclose (file);
      return;
    }

  /* Draw chart and free plotter. */
  chart_geometry_init (lp, &geom, 1000.0, 1000.0);
  chart_draw (chart, lp, &geom);
  chart_geometry_free (lp);
  pl_deletepl_r (lp);

  /* Write prologue for chart. */
  outp_eject_page (this);
  fprintf (x->file,
           "/sp save def\n"
           "%d %d translate 1000 dup scale\n"
           "userdict begin\n"
           "/showpage { } def\n"
           "0 setgray 0 setlinecap 1 setlinewidth\n"
           "0 setlinejoin 10 setmiterlimit [ ] 0 setdash newpath clear\n"
           "%%%%BeginDocument: %d\n",
           -x->left_margin, -x->bottom_margin,
           x->doc_num++);

  /* Copy chart into output file. */
  rewind (file);
  while (fwrite (buf, 1, fread (buf, 1, sizeof buf, file), x->file))
    continue;
  fclose (file);

  /* Write epilogue for chart. */
  fputs ("%%EndDocument\n"
         "end\n"
         "sp restore\n",
         x->file);
  outp_close_page (this);
}

static void
ps_submit (struct outp_driver *this UNUSED, struct som_entity *s)
{
  switch (s->type)
    {
    default:
      NOT_REACHED ();
    }
}

/* Draws a line from (x0,y0) to (x1,y1). */
static void
dump_line (struct outp_driver *this, int x0, int y0, int x1, int y1)
{
  struct ps_driver_ext *ext = this->ext;
  fprintf (ext->file, "%d %d %d %d L\n", x0, YT (y0), x1, YT (y1));
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
ps_line (struct outp_driver *this,
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
  struct ps_driver_ext *ext = this->ext;

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
  struct ps_driver_ext *ext = this->ext;
  char *r1, *r2;
  int x0, x1;
  int y;

  y = -3 * this->font_height;
  x0 = this->prop_em_width;
  x1 = this->width - this->prop_em_width;

  /* Draw box. */
  fprintf (ext->file, "%d %d %d %d GB\n",
	   0, YT (y),
           this->width, YT (y + 2 * this->font_height + ext->line_gutter));
  y += ext->line_width + ext->line_gutter;

  r1 = xasprintf (_("%s - Page %d"), get_start_date (), ext->page_number);
  r2 = xasprintf ("%s - %s", version, host_system);

  draw_header_line (this, outp_title, r1, x0, x1, y);
  y += this->font_height;

  draw_header_line (this, outp_subtitle, r2, x0, x1, y);

  free (r1);
  free (r2);
}

/* Writes the CHAR_CNT characters in CHARS at (X0,Y0), using the
   given FONT.
   The characters are justified according to JUSTIFICATION in a
   field that has WIDTH_LEFT space remaining after the characters
   themselves are accounted for.
   Before character I is written, its x-position is adjusted by
   KERNS[I]. */
static void
write_text (struct outp_driver *this,
            int x0, int y0,
            enum outp_font font,
            enum outp_justification justification,
            const struct afm_character **chars, int *kerns, size_t char_cnt,
            int width_left)
{
  struct ps_driver_ext *ext = this->ext;
  struct afm *afm = ext->fonts[font]->metrics;
  struct string out;
  size_t i, j;

  if (justification == OUTP_RIGHT)
    x0 += width_left;
  else if (justification == OUTP_CENTER)
    x0 += width_left / 2;
  y0 += afm_get_ascent (afm) * this->font_height / 1000;

  fprintf (ext->file, "\n%d %d moveto\n", x0, YT (y0));

  if (ext->last_font != font)
    {
      ext->last_font = font;
      fprintf (ext->file, "F%d setfont\n", font);
    }

  ds_init_empty (&out);
  for (i = 0; i < char_cnt; i = j)
    {
      for (j = i + 1; j < char_cnt; j++)
        if (kerns[j] != 0)
          break;

      if (kerns[i] != 0)
        fprintf (ext->file, "%d K", kerns[i]);
      while (i < j)
        {
          size_t encoded = afm_encode_string (afm, chars + i, j - i, &out);
          if (encoded > 0)
            {
              fprintf (ext->file, "%sS\n", ds_cstr (&out));
              ds_clear (&out);
              i += encoded;
            }

          if (i < j)
            {
              fprintf (ext->file, "/%s GS\n", chars[i]->name);
              i++;
            }
        }
    }
  ds_destroy (&out);
}

/* State of a text formatting operation. */
struct text_state
  {
    /* Input. */
    const struct outp_text *text;
    bool draw;

    /* Output. */
    const struct afm_character **glyphs;
    int *glyph_kerns;

    /* State. */
    size_t glyph_cnt;           /* Number of glyphs output. */
    int width_left;       	/* Width left over. */
    int height_left;            /* Height left over. */

    /* State as of last space. */
    const char *space_char;     /* Just past last space. */
    size_t space_glyph_cnt;     /* Number of glyphs as of last space. */
    int space_width_left;       /* Width left over as of last space. */

    /* Statistics. */
    int max_width;             /* Widest line so far. */
  };

/* Adjusts S to complete a line of text,
   and draws the current line if appropriate. */
static void
finish_line (struct outp_driver *this, struct text_state *s)
{
  int width;

  if (s->draw)
    {
      write_text (this,
                  s->text->x, s->text->y + (s->text->v - s->height_left),
                  s->text->font,
                  s->text->justification,
                  s->glyphs, s->glyph_kerns, s->glyph_cnt,
                  s->width_left);
      s->glyph_cnt = 0;
    }

  /* Update maximum width. */
  width = s->text->h - s->width_left;
  if (width > s->max_width)
    s->max_width = width;

  /* Move to next line. */
  s->width_left = s->text->h;
  s->height_left -= this->font_height;

  /* No spaces on this line yet. */
  s->space_char = NULL;
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
  struct ps_driver_ext *ext = this->ext;
  struct afm *afm = ext->fonts[text->font]->metrics;
  const char *cp;
  size_t glyph_cap;
  struct text_state s;

  s.text = text;
  s.draw = draw;

  s.glyphs = NULL;
  s.glyph_kerns = NULL;
  glyph_cap = 0;

  s.glyph_cnt = 0;
  s.width_left = s.text->h;
  s.height_left = s.text->v;

  s.space_char = 0;

  s.max_width = 0;

  cp = ss_data (s.text->string);
  while (s.height_left >= this->font_height && cp < ss_end (s.text->string))
    {
      const struct afm_character *cur;
      int char_width;
      int kern_adjust;

      if (*cp == '\n')
        {
          finish_line (this, &s);
          cp++;
          continue;
        }

      /* Get character and resolve ligatures. */
      cur = afm_get_character (afm, *cp);
      while (++cp < ss_end (s.text->string))
        {
          const struct afm_character *next = afm_get_character (afm, *cp);
          const struct afm_character *ligature = afm_get_ligature (cur, next);
          if (ligature == NULL)
            break;
          cur = ligature;
        }
      char_width = cur->width * this->font_height / 1000;

      /* Get kern adjustment. */
      if (s.glyph_cnt > 0)
        kern_adjust = (afm_get_kern_adjustment (s.glyphs[s.glyph_cnt - 1], cur)
                       * this->font_height / 1000);
      else
        kern_adjust = 0;

      /* Record the current status if this is a space character. */
      if (cur->code == ' ' && cp > ss_data (s.text->string))
	{
	  s.space_char = cp;
	  s.space_glyph_cnt = s.glyph_cnt;
          s.space_width_left = s.width_left;
	}

      /* Enough room on this line? */
      if (char_width + kern_adjust > s.width_left)
	{
	  if (s.space_char == NULL)
            {
              finish_line (this, &s);
              kern_adjust = 0;
            }
          else
            {
              cp = s.space_char;
              s.glyph_cnt = s.space_glyph_cnt;
              s.width_left = s.space_width_left;
              finish_line (this, &s);
              continue;
            }
	}

      if (s.glyph_cnt >= glyph_cap)
        {
          glyph_cap = 2 * (glyph_cap + 8);
          s.glyphs = xnrealloc (s.glyphs, glyph_cap, sizeof *s.glyphs);
          s.glyph_kerns = xnrealloc (s.glyph_kerns,
                                     glyph_cap, sizeof *s.glyph_kerns);
        }
      s.glyphs[s.glyph_cnt] = cur;
      s.glyph_kerns[s.glyph_cnt] = kern_adjust;
      s.glyph_cnt++;

      s.width_left -= char_width + kern_adjust;
    }
  if (s.height_left >= this->font_height && s.glyph_cnt > 0)
    finish_line (this, &s);

  if (width != NULL)
    *width = s.max_width;
  if (height != NULL)
    *height = text->v - s.height_left;
  free (s.glyphs);
  free (s.glyph_kerns);
}

static void
ps_text_metrics (struct outp_driver *this, const struct outp_text *t,
                 int *width, int *height)
{
  text (this, t, false, width, height);
}

static void
ps_text_draw (struct outp_driver *this, const struct outp_text *t)
{
  assert (this->page_open);
  text (this, t, true, NULL, NULL);
}

static void embed_font (struct outp_driver *this, struct font *font);
static void reencode_font (struct outp_driver *this, struct font *font);

/* Loads and returns the font for STRING, which has the format
   "AFM,PFA,ENC", where AFM is the AFM file's name, PFA is the
   PFA or PFB file's name, and ENC is the encoding file's name.
   PFA and ENC are optional.
   Returns a null pointer if unsuccessful. */
static struct font *
load_font (const char *string_)
{
  char *string = xstrdup (string_);
  struct font *font;
  char *position = string;
  char *token;
  char *afm_file_name;

  font = xmalloc (sizeof *font);
  font->metrics = NULL;
  font->embed_fn = NULL;
  font->encoding_fn = NULL;

  token = strsep (&position, ",");
  if (token == NULL)
    {
      error (0, 0, _("\"%s\": bad font specification"), string);
      goto error;
    }

  /* Read AFM file. */
  afm_file_name = find_ps_file (token);
  if (afm_file_name == NULL)
    {
      error (0, 0, _("could not find AFM file \"%s\""), token);
      goto error;
    }
  font->metrics = afm_open (afm_file_name);
  free (afm_file_name);
  if (font->metrics == NULL)
    goto error;

  /* Find font file to embed. */
  token = strsep (&position, ",");
  if (token != NULL && *token != '\0')
    {
      font->embed_fn = find_ps_file (token);
      if (font->embed_fn == NULL)
        error (0, 0, _("could not find font \"%s\""), token);
    }

  /* Find encoding. */
  token = strsep (&position, ",");
  if (token != NULL && *token == '\0')
    {
      font->encoding_fn = find_ps_file (token);
      if (font->encoding_fn == NULL)
        error (0, 0, _("could not find encoding \"%s\""), token);
    }

  free (string);
  return font;

 error:
  free (string);
  free_font (font);
  return NULL;
}

/* Frees FONT. */
static void
free_font (struct font *font)
{
  if (font != NULL)
    {
      afm_close (font->metrics);
      free (font->embed_fn);
      free (font->encoding_fn);
      free (font);
    }
}

/* Emits PostScript code to embed FONT (if necessary), scale it
   to the proper size, re-encode it (if necessary), and store the
   resulting font as an object named F#, where INDEX is
   substituted for #. */
static void
setup_font (struct outp_driver *this, struct font *font, int index)
{
  struct ps_driver_ext *x = this->ext;
  char *ps_name;

  if (font->embed_fn != NULL)
    embed_font (this, font);
  else
    fprintf (x->file, "%%%%IncludeResource: font %s\n",
             afm_get_findfont_name (font->metrics));

  ps_name = quote_ps_name (afm_get_findfont_name (font->metrics));
  fprintf (x->file, "%s findfont %d scalefont\n", ps_name, this->font_height);
  free (ps_name);

  if (font->encoding_fn != NULL)
    reencode_font (this, font);

  fprintf (x->file, "/F%d ED\n", index);
}

/* Copies up to COPY_BYTES bytes from SRC to DST, stopping at
   end-of-file or on error. */
static void
copy_bytes_literally (FILE *src, FILE *dst, unsigned long copy_bytes)
{
  while (copy_bytes > 0)
    {
      char buffer[BUFSIZ];
      unsigned long chunk_bytes = MIN (copy_bytes, sizeof buffer);
      size_t read_bytes = fread (buffer, 1, chunk_bytes, src);
      size_t write_bytes = fwrite (buffer, 1, read_bytes, dst);
      if (write_bytes != chunk_bytes)
        break;
      copy_bytes -= chunk_bytes;
    }
}

/* Copies up to COPY_BYTES bytes from SRC to DST, stopping at
   end-of-file or on error.  The bytes are translated into
   hexadecimal during copying and broken into lines with
   new-line characters. */
static void
copy_bytes_as_hex (FILE *src, FILE *dst, unsigned long copy_bytes)
{
  unsigned long i;

  for (i = 0; i < copy_bytes; i++)
    {
      int c = getc (src);
      if (c == EOF)
        break;
      if (i > 0 && i % 36 == 0)
        putc ('\n', dst);
      fprintf (dst, "%02X", c);
    }
  putc ('\n', dst);
}

/* Embeds the given FONT into THIS driver's output stream. */
static void
embed_font (struct outp_driver *this, struct font *font)
{
  struct ps_driver_ext *x = this->ext;
  FILE *file;
  int c;

  file = fopen (font->embed_fn, "rb");
  if (file == NULL)
    {
      error (errno, 0, _("cannot open font file \"%s\""), font->embed_fn);
      return;
    }

  fprintf (x->file, "%%%%BeginResource: font %s\n",
           afm_get_findfont_name (font->metrics));

  c = getc (file);
  ungetc (c, file);
  if (c != 128)
    {
      /* PFA file.  Copy literally. */
      copy_bytes_literally (file, x->file, ULONG_MAX);
    }
  else
    {
      /* PFB file.  Translate as specified in Adobe Technical
         Note #5040. */
      while ((c = getc (file)) == 128)
        {
          int type;
          unsigned long length;

          type = getc (file);
          if (type == 3)
            break;

          length = getc (file);
          length |= (unsigned long) getc (file) << 8;
          length |= (unsigned long) getc (file) << 16;
          length |= (unsigned long) getc (file) << 24;

          if (type == 1)
            copy_bytes_literally (file, x->file, length);
          else if (type == 2)
            copy_bytes_as_hex (file, x->file, length);
          else
            break;
        }
    }
  if (freaderror (file))
    error (errno, 0, _("reading font file \"%s\""), font->embed_fn);
  fputs ("%%EndResource\n", x->file);
}

/* Re-encodes FONT according to the specified encoding. */
static void
reencode_font (struct outp_driver *this, struct font *font)
{
  struct ps_driver_ext *x = this->ext;

  struct string line;

  int line_number;
  FILE *file;

  char *tab[256];

  int i;

  file = fopen (font->encoding_fn, "r");
  if (file == NULL)
    {
      error (errno, 0, _("cannot open font encoding file \"%s\""),
             font->encoding_fn);
      return;
    }

  for (i = 0; i < 256; i++)
    tab[i] = NULL;

  line_number = 0;

  ds_init_empty (&line);
  while (ds_read_config_line (&line, &line_number, file))
    {
      char *pschar, *code;
      char *save_ptr, *tail;
      int code_val;

      if (ds_is_empty (&line) == 0)
        continue;

      pschar = strtok_r (ds_cstr (&line), " \t\r\n", &save_ptr);
      code = strtok_r (NULL, " \t\r\n", &save_ptr);
      if (pschar == NULL || code == NULL)
        continue;

      code_val = strtol (code, &tail, 0);
      if (*tail)
        {
          error_at_line (0, 0, font->encoding_fn, line_number,
                         _("invalid numeric format"));
          continue;
        }
      if (code_val < 0 || code_val > 255)
        continue;
      if (tab[code_val] != 0)
        free (tab[code_val]);
      tab[code_val] = xstrdup (pschar);
    }
  ds_destroy (&line);

  fputs ("[", x->file);
  for (i = 0; i < 256; i++)
    {
      char *name = quote_ps_name (tab[i] != NULL ? tab[i] : ".notdef");
      fprintf (x->file, "%s\n", name);
      free (name);
      free (tab[i]);
    }
  fputs ("] RF\n", x->file);

  if (freaderror (file) != 0)
    error (errno, 0, _("closing Postscript encoding \"%s\""),
           font->encoding_fn);
}

/* PostScript driver class. */
const struct outp_class postscript_class =
{
  "postscript",
  0,

  ps_open_driver,
  ps_close_driver,

  ps_open_page,
  ps_close_page,
  NULL,

  ps_output_chart,

  ps_submit,

  ps_line,
  ps_text_metrics,
  ps_text_draw,
};
