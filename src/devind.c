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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

/* Device-independent output format.  Eventually I intend for all
   PSPP output to work this way, but adding it as an available
   format is a first step.

   Each line in the output is a command.  The first character on
   the line is the command name, and the rest of the line is the
   command arguments.  The commands are described below as Perl
   regular expressions:

   #.*          comment

   s            starts a new table
   S[rc]\d+     table size in rows or columns (optional)
   H[lrtb]\d+   number of left/right/top/bottom header rows/columns
   B(\d+)-(\d+)/(\d+)
                allow column breaks every \3 rows from \1 to \2 exclusive
   T.*          table title
   C.*          table caption (not yet supported)
   t(\d+)(-\d+)?,(\d+)(-\d+)?[wn][hb][lcr][tmb]:.*
                text for cells in rows (\1-\2) inclusive and
                columns (\3-\4) inclusive,
                wrappable/nonwrappable, header/body,
                left/center/right justified, top/middle/bottom
                justified
   l[hv][sdtn](\d+),(\d+)-(\d+)
                horiz/vert line in single/double/thick/none
                style, running across columns/rows \2 to \3
                inclusive at offset \1 from top/left side of
                table
   b[sdtno]{4}(\d+)-(\d+),(\d+)-(\d+)
                box across columns \1 to \2 inclusive and rows \3
                to \4 inclusive with
                single/double/thick/none/omit style for horiz &
                vert frame and horiz & vert interior lines
   f(\d+),(\d+):.*
                add footnote for cell \1, \2
   e            end table

   v(\d(.\d+)+) insert \1 lines of blank space

   p:.*         plain text
   m[ewmlu]:(.*),(\d+),((\d+)(-\d+)?)?:(.*)
                error/warning/message/listing/user class message
                for file \1, line \2, columns \4 to \5, actual
                message \6

   q            end of file

   Text tokens are free-form, except that they are terminated by
   commas and new-lines.  The following escapes are allowed:

   \\n          line break
   \\c          comma
   \\s          non-breaking space
   \\[0-7]{3}   octal escape
   \\B          toggle subscript
   \\P          toggle superscript
   \\e          toggle emphasis
   \\E          toggle strong emphasis
   \\v          toggle variable name font
   \\F          toggle file name font
   \\p          toggle fixed-pitch text font (default: proportional)
   \\n\((\d+)?(\.\d+)?(-?\d+(\.\d+)?+(e-?\d+))?\)
                number \3 (sysmis if not provided) in \1.\2 format
   \\f\(([A-Z]*(\d+)?(\.\d+)?)(-?\d+(\.\d+)?+(e-?\d+))?\)
                number \1 in \4 format

*/

#include <config.h>
#include "devind.h"
#include "error.h"
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "alloc.h"
#include "error.h"
#include "filename.h"
#include "getline.h"
#include "output.h"
#include "som.h"
#include "tab.h"
#include "version.h"

/* Device-independent output driver extension record. */
struct devind_driver_ext
  {
    /* Internal state. */
    struct file_ext file;	/* Output file. */
    int sequence_no;		/* Sequence number. */
  };

static int
devind_open_global (struct outp_class *this UNUSED)
{
  return 1;
}

static int
devind_close_global (struct outp_class *this UNUSED)
{
  return 1;
}

static int
devind_preopen_driver (struct outp_driver *this)
{
  struct devind_driver_ext *x;

  assert (this->driver_open == 0);
  msg (VM (1), _("DEVIND driver initializing as `%s'..."), this->name);

  this->ext = x = xmalloc (sizeof *x);
  this->res = 0;
  this->horiz = this->vert = 0;
  this->width = this->length = 0;

  this->cp_x = this->cp_y = 0;

  x->file.filename = NULL;
  x->file.mode = "w";
  x->file.file = NULL;
  x->file.sequence_no = &x->sequence_no;
  x->file.param = this;
  x->file.postopen = NULL;
  x->file.preclose = NULL;

  x->sequence_no = 0;

  return 1;
}

static int
devind_postopen_driver (struct outp_driver *this)
{
  struct devind_driver_ext *x = this->ext;

  assert (this->driver_open == 0);
  if (NULL == x->file.filename)
    x->file.filename = xstrdup ("pspp.devind");
	
  msg (VM (2), _("%s: Initialization complete."), this->name);
  this->driver_open = 1;

  return 1;
}

static int
devind_close_driver (struct outp_driver *this)
{
  struct devind_driver_ext *x = this->ext;

  assert (this->driver_open);
  msg (VM (2), _("%s: Beginning closing..."), this->name);
  fputs ("q\n", x->file.file);
  fn_close_ext (&x->file);
  free (x->file.filename);
  free (x);
  msg (VM (3), _("%s: Finished closing."), this->name);
  this->driver_open = 0;
  
  return 1;
}

/* Generic option types. */
enum
{
  boolean_arg = -10,
  string_arg,
  nonneg_int_arg
};

/* All the options that the DEVIND driver supports. */
static struct outp_option option_tab[] =
{
  /* *INDENT-OFF* */
  {"output-file",		1,		0},
  {"", 0, 0},
  /* *INDENT-ON* */
};
static struct outp_option_info option_info;

static void
devind_option (struct outp_driver *this, const char *key, const struct string *val)
{
  struct devind_driver_ext *x = this->ext;
  int cat, subcat;

  cat = outp_match_keyword (key, option_tab, &option_info, &subcat);
  switch (cat)
    {
    case 0:
      msg (SE, _("Unknown configuration parameter `%s' for DEVIND device "
	   "driver."), key);
      break;
    case 1:
      free (x->file.filename);
      x->file.filename = xstrdup (ds_value (val));
      break;
    default:
      assert (0);
    }
}

static int
devind_open_page (struct outp_driver *this)
{
  struct devind_driver_ext *x = this->ext;

  assert (this->driver_open && this->page_open == 0);
  x->sequence_no++;
  if (!fn_open_ext (&x->file))
    {
      if (errno)
	msg (ME, _("DEVIND output driver: %s: %s"), x->file.filename,
	     strerror (errno));
      return 0;
    }

  if (!ferror (x->file.file))
    this->page_open = 1;
  return !ferror (x->file.file);
}

static int
devind_close_page (struct outp_driver *this)
{
  struct devind_driver_ext *x = this->ext;

  assert (this->driver_open && this->page_open);
  this->page_open = 0;
  return !ferror (x->file.file);
}

static void output_tab_table (struct outp_driver *, struct tab_table *);

static void
devind_submit (struct outp_driver *this, struct som_table *s)
{
  extern struct som_table_class tab_table_class;
  struct devind_driver_ext *x = this->ext;
  
  assert (this->driver_open && this->page_open);
  if (x->sequence_no == 0 && !devind_open_page (this))
    {
      msg (ME, _("Cannot open first page on DEVIND device %s."), this->name);
      return;
    }

  if (s->class == &tab_table_class)
    output_tab_table (this, s->ext);
  else
    assert (0);
}

/* Write string S of length LEN to file F, escaping characters as
   necessary for DEVIND. */
static void
escape_string (FILE *f, char *s, int len)
{
  char *ep = &s[len];
  char *bp, *cp;

  putc (':', f);

  for (bp = cp = s; bp < ep; bp = cp)
    {
      while (cp < ep && *cp != ',' && *cp != '\n' && *cp)
	cp++;
      if (cp > bp)
	fwrite (bp, 1, cp - bp, f);
      if (cp < ep)
	switch (*cp++)
	  {
	  case ',':
	    fputs ("\\c", f);
	    break;
	  case '\n':
	    fputs ("\\n", f);
	    break;
	  case 0:
	    break;
	  default:
	    assert (0);
	  }
    }
}
  
/* Write table T to THIS output driver. */
static void
output_tab_table (struct outp_driver *this, struct tab_table *t)
{
  struct devind_driver_ext *x = this->ext;
  
  if (t->nr == 1 && t->nc == 1)
    {
      fputs ("p:", x->file.file);
      escape_string (x->file.file, ls_value (t->cc), ls_length (t->cc));
      putc ('\n', x->file.file);
      
      return;
    }

  /* Start table. */
  fprintf (x->file.file, "s\n");

  /* Table size. */
  fprintf (x->file.file, "Sr%d\n", t->nr);
  fprintf (x->file.file, "Sc%d\n", t->nc);

  /* Table headers. */
  if (t->l != 0)
    fprintf (x->file.file, "Hl%d\n", t->l);
  if (t->r != 0)
    fprintf (x->file.file, "Hr%d\n", t->r);
  if (t->t != 0)
    fprintf (x->file.file, "Ht%d\n", t->t);
  if (t->b != 0)
    fprintf (x->file.file, "Hb%d\n", t->b);

  /* Title. */
  if (!ls_empty_p (&t->title))
    {
      putc ('T', x->file.file);
      escape_string (x->file.file, ls_value (&t->title),
		     ls_length (&t->title));
      putc ('\n', x->file.file);
    }

  /* Column breaks. */
  if (t->col_style == TAB_COL_DOWN) 
    fprintf (x->file.file, "B%d-%d/%d\n", t->t, t->nr - t->b, t->col_group);

  /* Table text. */
  {
    int r;
    unsigned char *ct = t->ct;

    for (r = 0; r < t->nr; r++)
      {
	int c;
	
	for (c = 0; c < t->nc; c++, ct++)
	  {
            struct len_string *cc;
            struct tab_joined_cell *j;

            if (*ct == TAB_EMPTY)
              continue;
            
            cc = t->cc + c + r * t->nc;
	    if (*ct & TAB_JOIN) 
              {
                j = (struct tab_joined_cell *) ls_value (cc);
                cc = &j->contents;
                if (c != j->x1 || r != j->y1)
                  continue;
              }
            else
              j = NULL;

            putc ('t', x->file.file);
            if (j == NULL) 
              fprintf (x->file.file, "%d,%d", r, c);
            else
              fprintf (x->file.file, "%d-%d,%d-%d",
                       j->y1, j->y2, j->x1, j->x2);
            putc ((*ct & TAT_NOWRAP) ? 'n' : 'w', x->file.file);
            putc ((*ct & TAT_TITLE) ? 'h' : 'b', x->file.file);
            if ((*ct & TAB_ALIGN_MASK) == TAB_RIGHT)
              putc ('r', x->file.file);
            else if ((*ct & TAB_ALIGN_MASK) == TAB_LEFT)
              putc ('l', x->file.file);
            else
              putc ('c', x->file.file);
            putc ('t', x->file.file);
            escape_string (x->file.file, ls_value (cc), ls_length (cc));
            putc ('\n', x->file.file);
          }
      }
  }

  /* Horizontal lines. */
  {
    int r, c;

    for (r = 0; r <= t->nr; r++)
      for (c = 0; c < t->nc; c++) 
        {
          int rule = t->rh[c + r * t->nc];
          if (rule != 0)
            fprintf (x->file.file, "lh%c%d,%d-%d\n", "nsdt"[rule], r, c, c);
        }
  }

  /* Vertical lines. */
  {
    int r, c;

    for (r = 0; r < t->nr; r++)
      for (c = 0; c <= t->nc; c++) 
        {
          int rule = t->rv[c + r * (t->nc + 1)];
          if (rule != 0)
            fprintf (x->file.file, "lv%c%d,%d-%d\n", "nsdt"[rule], c, r, r);
        }
  }

  /* End of table. */
  fputs ("e\n", x->file.file);
}

/* DEVIND driver class. */
struct outp_class devind_class =
{
  "devind",
  0xb1e7,
  1,

  devind_open_global,
  devind_close_global,
  NULL,

  devind_preopen_driver,
  devind_option,
  devind_postopen_driver,
  devind_close_driver,

  devind_open_page,
  devind_close_page,

  devind_submit,

  NULL,
  NULL,
  NULL,

  NULL,
  NULL,
  NULL,
  NULL,

  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
};
