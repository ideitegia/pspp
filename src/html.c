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

/* This #if encloses the rest of the file. */
#if !NO_HTML

#include <config.h>
#include <assert.h>
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
#include "htmlP.h"
#include "output.h"
#include "som.h"
#include "tab.h"
#include "version.h"

/* Prototypes. */
static int postopen (struct file_ext *);
static int preclose (struct file_ext *);

int
html_open_global (struct outp_class *this unused)
{
  return 1;
}

int
html_close_global (struct outp_class *this unused)
{
  return 1;
}

int
html_preopen_driver (struct outp_driver *this)
{
  struct html_driver_ext *x;

  assert (this->driver_open == 0);
  msg (VM (1), _("HTML driver initializing as `%s'..."), this->name);

  this->ext = x = xmalloc (sizeof *x);
  this->res = 0;
  this->horiz = this->vert = 0;
  this->width = this->length = 0;

  this->cp_x = this->cp_y = 0;

  x->prologue_fn = NULL;

  x->file.filename = NULL;
  x->file.mode = "w";
  x->file.file = NULL;
  x->file.sequence_no = &x->sequence_no;
  x->file.param = this;
  x->file.postopen = postopen;
  x->file.preclose = preclose;

  x->sequence_no = 0;

  return 1;
}

int
html_postopen_driver (struct outp_driver *this)
{
  struct html_driver_ext *x = this->ext;

  assert (this->driver_open == 0);
  if (NULL == x->file.filename)
    x->file.filename = xstrdup ("pspp.html");
	
  if (x->prologue_fn == NULL)
    x->prologue_fn = xstrdup ("html-prologue");

  msg (VM (2), _("%s: Initialization complete."), this->name);
  this->driver_open = 1;

  return 1;
}

int
html_close_driver (struct outp_driver *this)
{
  struct html_driver_ext *x = this->ext;

  assert (this->driver_open);
  msg (VM (2), _("%s: Beginning closing..."), this->name);
  fn_close_ext (&x->file);
  free (x->prologue_fn);
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

/* All the options that the HTML driver supports. */
static struct outp_option option_tab[] =
{
  /* *INDENT-OFF* */
  {"output-file",		1,		0},
  {"prologue-file",		string_arg,	0},
  {"", 0, 0},
  /* *INDENT-ON* */
};
static struct outp_option_info option_info;

void
html_option (struct outp_driver *this, const char *key, const struct string *val)
{
  struct html_driver_ext *x = this->ext;
  int cat, subcat;

  cat = outp_match_keyword (key, option_tab, &option_info, &subcat);
  switch (cat)
    {
    case 0:
      msg (SE, _("Unknown configuration parameter `%s' for HTML device "
	   "driver."), key);
      break;
    case 1:
      free (x->file.filename);
      x->file.filename = xstrdup (ds_value (val));
      break;
    case string_arg:
      {
	char **dest;
	switch (subcat)
	  {
	  case 0:
	    dest = &x->prologue_fn;
	    break;
	  default:
	    assert (0);
	  }
	if (*dest)
	  free (*dest);
	*dest = xstrdup (ds_value (val));
      }
      break;
#if __CHECKER__
    case 42000:
      assert (0);
#endif
    default:
      assert (0);
    }
}

/* Variables for the prologue. */
struct html_variable
  {
    const char *key;
    const char *value;
  };
  
static struct html_variable *html_var_tab;

/* Searches html_var_tab for a html_variable with key KEY, and returns
   the associated value. */
static const char *
html_get_var (const char *key)
{
  struct html_variable *v;

  for (v = html_var_tab; v->key; v++)
    if (!strcmp (key, v->key))
      return v->value;
  return NULL;
}

/* Writes the HTML prologue to file F. */
static int
postopen (struct file_ext *f)
{
  static struct html_variable dict[] =
    {
      {"generator", 0},
      {"date", 0},
      {"user", 0},
      {"host", 0},
      {"title", 0},
      {"subtitle", 0},
      {"source-file", 0},
      {0, 0},
    };
#if HAVE_UNISTD_H
  char host[128];
#endif
  time_t curtime;
  struct tm *loctime;

  struct outp_driver *this = f->param;
  struct html_driver_ext *x = this->ext;

  char *prologue_fn = fn_search_path (x->prologue_fn, config_path, NULL);
  FILE *prologue_file;

  char *buf = NULL;
  int buf_size = 0;

  if (prologue_fn == NULL)
    {
      msg (IE, _("Cannot find HTML prologue.  The use of `-vv' "
		 "on the command line is suggested as a debugging aid."));
      return 0;
    }

  msg (VM (1), _("%s: %s: Opening HTML prologue..."), this->name, prologue_fn);
  prologue_file = fopen (prologue_fn, "rb");
  if (prologue_file == NULL)
    {
      fclose (prologue_file);
      free (prologue_fn);
      msg (IE, "%s: %s", prologue_fn, strerror (errno));
      goto error;
    }

  dict[0].value = version;

  curtime = time (NULL);
  loctime = localtime (&curtime);
  dict[1].value = asctime (loctime);
  {
    char *cp = strchr (dict[1].value, '\n');
    if (cp)
      *cp = 0;
  }

  /* PORTME: Determine username, net address. */
#if HAVE_UNISTD_H
  dict[2].value = getenv ("LOGNAME");
  if (!dict[2].value)
    dict[2].value = getlogin ();
  if (!dict[2].value)
    dict[2].value = _("nobody");

  if (gethostname (host, 128) == -1)
    {
      if (errno == ENAMETOOLONG)
	host[127] = 0;
      else
	strcpy (host, _("nowhere"));
    }
  dict[3].value = host;
#else /* !HAVE_UNISTD_H */
  dict[2].value = _("nobody");
  dict[3].value = _("nowhere");
#endif /* !HAVE_UNISTD_H */

  dict[4].value = outp_title ? outp_title : "";
  dict[5].value = outp_subtitle ? outp_subtitle : "";

  getl_location (&dict[6].value, NULL);
  if (dict[6].value == NULL)
    dict[6].value = "<stdin>";

  html_var_tab = dict;
  while (-1 != getline (&buf, &buf_size, prologue_file))
    {
      char *buf2;
      int len;

      if (strstr (buf, "!!!"))
	continue;
      
      {
	char *cp = strstr (buf, "!title");
	if (cp)
	  {
	    if (outp_title == NULL)
	      continue;
	    else
	      *cp = '\0';
	  }
      }
      
      {
	char *cp = strstr (buf, "!subtitle");
	if (cp)
	  {
	    if (outp_subtitle == NULL)
	      continue;
	    else
	      *cp = '\0';
	  }
      }
      
      /* PORTME: Line terminator. */
      buf2 = fn_interp_vars (buf, html_get_var);
      len = strlen (buf2);
      fwrite (buf2, len, 1, f->file);
      if (buf2[len - 1] != '\n')
	putc ('\n', f->file);
      free (buf2);
    }
  if (ferror (f->file))
    msg (IE, _("Reading `%s': %s."), prologue_fn, strerror (errno));
  fclose (prologue_file);

  free (prologue_fn);
  free (buf);

  if (ferror (f->file))
    goto error;

  msg (VM (2), _("%s: HTML prologue read successfully."), this->name);
  return 1;

error:
  msg (VM (1), _("%s: Error reading HTML prologue."), this->name);
  return 0;
}

/* Writes the HTML epilogue to file F. */
static int
preclose (struct file_ext *f)
{
  fprintf (f->file,
	   "</BODY>\n"
	   "</HTML>\n"
	   "<!-- end of file -->\n");

  if (ferror (f->file))
    return 0;
  return 1;
}

int
html_open_page (struct outp_driver *this)
{
  struct html_driver_ext *x = this->ext;

  assert (this->driver_open && this->page_open == 0);
  x->sequence_no++;
  if (!fn_open_ext (&x->file))
    {
      if (errno)
	msg (ME, _("HTML output driver: %s: %s"), x->file.filename,
	     strerror (errno));
      return 0;
    }

  if (!ferror (x->file.file))
    this->page_open = 1;
  return !ferror (x->file.file);
}

int
html_close_page (struct outp_driver *this)
{
  struct html_driver_ext *x = this->ext;

  assert (this->driver_open && this->page_open);
  this->page_open = 0;
  return !ferror (x->file.file);
}

static void output_tab_table (struct outp_driver *, struct tab_table *);

void
html_submit (struct outp_driver *this, struct som_table *s)
{
  extern struct som_table_class tab_table_class;
  struct html_driver_ext *x = this->ext;
  
  assert (this->driver_open && this->page_open);
  if (x->sequence_no == 0 && !html_open_page (this))
    {
      msg (ME, _("Cannot open first page on HTML device %s."), this->name);
      return;
    }

  if (s->class == &tab_table_class)
    output_tab_table (this, (struct tab_table *) s->ext);
  else
    assert (0);
}

/* Emit HTML to FILE to change from *OLD_ATTR attributes to NEW_ATTR.
   Sets *OLD_ATTR to NEW_ATTR when done. */
static void
change_attributes (FILE *f, int *old_attr, int new_attr)
{
  if (*old_attr == new_attr)
    return;

  if (*old_attr & OUTP_F_B)
    fputs ("</B>", f);
  if (*old_attr & OUTP_F_I)
    fputs ("</I>", f);
  if (new_attr & OUTP_F_I)
    fputs ("<I>", f);
  if (new_attr & OUTP_F_B)
    fputs ("<B>", f);

  *old_attr = new_attr;
}

/* Write string S of length LEN to file F, escaping characters as
   necessary for HTML. */
static void
escape_string (FILE *f, char *s, int len)
{
  char *ep = &s[len];
  char *bp, *cp;
  int attr = 0;

  for (bp = cp = s; bp < ep; bp = cp)
    {
      while (cp < ep && *cp != '&' && *cp != '<' && *cp != '>' && *cp)
	cp++;
      if (cp > bp)
	fwrite (bp, 1, cp - bp, f);
      if (cp < ep)
	switch (*cp++)
	  {
	  case '&':
	    fputs ("&amp;", f);
	    break;
	  case '<':
	    fputs ("&lt;", f);
	    break;
	  case '>':
	    fputs ("&gt;", f);
	    break;
	  case 0:
	    break;
	  default:
	    assert (0);
	  }
    }

  if (attr)
    change_attributes (f, &attr, 0);
}
  
/* Write table T to THIS output driver. */
static void
output_tab_table (struct outp_driver *this, struct tab_table *t)
{
  struct html_driver_ext *x = this->ext;
  
  tab_hit++;

  if (t->nr == 1 && t->nc == 1)
    {
      fputs ("<P>", x->file.file);
      if (!ls_empty_p (t->cc))
	escape_string (x->file.file, ls_value (t->cc), ls_length (t->cc));
      fputs ("</P>\n", x->file.file);
      
      return;
    }

  fputs ("<TABLE BORDER=1>\n", x->file.file);
  
  if (!ls_empty_p (&t->title))
    {
      fprintf (x->file.file, "  <TR>\n    <TH COLSPAN=%d>", t->nc);
      escape_string (x->file.file, ls_value (&t->title),
		     ls_length (&t->title));
      fputs ("</TH>\n  </TR>\n", x->file.file);
    }
  
  {
    int r;
    struct len_string *cc = t->cc;
    unsigned char *ct = t->ct;

    for (r = 0; r < t->nr; r++)
      {
	int c;
	
	fputs ("  <TR>\n", x->file.file);
	for (c = 0; c < t->nc; c++, cc++, ct++)
	  {
	    int tag;
	    char header[128];
	    char *cp;

	    if ((*ct & TAB_JOIN)
		&& ((struct tab_joined_cell *) ls_value (cc))->hit == tab_hit)
	      continue;

	    if (r < t->t || r >= t->nr - t->b
		|| c < t->l || c >= t->nc - t->r)
	      tag = 'H';
	    else
	      tag = 'D';
	    cp = stpcpy (header, "    <T");
	    *cp++ = tag;
	    
	    switch (*ct & TAB_ALIGN_MASK)
	      {
	      case TAB_RIGHT:
		cp = stpcpy (cp, " ALIGN=RIGHT");
		break;
	      case TAB_LEFT:
		break;
	      case TAB_CENTER:
		cp = stpcpy (cp, " ALIGN=CENTER");
		break;
	      default:
		assert (0);
	      }

	    if (*ct & TAB_JOIN)
	      {
		struct tab_joined_cell *j =
		  (struct tab_joined_cell *) ls_value (cc);
		j->hit = tab_hit;
		
		if (j->x2 - j->x1 > 1)
		  cp = spprintf (cp, " COLSPAN=%d", j->x2 - j->x1);
		if (j->y2 - j->y1 > 1)
		  cp = spprintf (cp, " ROWSPAN=%d", j->y2 - j->y1);
	      }
	    
	    strcpy (cp, ">");
	    fputs (header, x->file.file);
	    
	    {
	      char *s = ls_value (cc);
	      size_t l = ls_length (cc);

	      while (l && isspace ((unsigned char) *s))
		{
		  l--;
		  s++;
		}
	      
	      escape_string (x->file.file, s, l);
	    }

	    fprintf (x->file.file, "</T%c>\n", tag);
	  }
	fputs ("  </TR>\n", x->file.file);
      }
  }
	      
  fputs ("</TABLE>\n\n", x->file.file);
}

/* HTML driver class. */
struct outp_class html_class =
{
  "html",
  0xfaeb,
  1,

  html_open_global,
  html_close_global,
  NULL,

  html_preopen_driver,
  html_option,
  html_postopen_driver,
  html_close_driver,

  html_open_page,
  html_close_page,

  html_submit,

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

#endif /* !NO_HTML */

