/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

#include "data/file-name.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/message.h"
#include "libpspp/version.h"
#include "output/cairo.h"
#include "output/chart-item.h"
#include "output/driver-provider.h"
#include "output/message-item.h"
#include "output/options.h"
#include "output/output-item-provider.h"
#include "output/table-provider.h"
#include "output/table-item.h"
#include "output/text-item.h"

#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct html_driver
  {
    struct output_driver driver;

    struct xr_color fg;
    struct xr_color bg;
    
    char *file_name;
    char *chart_file_name;

    char *command_name;
    FILE *file;
    size_t chart_cnt;

    bool css;
    bool borders;
  };

static const struct output_driver_class html_driver_class;

static void html_output_table (struct html_driver *, struct table_item *);
static void escape_string (FILE *file,
                           const char *text, size_t length,
                           const char *space);
static void print_title_tag (FILE *file, const char *name,
                             const char *content);

static struct html_driver *
html_driver_cast (struct output_driver *driver)
{
  assert (driver->class == &html_driver_class);
  return UP_CAST (driver, struct html_driver, driver);
}

static struct driver_option *
opt (struct output_driver *d, struct string_map *options, const char *key,
     const char *default_value)
{
  return driver_option_get (d, options, key, default_value);
}

static struct output_driver *
html_create (const char *file_name, enum settings_output_devices device_type,
             struct string_map *o)
{
  struct output_driver *d;
  struct html_driver *html;

  html = xzalloc (sizeof *html);
  d = &html->driver;
  output_driver_init (&html->driver, &html_driver_class, file_name,
                      device_type);
  html->css = parse_boolean (opt (d, o, "css", "true"));
  html->borders = parse_boolean (opt (d, o, "borders", "true"));

  html->file_name = xstrdup (file_name);
  html->chart_file_name = parse_chart_file_name (opt (d, o, "charts",
                                                      file_name));
  html->file = NULL;
  html->chart_cnt = 1;

  parse_color (d, o, "background-color", "#FFFFFFFFFFFF", &html->bg);
  parse_color (d, o, "foreground-color", "#000000000000", &html->fg);

  html->file = fn_open (html->file_name, "w");
  if (html->file == NULL)
    {
      msg_error (errno, _("error opening output file `%s'"), html->file_name);
      goto error;
    }

  fputs ("<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\"\n"
         "   \"http://www.w3.org/TR/html4/loose.dtd\">\n", html->file);
  fputs ("<HTML>\n", html->file);
  fputs ("<HEAD>\n", html->file);
  print_title_tag (html->file, "TITLE", _("PSPP Output"));
  fprintf (html->file, "<META NAME=\"generator\" CONTENT=\"%s\">\n", version);
  fputs ("<META HTTP-EQUIV=\"Content-Type\" "
         "CONTENT=\"text/html; charset=utf-8\">\n", html->file);

  if ( html->css)
    {
      fputs ("<META http-equiv=\"Content-Style-Type\" content=\"text/css\">\n",
	     html->file);
      fputs ("<STYLE TYPE=\"text/css\">\n"
	     "<!--\n"
	     "body {\n"
	     "  background: white;\n"
	     "  color: black;\n"
	     "  padding: 0em 12em 0em 3em;\n"
	     "  margin: 0\n"
	     "}\n"
	     "body>p {\n"
	     "  margin: 0pt 0pt 0pt 0em\n"
	     "}\n"
	     "body>p + p {\n"
	     "  text-indent: 1.5em;\n"
	     "}\n"
	     "h1 {\n"
	     "  font-size: 150%;\n"
	     "  margin-left: -1.33em\n"
	     "}\n"
	     "h2 {\n"
	     "  font-size: 125%;\n"
	     "  font-weight: bold;\n"
	     "  margin-left: -.8em\n"
	     "}\n"
	     "h3 {\n"
	     "  font-size: 100%;\n"
	     "  font-weight: bold;\n"
	     "  margin-left: -.5em }\n"
	     "h4 {\n"
	     "  font-size: 100%;\n"
	     "  margin-left: 0em\n"
	     "}\n"
	     "h1, h2, h3, h4, h5, h6 {\n"
	     "  font-family: sans-serif;\n"
	     "  color: blue\n"
	     "}\n"
	     "html {\n"
	     "  margin: 0\n"
	     "}\n"
	     "code {\n"
	     "  font-family: sans-serif\n"
	     "}\n"
	     "table {\n"
	     "  border-collapse: collapse;\n"
	     "  margin-bottom: 1em\n"
	     "}\n"
	     "th { background: #dddddd; font-weight: normal; font-style: oblique }\n"
	     "caption {\n"
	     "  text-align: left\n"
	     "}\n"
	     "-->\n"
	     "</STYLE>\n",
	     html->file);
    }
  fputs ("</HEAD>\n", html->file);
  fputs ("<BODY BGCOLOR=\"#ffffff\" TEXT=\"#000000\"\n", html->file);
  fputs (" LINK=\"#1f00ff\" ALINK=\"#ff0000\" VLINK=\"#9900dd\">\n", html->file);

  return d;

 error:
  output_driver_destroy (d);
  return NULL;
}

/* Emits <NAME>CONTENT</NAME> to the output, escaping CONTENT as
   necessary for HTML. */
static void
print_title_tag (FILE *file, const char *name, const char *content)
{
  if (content != NULL)
    {
      fprintf (file, "<%s>", name);
      escape_string (file, content, strlen (content), " ");
      fprintf (file, "</%s>\n", name);
    }
}

static void
html_destroy (struct output_driver *driver)
{
  struct html_driver *html = html_driver_cast (driver);

  if (html->file != NULL)
    {
      fprintf (html->file,
               "</BODY>\n"
               "</HTML>\n"
               "<!-- end of file -->\n");
      fn_close (html->file_name, html->file);
    }
  free (html->chart_file_name);
  free (html->file_name);
  free (html->command_name);
  free (html);
}

static void
html_submit (struct output_driver *driver,
             const struct output_item *output_item)
{
  struct html_driver *html = html_driver_cast (driver);

  output_driver_track_current_command (output_item, &html->command_name);

  if (is_table_item (output_item))
    {
      struct table_item *table_item = to_table_item (output_item);
      html_output_table (html, table_item);
    }
#ifdef HAVE_CAIRO
  else if (is_chart_item (output_item) && html->chart_file_name != NULL)
    {
      struct chart_item *chart_item = to_chart_item (output_item);
      char *file_name;

      file_name = xr_draw_png_chart (chart_item, html->chart_file_name,
                                     html->chart_cnt++,
				     &html->fg,
				     &html->bg
				     );
      if (file_name != NULL)
        {
	  const char *title = chart_item_get_title (chart_item);
          fprintf (html->file, "<IMG SRC=\"%s\" ALT=\"Chart: %s\">",
		   file_name, title ? title : _("No description"));
          free (file_name);
        }
    }
#endif  /* HAVE_CAIRO */
  else if (is_text_item (output_item))
    {
      struct text_item *text_item = to_text_item (output_item);
      const char *s = text_item_get_text (text_item);

      switch (text_item_get_type (text_item))
        {
        case TEXT_ITEM_TITLE:
          print_title_tag (html->file, "H1", s);
          break;

        case TEXT_ITEM_SUBTITLE:
          print_title_tag (html->file, "H2", s);
          break;

        case TEXT_ITEM_COMMAND_OPEN:
          fprintf (html->file, "<DIV class=\"");
          escape_string (html->file, s, strlen (s), "_");
          fprintf (html->file, "\">");
          print_title_tag (html->file, "H3", s);
          break;

        case TEXT_ITEM_COMMAND_CLOSE:
          fprintf (html->file, "</DIV>\n");
          break;

        case TEXT_ITEM_SUBHEAD:
          print_title_tag (html->file, "H4", s);
          break;

        case TEXT_ITEM_SYNTAX:
          fprintf (html->file, "<PRE class=\"syntax\">");
          escape_string (html->file, s, strlen (s), " ");
          fprintf (html->file, "</PRE>\n");
          break;

        case TEXT_ITEM_PARAGRAPH:
          print_title_tag (html->file, "P", s);
          break;

        case TEXT_ITEM_MONOSPACE:
          print_title_tag (html->file, "PRE", s); /* should be <P><TT> */
          break;

        case TEXT_ITEM_BLANK_LINE:
          fputs ("<BR>", html->file);
          break;

        case TEXT_ITEM_EJECT_PAGE:
          /* Nothing to do. */
          break;

        case TEXT_ITEM_COMMENT:
        case TEXT_ITEM_ECHO:
          /* We print out syntax anyway, so nothing to do here either. */
          break;
        }
    }
  else if (is_message_item (output_item))
    {
      const struct message_item *message_item = to_message_item (output_item);
      const struct msg *msg = message_item_get_msg (message_item);
      char *s = msg_to_string (msg, html->command_name);
      print_title_tag (html->file, "P", s);
      free (s);
    }
}

/* Write LENGTH characters in TEXT to file F, escaping characters
   as necessary for HTML.  Spaces are replaced by SPACE, which
   should be " " or "&nbsp;". */
static void
escape_string (FILE *file,
               const char *text, size_t length,
               const char *space)
{
  while (length-- > 0)
    {
      char c = *text++;
      switch (c)
        {
        case '&':
          fputs ("&amp;", file);
          break;
        case '<':
          fputs ("&lt;", file);
          break;
        case '>':
          fputs ("&gt;", file);
          break;
        case ' ':
          fputs (space, file);
          break;
        case '"':
          fputs ("&quot;", file);
          break;
        default:
          putc (c, file);
          break;
        }
    }
}

static void
put_border (FILE *file, int n_borders, int style, const char *border_name)
{
  fprintf (file, "%sborder-%s: %s",
           n_borders == 0 ? " STYLE=\"" : "; ",
           border_name,
           style == TAL_1 ? "thin solid" : "double");
}

static void
html_output_table (struct html_driver *html, struct table_item *item)
{
  const struct table *t = table_item_get_table (item);
  const char *caption;
  int x, y;

  fputs ("<TABLE>\n", html->file);

  caption = table_item_get_caption (item);
  if (caption != NULL)
    {
      fputs ("  <CAPTION>", html->file);
      escape_string (html->file, caption, strlen (caption), " ");
      fputs ("</CAPTION>\n", html->file);
    }

  for (y = 0; y < table_nr (t); y++)
    {
      fputs ("  <TR>\n", html->file);
      for (x = 0; x < table_nc (t); x++)
        {
          struct table_cell cell;
          const char *tag;
          bool is_header;
          int alignment, colspan, rowspan;
          int top, left, right, bottom, n_borders;
          const char *s;

          table_get_cell (t, x, y, &cell);
          if (x != cell.d[TABLE_HORZ][0] || y != cell.d[TABLE_VERT][0])
            continue;

          /* Output <TD> or <TH> tag. */
          is_header = (y < table_ht (t)
                       || y >= table_nr (t) - table_hb (t)
                       || x < table_hl (t)
                       || x >= table_nc (t) - table_hr (t));
          tag = is_header ? "TH" : "TD";
          fprintf (html->file, "    <%s", tag);

          alignment = cell.options & TAB_ALIGNMENT;
          if (alignment != TAB_LEFT)
            fprintf (html->file, " ALIGN=\"%s\"",
                     alignment == TAB_RIGHT ? "RIGHT" : "CENTER");

          colspan = table_cell_colspan (&cell);
          if (colspan > 1)
            fprintf (html->file, " COLSPAN=\"%d\"", colspan);

          rowspan = table_cell_rowspan (&cell);
          if (rowspan > 1)
            fprintf (html->file, " ROWSPAN=\"%d\"", rowspan);

	  if (html->borders)
	    {
	      /* Cell borders. */
	      n_borders = 0;
          
	      top = table_get_rule (t, TABLE_VERT, x, y);
	      if (top > TAL_GAP)
		put_border (html->file, n_borders++, top, "top");

	      if (y == table_nr (t) - 1)
		{
		  bottom = table_get_rule (t, TABLE_VERT, x, y + 1);
		  if (bottom > TAL_GAP)
		    put_border (html->file, n_borders++, bottom, "bottom");
		}

	      left = table_get_rule (t, TABLE_HORZ, x, y);
	      if (left > TAL_GAP)
		put_border (html->file, n_borders++, left, "left");

	      if (x == table_nc (t) - 1)
		{
		  right = table_get_rule (t, TABLE_HORZ, x + 1, y);
		  if (right > TAL_GAP)
		    put_border (html->file, n_borders++, right, "right");
		}

	      if (n_borders > 0)
		fputs ("\"", html->file);
	    }

          putc ('>', html->file);

          /* Output cell contents. */
          s = cell.contents;
          if (cell.options & TAB_EMPH)
            fputs ("<EM>", html->file);
          if (cell.options & TAB_FIX)
            {
              fputs ("<TT>", html->file);
              escape_string (html->file, s, strlen (s), "&nbsp;");
              fputs ("</TT>", html->file);
            }
          else
            {
              s += strspn (s, CC_SPACES);
              escape_string (html->file, s, strlen (s), " ");
            }
          if (cell.options & TAB_EMPH)
            fputs ("</EM>", html->file);

          /* Output </TH> or </TD>. */
          fprintf (html->file, "</%s>\n", tag);

          table_cell_free (&cell);
        }
      fputs ("  </TR>\n", html->file);
    }

  fputs ("</TABLE>\n\n", html->file);
}

struct output_driver_factory html_driver_factory =
  { "html", "pspp.html", html_create };

static const struct output_driver_class html_driver_class =
  {
    "html",
    html_destroy,
    html_submit,
    NULL,
  };
