/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>
#include "chart.h"
#include "htmlP.h"
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

#include <libpspp/alloc.h>
#include <libpspp/compiler.h>
#include <data/filename.h>
#include "error.h"
#include "getline.h"
#include "getlogin_r.h"
#include "output.h"
#include "manager.h"
#include "table.h"
#include <libpspp/version.h>
#include <data/make-file.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

static void escape_string (FILE *file,
                           const char *text, size_t length,
                           const char *space);
static bool handle_option (struct outp_driver *this,
                           const char *key, const struct string *val);
static void print_title_tag (FILE *file, const char *name,
                             const char *content);

static bool
html_open_driver (struct outp_driver *this, const struct string *options)
{
  struct html_driver_ext *x;

  this->ext = x = xmalloc (sizeof *x);
  x->file_name = xstrdup ("pspp.html");
  x->file = NULL;

  outp_parse_options (options, handle_option, this);

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

  return true;

 error:
  this->class->close_driver (this);
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
  free (x->file_name);
  free (x);
   
  return ok;
}

/* Link the image contained in FILE_NAME to the 
   HTML stream in FILE. */
static void
link_image (FILE *file, char *file_name)
{
  fprintf (file, "<IMG SRC=\"%s\"/>", file_name);
 }

/* Generic option types. */
enum
  {
    string_arg,
    nonneg_int_arg
  };

/* All the options that the HTML driver supports. */
static struct outp_option option_tab[] =
  {
    {"output-file",		string_arg,     0},
    {NULL, 0, 0},
  };

static bool
handle_option (struct outp_driver *this,
               const char *key, const struct string *val)
{
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
      free (x->file_name);
      x->file_name = xstrdup (ds_c_str (val));
      break;
    default:
      abort ();
    }
  
  return true;
}

static void output_tab_table (struct outp_driver *, struct tab_table *);

static void
html_submit (struct outp_driver *this, struct som_entity *s)
{
  extern struct som_table_class tab_table_class;
  struct html_driver_ext *x = this->ext;
  
  assert (s->class == &tab_table_class ) ;

  switch (s->type) 
    {
    case SOM_TABLE:
      output_tab_table ( this, (struct tab_table *) s->ext);
      break;
    case SOM_CHART:
      link_image (x->file, ((struct chart *)s->ext)->filename);
      break;
    default:
      abort ();
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
                        unsigned int opts, struct fixed_string *text)
{
  struct html_driver_ext *x = this->ext;

  if (!(opts & TAB_EMPTY)) 
    {
      if (opts & TAB_EMPH)
        fputs ("<EM>", x->file);
      if (opts & TAB_FIX) 
        {
          fputs ("<TT>", x->file);
          escape_string (x->file, ls_c_str (text), ls_length (text), "&nbsp;");
          fputs ("</TT>", x->file);
        }
      else 
        {
          size_t initial_spaces = strspn (ls_c_str (text), " \t");
          escape_string (x->file,
                         ls_c_str (text) + initial_spaces,
                         ls_length (text) - initial_spaces,
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
      html_put_cell_contents (this, t->ct[0], t->cc);
      fputs ("</P>\n", x->file);
      
      return;
    }

  fputs ("<TABLE BORDER=1>\n", x->file);
  
  if (!ls_empty_p (&t->title))
    {
      fprintf (x->file, "  <CAPTION>");
      escape_string (x->file, ls_c_str (&t->title), ls_length (&t->title),
                     " ");
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
            struct fixed_string *cc;
            const char *tag;
            struct tab_joined_cell *j = NULL;

            cc = t->cc + c + r * t->nc;
	    if (*ct & TAB_JOIN)
              {
                j = (struct tab_joined_cell *) ls_c_str (cc);
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
            html_put_cell_contents (this, *ct, cc);

            /* Output </TH> or </TD>. */
	    fprintf (x->file, "</%s>\n", tag);
	  }
	fputs ("  </TR>\n", x->file);
      }
  }
	      
  fputs ("</TABLE>\n\n", x->file);
}

static void
html_initialise_chart(struct outp_driver *d UNUSED, struct chart *ch)
{

  FILE  *fp;

  make_unique_file_stream(&fp, &ch->filename);

#ifdef NO_CHARTS
  ch->lp = 0;
#else
  ch->pl_params = pl_newplparams();
  ch->lp = pl_newpl_r ("png", 0, fp, stderr, ch->pl_params);
#endif

}

static void 
html_finalise_chart(struct outp_driver *d UNUSED, struct chart *ch)
{
  free(ch->filename);
}



/* HTML driver class. */
struct outp_class html_class =
  {
    "html",
    1,

    html_open_driver,
    html_close_driver,

    NULL,
    NULL,

    html_submit,

    NULL,
    NULL,
    NULL,
    html_initialise_chart,
    html_finalise_chart
  };
