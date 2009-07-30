/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009 Free Software Foundation, Inc.

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

#include <data/file-name.h>
#include <data/settings.h>
#include <libpspp/assertion.h>
#include <libpspp/compiler.h>
#include <libpspp/pool.h>
#include <libpspp/start-date.h>
#include <libpspp/version.h>
#include <output/chart-provider.h>
#include <output/chart.h>
#include <output/output.h>

#include "error.h"
#include "minmax.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* ASCII driver options: (defaults listed first)

   output-file="pspp.list"
   append=no|yes                If output-file exists, append to it?
   chart-files="pspp-#.png"     Name used for charts.
   chart-type=png|none

   paginate=on|off              Formfeeds are desired?
   tab-width=8                  Width of a tab; 0 to not use tabs.

   headers=on|off               Put headers at top of page?
   emphasis=bold|underline|none Style to use for emphasis.
   length=66|auto
   width=79|auto
   squeeze=off|on               Squeeze multiple newlines into exactly one.

   top-margin=2
   bottom-margin=2

   box[x]="strng"               Sets box character X (X in base 4: 0-3333).
   init="string"                Set initialization string.
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
    bool append;                /* Append if output-file already exists? */
    bool headers;		/* Print headers at top of page? */
    bool paginate;		/* Insert formfeeds? */
    bool squeeze_blank_lines;   /* Squeeze multiple blank lines into one? */
    enum emphasis_style emphasis; /* How to emphasize text. */
    int tab_width;		/* Width of a tab; 0 not to use tabs. */
    bool enable_charts;         /* Enable charts? */
    const char *chart_file_name; /* Name of files used for charts. */

    bool auto_width;            /* Use viewwidth as page width? */
    bool auto_length;           /* Use viewlength as page width? */
    int page_length;		/* Page length before subtracting margins. */
    int top_margin;		/* Top margin in lines. */
    int bottom_margin;		/* Bottom margin in lines. */

    char *box[LNS_COUNT];       /* Line & box drawing characters. */
    char *init;                 /* Device initialization string. */

    /* Internal state. */
    char *file_name;            /* Output file name. */
    FILE *file;                 /* Output file. */
    bool reported_error;        /* Reported file open error? */
    int page_number;		/* Current page number. */
    struct line *lines;         /* Page content. */
    int line_cap;               /* Number of lines allocated. */
    int chart_cnt;              /* Number of charts so far. */
  };

static void ascii_flush (struct outp_driver *);
static int get_default_box_char (size_t idx);
static bool update_page_size (struct outp_driver *, bool issue_error);
static bool handle_option (void *this, const char *key,
                           const struct string *val);

static bool
ascii_open_driver (const char *name, int types, struct substring options)
{
  struct outp_driver *this;
  struct ascii_driver_ext *x;
  int i;

  this = outp_allocate_driver (&ascii_class, name, types);
  this->width = 79;
  this->font_height = 1;
  this->prop_em_width = 1;
  this->fixed_width = 1;
  for (i = 0; i < OUTP_L_COUNT; i++)
    this->horiz_line_width[i] = this->vert_line_width[i] = i != OUTP_L_NONE;

  this->ext = x = pool_create_container (struct ascii_driver_ext, pool);
  x->append = false;
  x->headers = true;
  x->paginate = true;
  x->squeeze_blank_lines = false;
  x->emphasis = EMPH_BOLD;
  x->tab_width = 8;
  x->chart_file_name = pool_strdup (x->pool, "pspp-#.png");
  x->enable_charts = true;
  x->auto_width = false;
  x->auto_length = false;
  x->page_length = 66;
  x->top_margin = 2;
  x->bottom_margin = 2;
  for (i = 0; i < LNS_COUNT; i++)
    x->box[i] = NULL;
  x->init = NULL;
  x->file_name = pool_strdup (x->pool, "pspp.list");
  x->file = NULL;
  x->reported_error = false;
  x->page_number = 0;
  x->lines = NULL;
  x->line_cap = 0;
  x->chart_cnt = 1;

  if (!outp_parse_options (this->name, options, handle_option, this))
    goto error;

  if (!update_page_size (this, true))
    goto error;

  for (i = 0; i < LNS_COUNT; i++)
    if (x->box[i] == NULL)
      {
        char s[2];
        s[0] = get_default_box_char (i);
        s[1] = '\0';
        x->box[i] = pool_strdup (x->pool, s);
      }

  outp_register_driver (this);

  return true;

 error:
  pool_destroy (x->pool);
  outp_free_driver (this);
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

/* Re-calculates the page width and length based on settings,
   margins, and, if "auto" is set, the size of the user's
   terminal window or GUI output window. */
static bool
update_page_size (struct outp_driver *this, bool issue_error)
{
  struct ascii_driver_ext *x = this->ext;
  int margins = x->top_margin + x->bottom_margin + 1 + (x->headers ? 3 : 0);

  if (x->auto_width)
    this->width = settings_get_viewwidth ();
  if (x->auto_length)
    x->page_length = settings_get_viewlength ();

  this->length = x->page_length - margins;

  if (this->width < 59 || this->length < 15)
    {
      if (issue_error)
        error (0, 0,
               _("ascii: page excluding margins and headers "
                 "must be at least 59 characters wide by 15 lines long, but "
                 "as configured is only %d characters by %d lines"),
             this->width, this->length);
      if (this->width < 59)
        this->width = 59;
      if (this->length < 15)
        {
          this->length = 15;
          x->page_length = this->length + margins;
        }
      return false;
    }

  return true;
}

static bool
ascii_close_driver (struct outp_driver *this)
{
  struct ascii_driver_ext *x = this->ext;

  ascii_flush (this);
  pool_detach_file (x->pool, x->file);
  pool_destroy (x->pool);

  return true;
}

/* Generic option types. */
enum
  {
    boolean_arg,
    emphasis_arg,
    nonneg_int_arg,
    page_size_arg,
    string_arg
  };

static const struct outp_option option_tab[] =
  {
    {"headers", boolean_arg, 0},
    {"paginate", boolean_arg, 1},
    {"squeeze", boolean_arg, 2},
    {"append", boolean_arg, 3},

    {"emphasis", emphasis_arg, 0},

    {"length", page_size_arg, 0},
    {"width", page_size_arg, 1},

    {"top-margin", nonneg_int_arg, 0},
    {"bottom-margin", nonneg_int_arg, 1},
    {"tab-width", nonneg_int_arg, 2},

    {"output-file", string_arg, 0},
    {"chart-files", string_arg, 1},
    {"chart-type", string_arg, 2},
    {"init", string_arg, 3},

    {NULL, 0, 0},
  };

static bool
handle_option (void *this_, const char *key,
               const struct string *val)
{
  struct outp_driver *this = this_;
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
    case page_size_arg:
      {
	char *tail;
	int arg;

        if (ss_equals_case (ds_ss (val), ss_cstr ("auto")))
          {
            if (!(this->device & OUTP_DEV_SCREEN))
              {
                /* We only let `screen' devices have `auto'
                   length or width because output to such devices
                   is flushed before each new command.  Resizing
                   a device in the middle of output seems like a
                   bad idea. */
                error (0, 0, _("ascii: only screen devices may have `auto' "
                               "length or width"));
              }
            else if (subcat == 0)
              x->auto_length = true;
            else
              x->auto_width = true;
          }
        else
          {
            errno = 0;
            arg = strtol (value, &tail, 0);
            if (arg < 1 || errno == ERANGE || *tail)
              {
                error (0, 0, _("ascii: positive integer required as "
                               "`%s' value"),
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
      }
      break;
    case emphasis_arg:
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
          case 3:
            x->append = setting;
            break;
	  default:
	    NOT_REACHED ();
	  }
      }
      break;
    case string_arg:
      switch (subcat)
        {
        case 0:
          x->file_name = pool_strdup (x->pool, value);
          break;
        case 1:
          if (ds_find_char (val, '#') != SIZE_MAX)
            x->chart_file_name = pool_strdup (x->pool, value);
          else
            error (0, 0, _("`chart-files' value must contain `#'"));
          break;
        case 2:
          if (!strcmp (value, "png"))
            x->enable_charts = true;
          else if (!strcmp (value, "none"))
            x->enable_charts = false;
          else
            {
              error (0, 0,
                     _("ascii: `png' or `none' expected for `chart-type'"));
              return false;
            }
          break;
        case 3:
          x->init = pool_strdup (x->pool, value);
          break;
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

  update_page_size (this, false);

  if (x->file == NULL)
    {
      x->file = fn_open (x->file_name, x->append ? "a" : "w");
      if (x->file != NULL)
        {
          pool_attach_file (x->pool, x->file);
          if (x->init != NULL)
            fputs (x->init, x->file);
        }
      else
        {
          /* Report the error to the user and complete
             initialization.  If we do not finish initialization,
             then calls to other driver functions will segfault
             later.  It would be better to simply drop the driver
             entirely, but we do not have a convenient mechanism
             for this (yet). */
          if (!x->reported_error)
            error (0, errno, _("ascii: opening output file \"%s\""),
                   x->file_name);
          x->reported_error = true;
        }
    }

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

/* Divides the text T->S into lines of width T->H.  Sets *WIDTH
   to the maximum width of a line and *HEIGHT to the number of
   lines, if those arguments are non-null.  Actually draws the
   text if DRAW is true. */
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

  if (x->file == NULL)
    return;

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

/* Flushes all output to the user and lets the user deal with it.
   This is applied only to output drivers that are designated as
   "screen" drivers that the user is interacting with in real
   time. */
static void
ascii_flush (struct outp_driver *this)
{
  struct ascii_driver_ext *x = this->ext;
  if (x->file != NULL)
    {
      if (fn_close (x->file_name, x->file) != 0)
        error (0, errno, _("ascii: closing output file \"%s\""),
               x->file_name);
      pool_detach_file (x->pool, x->file);
      x->file = NULL;
    }
}

static void
ascii_output_chart (struct outp_driver *this, const struct chart *chart)
{
  struct ascii_driver_ext *x = this->ext;
  struct outp_text t;
  char *file_name;
  char *text;

  /* Draw chart into separate file */
  file_name = chart_draw_png (chart, x->chart_file_name, x->chart_cnt++);

  /* Mention chart in output.
     First advance current position. */
  if (!this->page_open)
    outp_open_page (this);
  else
    {
      this->cp_y++;
      if (this->cp_y >= this->length)
        {
          outp_close_page (this);
          outp_open_page (this);
        }
    }

  /* Then write the text. */
  text = xasprintf ("See %s for a chart.", file_name);
  t.font = OUTP_FIXED;
  t.justification = OUTP_LEFT;
  t.string = ss_cstr (text);
  t.h = this->width;
  t.v = 1;
  t.x = 0;
  t.y = this->cp_y;
  ascii_text_draw (this, &t);
  this->cp_y++;

  free (file_name);
  free (text);
}

const struct outp_class ascii_class =
{
  "ascii",
  0,

  ascii_open_driver,
  ascii_close_driver,

  ascii_open_page,
  ascii_close_page,
  ascii_flush,

  ascii_output_chart,

  NULL,                         /* submit */

  ascii_line,
  ascii_text_metrics,
  ascii_text_draw,
};
