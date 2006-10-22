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

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include <data/file-name.h>
#include <libpspp/alloc.h>
#include <libpspp/assertion.h>
#include <libpspp/compiler.h>
#include <libpspp/pool.h>
#include <libpspp/start-date.h>
#include <libpspp/version.h>

#include "chart.h"
#include "error.h"
#include "minmax.h"
#include "output.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* ASCII driver options: (defaults listed first)

   output-file="pspp.list"
   paginate=on|off              Formfeeds are desired?
   tab-width=8                  Width of a tab; 0 to not use tabs.
   
   headers=on|off               Put headers at top of page?
   emphasis=bold|underline|none Style to use for emphasis.
   length=66
   width=130
   squeeze=off|on               Squeeze multiple newlines into exactly one.

   top-margin=2
   bottom-margin=2

   box[x]="strng"               Sets box character X (X in base 4: 0-3333).
 */

/* Disable messages by failed range checks. */
/*#define SUPPRESS_WARNINGS 1 */

/* Line styles bit shifts. */
enum
  {
    LNS_TOP = 0,
    LNS_LEFT = 2,
    LNS_BOTTOM = 4,
    LNS_RIGHT = 6,

    LNS_COUNT = 256
  };

/* Character attributes. */
#define ATTR_EMPHASIS   0x100   /* Bold-face. */
#define ATTR_BOX        0x200   /* Line drawing character. */

/* A line of text. */
struct line 
  {
    unsigned short *chars;      /* Characters and attributes. */
    int char_cnt;               /* Length. */
    int char_cap;               /* Allocated bytes. */
  };

/* How to emphasize text. */
enum emphasis_style 
  {
    EMPH_BOLD,                  /* Overstrike for bold. */
    EMPH_UNDERLINE,             /* Overstrike for underlining. */
    EMPH_NONE                   /* No emphasis. */
  };

/* ASCII output driver extension record. */
struct ascii_driver_ext
  {
    struct pool *pool;

    /* User parameters. */
    bool headers;		/* Print headers at top of page? */
    bool paginate;		/* Insert formfeeds? */
    bool squeeze_blank_lines;   /* Squeeze multiple blank lines into one? */
    enum emphasis_style emphasis; /* How to emphasize text. */
    int tab_width;		/* Width of a tab; 0 not to use tabs. */

    int page_length;		/* Page length before subtracting margins. */
    int top_margin;		/* Top margin in lines. */
    int bottom_margin;		/* Bottom margin in lines. */

    char *box[LNS_COUNT];       /* Line & box drawing characters. */

    /* Internal state. */
    char *file_name;            /* Output file name. */
    FILE *file;                 /* Output file. */
    int page_number;		/* Current page number. */
    struct line *lines;         /* Page content. */
    int line_cap;               /* Number of lines allocated. */
  };

static int get_default_box_char (size_t idx);
static bool handle_option (struct outp_driver *this, const char *key,
                           const struct string *val);

static bool
ascii_open_driver (struct outp_driver *this, struct substring options)
{
  struct ascii_driver_ext *x;
  int i;

  this->width = 79;
  this->font_height = 1;
  this->prop_em_width = 1;
  this->fixed_width = 1;
  for (i = 0; i < OUTP_L_COUNT; i++)
    this->horiz_line_width[i] = this->vert_line_width[i] = i != OUTP_L_NONE;

  this->ext = x = pool_create_container (struct ascii_driver_ext, pool);
  x->headers = true;
  x->paginate = true;
  x->squeeze_blank_lines = false;
  x->emphasis = EMPH_BOLD;
  x->tab_width = 8;
  x->page_length = 66;
  x->top_margin = 2;
  x->bottom_margin = 2;
  for (i = 0; i < LNS_COUNT; i++)
    x->box[i] = NULL;
  x->file_name = pool_strdup (x->pool, "pspp.list");
  x->file = NULL;
  x->page_number = 0;
  x->lines = NULL;
  x->line_cap = 0;

  if (!outp_parse_options (options, handle_option, this))
    goto error;

  x->file = pool_fopen (x->pool, x->file_name, "w");
  if (x->file == NULL)
    {
      error (0, errno, _("ascii: opening output file \"%s\""), x->file_name);
      goto error;
    }

  this->length = x->page_length - x->top_margin - x->bottom_margin - 1;
  if (x->headers)
    this->length -= 3;
  
  if (this->width < 59 || this->length < 15)
    {
      error (0, 0,
             _("ascii: page excluding margins and headers "
               "must be at least 59 characters wide by 15 lines long, but as "
               "configured is only %d characters by %d lines"),
             this->width, this->length);
      return false;
    }

  for (i = 0; i < LNS_COUNT; i++)
    if (x->box[i] == NULL) 
      {
        char s[2];
        s[0] = get_default_box_char (i);
        s[1] = '\0';
        x->box[i] = pool_strdup (x->pool, s);
      }
  
  return true;

 error:
  pool_destroy (x->pool);
  return false;
}

static int
get_default_box_char (size_t idx)
{
  /* Disassemble IDX into components. */
  unsigned top = (idx >> LNS_TOP) & 3;
  unsigned left = (idx >> LNS_LEFT) & 3;
  unsigned bottom = (idx >> LNS_BOTTOM) & 3;
  unsigned right = (idx >> LNS_RIGHT) & 3;

  /* Reassemble components into nibbles in the order TLBR.
     This makes it easy to read the case labels. */
  unsigned value = (top << 12) | (left << 8) | (bottom << 4) | (right << 0);
  switch (value)
    {
    case 0x0000:
      return ' ';

    case 0x0100: case 0x0101: case 0x0001:
      return '-';

    case 0x1000: case 0x1010: case 0x0010:
      return '|';

    case 0x0300: case 0x0303: case 0x0003:
    case 0x0200: case 0x0202: case 0x0002:
      return '=';

    default:
      return left > 1 || top > 1 || right > 1 || bottom > 1 ? '#' : '+';
    }
}

static bool
ascii_close_driver (struct outp_driver *this)
{
  struct ascii_driver_ext *x = this->ext;
  
  if (fn_close (x->file_name, x->file) != 0)
    error (0, errno, _("ascii: closing output file \"%s\""), x->file_name);
  pool_detach_file (x->pool, x->file);
  pool_destroy (x->pool);
  
  return true;
}

/* Generic option types. */
enum
  {
    boolean_arg,
    string_arg,
    nonneg_int_arg,
    pos_int_arg,
    output_file_arg
  };

static const struct outp_option option_tab[] =
  {
    {"headers", boolean_arg, 0},
    {"paginate", boolean_arg, 1},
    {"squeeze", boolean_arg, 2},

    {"emphasis", string_arg, 3},

    {"output-file", output_file_arg, 0},

    {"length", pos_int_arg, 0},
    {"width", pos_int_arg, 1},

    {"top-margin", nonneg_int_arg, 0},
    {"bottom-margin", nonneg_int_arg, 1},
    {"tab-width", nonneg_int_arg, 2},

    {NULL, 0, 0},
  };

static bool
handle_option (struct outp_driver *this, const char *key,
               const struct string *val)
{
  struct ascii_driver_ext *x = this->ext;
  int subcat;
  const char *value;

  value = ds_cstr (val);
  if (!strncmp (key, "box[", 4))
    {
      char *tail;
      int indx = strtol (&key[4], &tail, 4);
      if (*tail != ']' || indx < 0 || indx > LNS_COUNT)
	{
	  error (0, 0, _("ascii: bad index value for `box' key: syntax "
                         "is box[INDEX], 0 <= INDEX < %d decimal, with INDEX "
                         "expressed in base 4"),
                 LNS_COUNT);
	  return false;
	}
      if (x->box[indx] != NULL)
	error (0, 0, _("ascii: multiple values for %s"), key);
      x->box[indx] = pool_strdup (x->pool, value);
      return true;
    }

  switch (outp_match_keyword (key, option_tab, &subcat))
    {
    case -1:
      error (0, 0, _("ascii: unknown parameter `%s'"), key);
      break;
    case output_file_arg:
      x->file_name = pool_strdup (x->pool, value);
      break;
    case pos_int_arg:
      {
	char *tail;
	int arg;

	errno = 0;
	arg = strtol (value, &tail, 0);
	if (arg < 1 || errno == ERANGE || *tail)
	  {
	    error (0, 0, _("ascii: positive integer required as `%s' value"),
                   key);
	    break;
	  }
	switch (subcat)
	  {
	  case 0:
	    x->page_length = arg;
	    break;
	  case 1:
	    this->width = arg;
	    break;
	  default:
	    NOT_REACHED ();
	  }
      }
      break;
    case string_arg:
      if (!strcmp (value, "bold"))
        x->emphasis = EMPH_BOLD;
      else if (!strcmp (value, "underline"))
        x->emphasis = EMPH_UNDERLINE;
      else if (!strcmp (value, "none"))
        x->emphasis = EMPH_NONE;
      else 
        error (0, 0,
               _("ascii: `emphasis' value must be `bold', "
                 "`underline', or `none'"));
      break;
    case nonneg_int_arg:
      {
	char *tail;
	int arg;

	errno = 0;
	arg = strtol (value, &tail, 0);
	if (arg < 0 || errno == ERANGE || *tail)
	  {
	    error (0, 0,
                   _("ascii: zero or positive integer required as `%s' value"),
                   key);
	    break;
	  }
	switch (subcat)
	  {
	  case 0:
	    x->top_margin = arg;
	    break;
	  case 1:
	    x->bottom_margin = arg;
	    break;
	  case 2:
	    x->tab_width = arg;
	    break;
	  default:
	    NOT_REACHED ();
	  }
      }
      break;
    case boolean_arg:
      {
	bool setting;
	if (!strcmp (value, "on") || !strcmp (value, "true")
	    || !strcmp (value, "yes") || atoi (value))
	  setting = true;
	else if (!strcmp (value, "off") || !strcmp (value, "false")
		 || !strcmp (value, "no") || !strcmp (value, "0"))
	  setting = false;
	else
	  {
	    error (0, 0, _("ascii: boolean value expected for `%s'"), key);
	    return false;
	  }
	switch (subcat)
	  {
	  case 0:
	    x->headers = setting;
	    break;
	  case 1:
	    x->paginate = setting;
	    break;
          case 2:
            x->squeeze_blank_lines = setting;
            break;
	  default:
	    NOT_REACHED ();
	  }
      }
      break;
    default:
      NOT_REACHED ();
    }

  return true;
}

static void
ascii_open_page (struct outp_driver *this)
{
  struct ascii_driver_ext *x = this->ext;
  int i;

  x->page_number++;

  if (this->length > x->line_cap)
    {
      x->lines = pool_nrealloc (x->pool,
                                x->lines, this->length, sizeof *x->lines);
      for (i = x->line_cap; i < this->length; i++) 
        {
          struct line *line = &x->lines[i];
          line->chars = NULL;
          line->char_cap = 0;
        }
      x->line_cap = this->length;
    }

  for (i = 0; i < this->length; i++)
    x->lines[i].char_cnt = 0;
}

/* Ensures that at least the first LENGTH characters of line Y in
   THIS driver identified X have been cleared out. */
static inline void
expand_line (struct outp_driver *this, int y, int length)
{
  struct ascii_driver_ext *ext = this->ext;
  struct line *line = &ext->lines[y];
  if (line->char_cnt < length) 
    {
      int x;
      if (line->char_cap < length) 
        {
          line->char_cap = MIN (length * 2, this->width);
          line->chars = pool_nrealloc (ext->pool,
                                       line->chars,
                                       line->char_cap, sizeof *line->chars); 
        }
      for (x = line->char_cnt; x < length; x++)
        line->chars[x] = ' ';
      line->char_cnt = length; 
    }
}

static void
ascii_line (struct outp_driver *this, 
            int x0, int y0, int x1, int y1,
            enum outp_line_style top, enum outp_line_style left,
            enum outp_line_style bottom, enum outp_line_style right)
{
  struct ascii_driver_ext *ext = this->ext;
  int y;
  unsigned short value;

  assert (this->page_open);
#if DEBUGGING
  if (x0 < 0 || x1 > this->width || y0 < 0 || y1 > this->length)
    {
#if !SUPPRESS_WARNINGS
      printf (_("ascii: bad line (%d,%d)-(%d,%d) out of (%d,%d)\n"),
	      x0, y0, x1, y1, this->width, this->length);
#endif
      return;
    }
#endif

  value = ((left << LNS_LEFT) | (right << LNS_RIGHT)
           | (top << LNS_TOP) | (bottom << LNS_BOTTOM) | ATTR_BOX);
  for (y = y0; y < y1; y++) 
    {
      int x;

      expand_line (this, y, x1);
      for (x = x0; x < x1; x++)
        ext->lines[y].chars[x] = value; 
    }
}

static void
text_draw (struct outp_driver *this,
           enum outp_font font,
           int x, int y,
           enum outp_justification justification, int width,
           const char *string, size_t length)
{
  struct ascii_driver_ext *ext = this->ext;
  unsigned short attr = font == OUTP_EMPHASIS ? ATTR_EMPHASIS : 0;

  int line_len;

  switch (justification)
    {
    case OUTP_LEFT:
      break;
    case OUTP_CENTER:
      x += (width - length + 1) / 2;
      break;
    case OUTP_RIGHT:
      x += width - length;
      break;
    default:
      NOT_REACHED ();
    }

  if (y >= this->length || x >= this->width)
    return;

  if (x + length > this->width)
    length = this->width - x;

  line_len = x + length;

  expand_line (this, y, line_len);
  while (length-- > 0)
    ext->lines[y].chars[x++] = *string++ | attr;
}

/* Divides the text T->S into lines of width T->H.  Sets T->V to the
   number of lines necessary.  Actually draws the text if DRAW is
   true. */
static void
delineate (struct outp_driver *this, const struct outp_text *text, bool draw,
           int *width, int *height)
{
  int max_width;
  int height_left;

  const char *cp = ss_data (text->string);

  max_width = 0;
  height_left = text->v;

  while (height_left > 0)
    {
      size_t chars_left;
      size_t line_len;
      const char *end;

      /* Initially the line is up to text->h characters long. */
      chars_left = ss_end (text->string) - cp;
      if (chars_left == 0)
        break;
      line_len = MIN (chars_left, text->h);

      /* A new-line terminates the line prematurely. */
      end = memchr (cp, '\n', line_len);
      if (end != NULL)
        line_len = end - cp;

      /* Don't cut off words if it can be avoided. */
      if (cp + line_len < ss_end (text->string)) 
        {
          size_t space_len = line_len;
          while (space_len > 0 && !isspace ((unsigned char) cp[space_len]))
            space_len--;
          if (space_len > 0)
            line_len = space_len;
        }
      
      /* Draw text. */
      if (draw)
        text_draw (this,
                   text->font,
                   text->x, text->y + (text->v - height_left),
                   text->justification, text->h,
                   cp, line_len);

      /* Update. */
      height_left--;
      if (line_len > max_width)
        max_width = line_len;

      /* Next line. */
      cp += line_len;
      if (cp < ss_end (text->string) && isspace ((unsigned char) *cp))
        cp++;
    }

  if (width != NULL)
    *width = max_width;
  if (height != NULL)
    *height = text->v - height_left;
}

static void
ascii_text_metrics (struct outp_driver *this, const struct outp_text *t,
                    int *width, int *height)
{
  delineate (this, t, false, width, height);
}

static void
ascii_text_draw (struct outp_driver *this, const struct outp_text *t)
{
  assert (this->page_open);
  delineate (this, t, true, NULL, NULL);
}


/* ascii_close_page () and support routines. */

/* Writes the LENGTH characters in S to OUT.  */
static void
output_line (struct outp_driver *this, const struct line *line,
             struct string *out)
{
  struct ascii_driver_ext *ext = this->ext;
  const unsigned short *s = line->chars;
  size_t length;

  for (length = line->char_cnt; length-- > 0; s++)
    if (*s & ATTR_BOX)
      ds_put_cstr (out, ext->box[*s & 0xff]);
    else
      {
        if (*s & ATTR_EMPHASIS) 
          {
            if (ext->emphasis == EMPH_BOLD)
              {
                ds_put_char (out, *s);
                ds_put_char (out, '\b'); 
              }
            else if (ext->emphasis == EMPH_UNDERLINE)
              ds_put_cstr (out, "_\b"); 
          }
        ds_put_char (out, *s);
      }
}

static void
append_lr_justified (struct string *out, int width,
                     const char *left, const char *right)
{
  ds_put_char_multiple (out, ' ', width);
  if (left != NULL) 
    {
      size_t length = MIN (strlen (left), width);
      memcpy (ds_end (out) - width, left, length); 
    }
  if (right != NULL)
    {
      size_t length = MIN (strlen (right), width);
      memcpy (ds_end (out) - length, right, length);
    }
  ds_put_char (out, '\n');
}

static void
dump_output (struct outp_driver *this, struct string *out) 
{
  struct ascii_driver_ext *x = this->ext;
  fwrite (ds_data (out), ds_length (out), 1, x->file);
  ds_clear (out);
}

static void
ascii_close_page (struct outp_driver *this)
{
  struct ascii_driver_ext *x = this->ext;
  struct string out;
  int line_num;
 
  ds_init_empty (&out);
 
  ds_put_char_multiple (&out, '\n', x->top_margin);
  if (x->headers)
    {
      char *r1, *r2;
 
      r1 = xasprintf (_("%s - Page %d"), get_start_date (), x->page_number);
      r2 = xasprintf ("%s - %s" , version, host_system);
 
      append_lr_justified (&out, this->width, outp_title, r1);
      append_lr_justified (&out, this->width, outp_subtitle, r2);
      ds_put_char (&out, '\n');
 
      free (r1);
      free (r2);
    }
  dump_output (this, &out);
 
  for (line_num = 0; line_num < this->length; line_num++)
    {
 
      /* Squeeze multiple blank lines into a single blank line if
         requested. */
      if (x->squeeze_blank_lines) 
        {
          if (line_num >= x->line_cap)
            break;
          if (line_num > 0
              && x->lines[line_num].char_cnt == 0
              && x->lines[line_num - 1].char_cnt == 0)
            continue; 
        }
 
      if (line_num < x->line_cap) 
        output_line (this, &x->lines[line_num], &out); 
      ds_put_char (&out, '\n');
      dump_output (this, &out);
    }
 
  ds_put_char_multiple (&out, '\n', x->bottom_margin);
  if (x->paginate) 
    ds_put_char (&out, '\f');

  dump_output (this, &out);
  ds_destroy (&out);
}

static void
ascii_chart_initialise (struct outp_driver *d UNUSED, struct chart *ch)
{
  error (0, 0, _("ascii: charts are unsupported by this driver"));
  ch->lp = 0;
}

static void 
ascii_chart_finalise (struct outp_driver *d UNUSED, struct chart *ch UNUSED)
{
  
}

const struct outp_class ascii_class =
{
  "ascii",
  0,

  ascii_open_driver,
  ascii_close_driver,

  ascii_open_page,
  ascii_close_page,

  NULL,

  ascii_line,
  ascii_text_metrics,
  ascii_text_draw,

  ascii_chart_initialise,
  ascii_chart_finalise
};
