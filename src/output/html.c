/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009 Free Software Foundation, Inc.

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
#include "chart.h"
#include "htmlP.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

#include <libpspp/assertion.h>
#include <libpspp/compiler.h>
#include <data/file-name.h>
#include <output/chart-provider.h>
#include <output/output.h>
#include <output/manager.h>
#include <output/table.h>
#include <libpspp/version.h>

#include "error.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* HTML driver options: (defaults listed first)

   output-file="pspp.html"
   chart-files="pspp-#.png"
*/

static void escape_string (FILE *file,
                           const char *text, size_t length,
                           const char *space);
static bool handle_option (void *this,
                           const char *key, const struct string *val);
static void print_title_tag (FILE *file, const char *name,
                             const char *content);

static bool
html_open_driver (const char *name, int types, struct substring options)
{
  struct outp_driver *this;
  struct html_driver_ext *x;

  this = outp_allocate_driver (&html_class, name, types);
  this->ext = x = xmalloc (sizeof *x);
  x->file_name = xstrdup ("pspp.html");
  x->chart_file_name = xstrdup ("pspp-#.png");
  x->file = NULL;
  x->chart_cnt = 1;

  outp_parse_options (name, options, handle_option, this);

  x->file = fn_open (x->file_name, "w");
  if (x->file == NULL)
    {
      error (0, errno, _("opening HTML output file: %s"), x->file_name);
      goto error;
    }

  fputs ("<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\"\n"
         "   \"http://www.w3.org/TR/html4/loose.dtd\">\n", x->file);
  fputs ("<HTML>\n", x->file);
  fputs ("<HEAD>\n", x->file);
  /* The <TITLE> tag is required, so we use a default if the user
     didn't provide one. */
  print_title_tag (x->file,
                   "TITLE", outp_title ? outp_title : _("PSPP Output"));
  fprintf (x->file, "<META NAME=\"generator\" CONTENT=\"%s\">\n", version);
  fputs ("<META HTTP-EQUIV=\"Content-Type\" "
         "CONTENT=\"text/html; charset=ISO-8859-1\">\n", x->file);
  fputs ("</HEAD>\n", x->file);
  fputs ("<BODY BGCOLOR=\"#ffffff\" TEXT=\"#000000\"\n", x->file);
  fputs (" LINK=\"#1f00ff\" ALINK=\"#ff0000\" VLINK=\"#9900dd\">\n", x->file);
  print_title_tag (x->file, "H1", outp_title);
  print_title_tag (x->file, "H2", outp_subtitle);

  outp_register_driver (this);
  return true;

 error:
  this->class->close_driver (this);
  outp_free_driver (this);
  return false;
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

static bool
html_close_driver (struct outp_driver *this)
{
  struct html_driver_ext *x = this->ext;
  bool ok;

  if (x->file != NULL)
    {
      fprintf (x->file,
               "</BODY>\n"
               "</HTML>\n"
               "<!-- end of file -->\n");
      ok = fn_close (x->file_name, x->file) == 0;
      x->file = NULL;
    }
  else
    ok = true;
  free (x->chart_file_name);
  free (x->file_name);
  free (x);

  return ok;
}

/* Generic option types. */
enum
  {
    string_arg,
    nonneg_int_arg
  };

/* All the options that the HTML driver supports. */
static const struct outp_option option_tab[] =
  {
    {"output-file",		string_arg,     0},
    {"chart-files",            string_arg,     1},
    {NULL, 0, 0},
  };

static bool
handle_option (void *this_, const char *key, const struct string *val)
{
  struct outp_driver *this = this_;
  struct html_driver_ext *x = this->ext;
  int subcat;

  switch (outp_match_keyword (key, option_tab, &subcat))
    {
    case -1:
      error (0, 0,
             _("unknown configuration parameter `%s' for HTML device driver"),
             key);
      break;
    case string_arg:
      switch (subcat)
        {
        case 0:
          free (x->file_name);
          x->file_name = ds_xstrdup (val);
          break;
        case 1:
          if (ds_find_char (val, '#') != SIZE_MAX)
            {
              free (x->chart_file_name);
              x->chart_file_name = ds_xstrdup (val);
            }
          else
            error (0, 0, _("`chart-files' value must contain `#'"));
          break;
        default:
          NOT_REACHED ();
        }
      break;
    default:
      NOT_REACHED ();
    }

  return true;
}

static void output_tab_table (struct outp_driver *, struct tab_table *);

static void
html_output_chart (struct outp_driver *this, const struct chart *chart)
{
  struct html_driver_ext *x = this->ext;
  char *file_name;

  file_name = chart_draw_png (chart, x->chart_file_name, x->chart_cnt++);
  fprintf (x->file, "<IMG SRC=\"%s\"/>", file_name);
  free (file_name);
}

static void
html_submit (struct outp_driver *this, struct som_entity *s)
{
  extern struct som_table_class tab_table_class;

  assert (s->class == &tab_table_class ) ;

  switch (s->type)
    {
    case SOM_TABLE:
      output_tab_table ( this, (struct tab_table *) s->ext);
      break;
    default:
      NOT_REACHED ();
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
        default:
          putc (c, file);
          break;
        }
    }
}

/* Outputs content for a cell with options OPTS and contents
   TEXT. */
void
html_put_cell_contents (struct outp_driver *this,
                        unsigned int opts, const struct substring text)
{
  struct html_driver_ext *x = this->ext;

  if (!(opts & TAB_EMPTY))
    {
      if (opts & TAB_EMPH)
        fputs ("<EM>", x->file);
      if (opts & TAB_FIX)
        {
          fputs ("<TT>", x->file);
          escape_string (x->file, ss_data (text), ss_length (text), "&nbsp;");
          fputs ("</TT>", x->file);
        }
      else
        {
          size_t initial_spaces = ss_span (text, ss_cstr (CC_SPACES));
          escape_string (x->file,
                         ss_data (text) + initial_spaces,
                         ss_length (text) - initial_spaces,
                         " ");
        }
      if (opts & TAB_EMPH)
        fputs ("</EM>", x->file);
    }
}

/* Write table T to THIS output driver. */
static void
output_tab_table (struct outp_driver *this, struct tab_table *t)
{
  struct html_driver_ext *x = this->ext;

  if (t->nr == 1 && t->nc == 1)
    {
      fputs ("<P>", x->file);
      html_put_cell_contents (this, t->ct[0], *t->cc);
      fputs ("</P>\n", x->file);

      return;
    }

  fputs ("<TABLE BORDER=1>\n", x->file);

  if (t->title != NULL)
    {
      fprintf (x->file, "  <CAPTION>");
      escape_string (x->file, t->title, strlen (t->title), " ");
      fputs ("</CAPTION>\n", x->file);
    }

  {
    int r;
    unsigned char *ct = t->ct;

    for (r = 0; r < t->nr; r++)
      {
	int c;

	fputs ("  <TR>\n", x->file);
	for (c = 0; c < t->nc; c++, ct++)
	  {
            struct substring *cc;
            const char *tag;
            struct tab_joined_cell *j = NULL;

            cc = t->cc + c + r * t->nc;
	    if (*ct & TAB_JOIN)
              {
                j = (struct tab_joined_cell *) ss_data (*cc);
                cc = &j->contents;
                if (j->x1 != c || j->y1 != r)
                  continue;
              }

            /* Output <TD> or <TH> tag. */
            tag = (r < t->t || r >= t->nr - t->b
                   || c < t->l || c >= t->nc - t->r) ? "TH" : "TD";
            fprintf (x->file, "    <%s ALIGN=%s",
                     tag,
                     (*ct & TAB_ALIGN_MASK) == TAB_LEFT ? "LEFT"
                     : (*ct & TAB_ALIGN_MASK) == TAB_RIGHT ? "RIGHT"
                     : "CENTER");
	    if (*ct & TAB_JOIN)
	      {
		if (j->x2 - j->x1 > 1)
		  fprintf (x->file, " COLSPAN=%d", j->x2 - j->x1);
		if (j->y2 - j->y1 > 1)
		  fprintf (x->file, " ROWSPAN=%d", j->y2 - j->y1);
	      }
	    putc ('>', x->file);

            /* Output cell contents. */
            html_put_cell_contents (this, *ct, *cc);

            /* Output </TH> or </TD>. */
	    fprintf (x->file, "</%s>\n", tag);
	  }
	fputs ("  </TR>\n", x->file);
      }
  }

  fputs ("</TABLE>\n\n", x->file);
}



/* HTML driver class. */
const struct outp_class html_class =
  {
    "html",
    1,

    html_open_driver,
    html_close_driver,

    NULL,
    NULL,
    NULL,

    html_output_chart,

    html_submit,

    NULL,
    NULL,
    NULL,
  };
