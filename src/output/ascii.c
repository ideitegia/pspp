/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009, 2010, 2011 Free Software Foundation, Inc.

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
#include <signal.h>
#include <unistd.h>
#include <unilbrk.h>
#include <unistr.h>
#include <uniwidth.h>

#include "data/file-name.h"
#include "data/settings.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/message.h"
#include "libpspp/start-date.h"
#include "libpspp/string-map.h"
#include "libpspp/version.h"
#include "output/ascii.h"
#include "output/cairo.h"
#include "output/chart-item-provider.h"
#include "output/driver-provider.h"
#include "output/message-item.h"
#include "output/options.h"
#include "output/render.h"
#include "output/tab.h"
#include "output/table-item.h"
#include "output/text-item.h"

#include "gl/error.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* This file uses TABLE_HORZ and TABLE_VERT enough to warrant abbreviating. */
#define H TABLE_HORZ
#define V TABLE_VERT

#define N_BOX (RENDER_N_LINES * RENDER_N_LINES \
               * RENDER_N_LINES * RENDER_N_LINES)

static const ucs4_t ascii_box_chars[N_BOX] =
  {
    ' ', '|', '#',
    '-', '+', '#',
    '=', '#', '#',
    '|', '|', '#',
    '+', '+', '#',
    '#', '#', '#',
    '#', '#', '#',
    '#', '#', '#',
    '#', '#', '#',
    '-', '+', '#',
    '-', '+', '#',
    '#', '#', '#',
    '+', '+', '#',
    '+', '+', '#',
    '#', '#', '#',
    '#', '#', '#',
    '#', '#', '#',
    '#', '#', '#',
    '=', '#', '#',
    '#', '#', '#',
    '=', '#', '#',
    '#', '#', '#',
    '#', '#', '#',
    '#', '#', '#',
    '#', '#', '#',
    '#', '#', '#',
    '#', '#', '#',
  };

static const ucs4_t unicode_box_chars[N_BOX] =
  {
    0x0020, 0x2575, 0x2551,
    0x2574, 0x256f, 0x255c,
    0x2550, 0x255b, 0x255d,
    0x2577, 0x2502, 0x2551,
    0x256e, 0x2524, 0x2562,
    0x2555, 0x2561, 0x2563,
    0x2551, 0x2551, 0x2551,
    0x2556, 0x2562, 0x2562,
    0x2557, 0x2563, 0x2563,
    0x2576, 0x2570, 0x2559,
    0x2500, 0x2534, 0x2568,
    0x2550, 0x2567, 0x2569,
    0x256d, 0x251c, 0x255f,
    0x252c, 0x253c, 0x256a,
    0x2564, 0x256a, 0x256c,
    0x2553, 0x255f, 0x255f,
    0x2565, 0x256b, 0x256b,
    0x2566, 0x256c, 0x256c,
    0x2550, 0x2558, 0x255a,
    0x2550, 0x2567, 0x2569,
    0x2550, 0x2567, 0x2569,
    0x2552, 0x255e, 0x2560,
    0x2564, 0x256a, 0x256c,
    0x2564, 0x256a, 0x256c,
    0x2554, 0x2560, 0x2560,
    0x2566, 0x256c, 0x256c,
  };

static inline int
make_box_index (int left, int right, int top, int bottom)
{
  return ((right * 3 + bottom) * 3 + left) * 3 + top;
}

/* A line of text. */
struct ascii_line
  {
    struct string s;            /* Content, in UTF-8. */
    size_t width;               /* Display width, in character positions. */
  };

/* How to emphasize text. */
enum emphasis_style
  {
    EMPH_BOLD,                  /* Overstrike for bold. */
    EMPH_UNDERLINE,             /* Overstrike for underlining. */
    EMPH_NONE                   /* No emphasis. */
  };

/* ASCII output driver. */
struct ascii_driver
  {
    struct output_driver driver;

    /* User parameters. */
    bool append;                /* Append if output file already exists? */
    bool headers;		/* Print headers at top of page? */
    bool paginate;		/* Insert formfeeds? */
    bool squeeze_blank_lines;   /* Squeeze multiple blank lines into one? */
    enum emphasis_style emphasis; /* How to emphasize text. */
    char *chart_file_name;      /* Name of files used for charts. */

    int width;                  /* Page width. */
    int length;                 /* Page length minus margins and header. */
    bool auto_width;            /* Use viewwidth as page width? */
    bool auto_length;           /* Use viewlength as page width? */

    int top_margin;		/* Top margin in lines. */
    int bottom_margin;		/* Bottom margin in lines. */

    const ucs4_t *box;          /* Line & box drawing characters. */

    /* Internal state. */
    char *command_name;
    char *title;
    char *subtitle;
    char *file_name;            /* Output file name. */
    FILE *file;                 /* Output file. */
    bool error;                 /* Output error? */
    int page_number;		/* Current page number. */
    struct ascii_line *lines;   /* Page content. */
    int allocated_lines;        /* Number of lines allocated. */
    int chart_cnt;              /* Number of charts so far. */
    int y;
  };

static const struct output_driver_class ascii_driver_class;

static void ascii_submit (struct output_driver *, const struct output_item *);

static int vertical_margins (const struct ascii_driver *);

static bool update_page_size (struct ascii_driver *, bool issue_error);
static int parse_page_size (struct driver_option *);

static void ascii_close_page (struct ascii_driver *);
static bool ascii_open_page (struct ascii_driver *);

static void ascii_draw_line (void *, int bb[TABLE_N_AXES][2],
                             enum render_line_style styles[TABLE_N_AXES][2]);
static void ascii_measure_cell_width (void *, const struct table_cell *,
                                      int *min, int *max);
static int ascii_measure_cell_height (void *, const struct table_cell *,
                                      int width);
static void ascii_draw_cell (void *, const struct table_cell *,
                             int bb[TABLE_N_AXES][2],
                             int clip[TABLE_N_AXES][2]);

static struct ascii_driver *
ascii_driver_cast (struct output_driver *driver)
{
  assert (driver->class == &ascii_driver_class);
  return UP_CAST (driver, struct ascii_driver, driver);
}

static struct driver_option *
opt (struct output_driver *d, struct string_map *options, const char *key,
     const char *default_value)
{
  return driver_option_get (d, options, key, default_value);
}

static struct output_driver *
ascii_create (const char *file_name, enum settings_output_devices device_type,
              struct string_map *o)
{
  enum { BOX_ASCII, BOX_UNICODE } box;
  struct output_driver *d;
  struct ascii_driver *a;
  int paper_length;

  a = xzalloc (sizeof *a);
  d = &a->driver;
  output_driver_init (&a->driver, &ascii_driver_class, file_name, device_type);
  a->append = parse_boolean (opt (d, o, "append", "false"));
  a->headers = parse_boolean (opt (d, o, "headers", "false"));
  a->paginate = parse_boolean (opt (d, o, "paginate", "false"));
  a->squeeze_blank_lines = parse_boolean (opt (d, o, "squeeze", "true"));
  a->emphasis = parse_enum (opt (d, o, "emphasis", "none"),
                            "bold", EMPH_BOLD,
                            "underline", EMPH_UNDERLINE,
                            "none", EMPH_NONE,
                            NULL_SENTINEL);

  a->chart_file_name = parse_chart_file_name (opt (d, o, "charts", file_name));

  a->top_margin = parse_int (opt (d, o, "top-margin", "0"), 0, INT_MAX);
  a->bottom_margin = parse_int (opt (d, o, "bottom-margin", "0"), 0, INT_MAX);

  a->width = parse_page_size (opt (d, o, "width", "79"));
  paper_length = parse_page_size (opt (d, o, "length", "66"));
  a->auto_width = a->width < 0;
  a->auto_length = paper_length < 0;
  a->length = paper_length - vertical_margins (a);

  box = parse_enum (opt (d, o, "box", "ascii"),
                    "ascii", BOX_ASCII,
                    "unicode", BOX_UNICODE,
                    NULL_SENTINEL);
  a->box = box == BOX_ASCII ? ascii_box_chars : unicode_box_chars;

  a->command_name = NULL;
  a->title = xstrdup ("");
  a->subtitle = xstrdup ("");
  a->file_name = xstrdup (file_name);
  a->file = NULL;
  a->error = false;
  a->page_number = 0;
  a->lines = NULL;
  a->allocated_lines = 0;
  a->chart_cnt = 1;

  if (!update_page_size (a, true))
    goto error;

  return d;

error:
  output_driver_destroy (d);
  return NULL;
}

static int
parse_page_size (struct driver_option *option)
{
  int dim = atol (option->default_value);

  if (option->value != NULL)
    {
      if (!strcmp (option->value, "auto"))
        dim = -1;
      else
        {
          int value;
          char *tail;

          errno = 0;
          value = strtol (option->value, &tail, 0);
          if (dim >= 1 && errno != ERANGE && *tail == '\0')
            dim = value;
          else
            error (0, 0, _("%s: %s must be positive integer or `auto'"),
                   option->driver_name, option->name);
        }
    }

  driver_option_destroy (option);

  return dim;
}

static int
vertical_margins (const struct ascii_driver *a)
{
  return a->top_margin + a->bottom_margin + (a->headers ? 3 : 0);
}

/* Re-calculates the page width and length based on settings,
   margins, and, if "auto" is set, the size of the user's
   terminal window or GUI output window. */
static bool
update_page_size (struct ascii_driver *a, bool issue_error)
{
  enum { MIN_WIDTH = 6, MIN_LENGTH = 6 };

  if (a->auto_width)
    a->width = settings_get_viewwidth ();
  if (a->auto_length)
    a->length = settings_get_viewlength () - vertical_margins (a);

  if (a->width < MIN_WIDTH || a->length < MIN_LENGTH)
    {
      if (issue_error)
        error (0, 0,
               _("ascii: page excluding margins and headers "
                 "must be at least %d characters wide by %d lines long, but "
                 "as configured is only %d characters by %d lines"),
               MIN_WIDTH, MIN_LENGTH,
               a->width, a->length);
      if (a->width < MIN_WIDTH)
        a->width = MIN_WIDTH;
      if (a->length < MIN_LENGTH)
        a->length = MIN_LENGTH;
      return false;
    }

  return true;
}

static void
ascii_destroy (struct output_driver *driver)
{
  struct ascii_driver *a = ascii_driver_cast (driver);
  int i;

  if (a->y > 0)
    ascii_close_page (a);

  if (a->file != NULL)
    fn_close (a->file_name, a->file);
  free (a->command_name);
  free (a->title);
  free (a->subtitle);
  free (a->file_name);
  free (a->chart_file_name);
  for (i = 0; i < a->allocated_lines; i++)
    ds_destroy (&a->lines[i].s);
  free (a->lines);
  free (a);
}

static void
ascii_flush (struct output_driver *driver)
{
  struct ascii_driver *a = ascii_driver_cast (driver);
  if (a->y > 0)
    {
      ascii_close_page (a);

      if (fn_close (a->file_name, a->file) != 0)
        error (0, errno, _("ascii: closing output file `%s'"),
               a->file_name);
      a->file = NULL;
    }
}

static void
ascii_init_caption_cell (const char *caption, struct table_cell *cell)
{
  cell->contents = caption;
  cell->options = TAB_LEFT;
  cell->destructor = NULL;
}

static void
ascii_output_table_item (struct ascii_driver *a,
                         const struct table_item *table_item)
{
  const char *caption = table_item_get_caption (table_item);
  struct render_params params;
  struct render_page *page;
  struct render_break x_break;
  int caption_height;
  int i;

  update_page_size (a, false);

  if (caption != NULL)
    {
      /* XXX doesn't do well with very large captions */
      struct table_cell cell;
      ascii_init_caption_cell (caption, &cell);
      caption_height = ascii_measure_cell_height (a, &cell, a->width);
    }
  else
    caption_height = 0;

  params.draw_line = ascii_draw_line;
  params.measure_cell_width = ascii_measure_cell_width;
  params.measure_cell_height = ascii_measure_cell_height;
  params.draw_cell = ascii_draw_cell,
    params.aux = a;
  params.size[H] = a->width;
  params.size[V] = a->length - caption_height;
  params.font_size[H] = 1;
  params.font_size[V] = 1;
  for (i = 0; i < RENDER_N_LINES; i++)
    {
      int width = i == RENDER_LINE_NONE ? 0 : 1;
      params.line_widths[H][i] = width;
      params.line_widths[V][i] = width;
    }

  if (a->file == NULL && !ascii_open_page (a))
    return;

  page = render_page_create (&params, table_item_get_table (table_item));
  for (render_break_init (&x_break, page, H);
       render_break_has_next (&x_break); )
    {
      struct render_page *x_slice;
      struct render_break y_break;

      x_slice = render_break_next (&x_break, a->width);
      for (render_break_init (&y_break, x_slice, V);
           render_break_has_next (&y_break); )
        {
          struct render_page *y_slice;
          int space;

          if (a->y > 0)
            a->y++;

          space = a->length - a->y - caption_height;
          if (render_break_next_size (&y_break) > space)
            {
              assert (a->y > 0);
              ascii_close_page (a);
              if (!ascii_open_page (a))
                return;
              continue;
            }

          y_slice = render_break_next (&y_break, space);
          if (caption_height)
            {
              struct table_cell cell;
              int bb[TABLE_N_AXES][2];

              ascii_init_caption_cell (caption, &cell);
              bb[H][0] = 0;
              bb[H][1] = a->width;
              bb[V][0] = 0;
              bb[V][1] = caption_height;
              ascii_draw_cell (a, &cell, bb, bb);
              a->y += caption_height;
              caption_height = 0;
            }
          render_page_draw (y_slice);
          a->y += render_page_get_size (y_slice, V);
          render_page_unref (y_slice);
        }
      render_break_destroy (&y_break);
    }
  render_break_destroy (&x_break);
}

static void
ascii_output_text (struct ascii_driver *a, const char *text)
{
  struct table_item *table_item;

  table_item = table_item_create (table_from_string (TAB_LEFT, text), NULL);
  ascii_output_table_item (a, table_item);
  table_item_unref (table_item);
}

static void
ascii_submit (struct output_driver *driver,
              const struct output_item *output_item)
{
  struct ascii_driver *a = ascii_driver_cast (driver);

  output_driver_track_current_command (output_item, &a->command_name);

  if (a->error)
    return;

  if (is_table_item (output_item))
    ascii_output_table_item (a, to_table_item (output_item));
#ifdef HAVE_CAIRO
  else if (is_chart_item (output_item) && a->chart_file_name != NULL)
    {
      struct chart_item *chart_item = to_chart_item (output_item);
      char *file_name;

      file_name = xr_draw_png_chart (chart_item, a->chart_file_name,
                                     a->chart_cnt++);
      if (file_name != NULL)
        {
          struct text_item *text_item;

          text_item = text_item_create_format (
            TEXT_ITEM_PARAGRAPH, _("See %s for a chart."), file_name);

          ascii_submit (driver, &text_item->output_item);
          text_item_unref (text_item);
          free (file_name);
        }
    }
#endif  /* HAVE_CAIRO */
  else if (is_text_item (output_item))
    {
      const struct text_item *text_item = to_text_item (output_item);
      enum text_item_type type = text_item_get_type (text_item);
      const char *text = text_item_get_text (text_item);

      switch (type)
        {
        case TEXT_ITEM_TITLE:
          free (a->title);
          a->title = xstrdup (text);
          break;

        case TEXT_ITEM_SUBTITLE:
          free (a->subtitle);
          a->subtitle = xstrdup (text);
          break;

        case TEXT_ITEM_COMMAND_OPEN:
        case TEXT_ITEM_COMMAND_CLOSE:
          break;

        case TEXT_ITEM_BLANK_LINE:
          if (a->y > 0)
            a->y++;
          break;

        case TEXT_ITEM_EJECT_PAGE:
          if (a->y > 0)
            ascii_close_page (a);
          break;

        default:
          ascii_output_text (a, text);
          break;
        }
    }
  else if (is_message_item (output_item))
    {
      const struct message_item *message_item = to_message_item (output_item);
      const struct msg *msg = message_item_get_msg (message_item);
      char *s = msg_to_string (msg, a->command_name);
      ascii_output_text (a, s);
      free (s);
    }
}

const struct output_driver_factory txt_driver_factory =
  { "txt", ascii_create };
const struct output_driver_factory list_driver_factory =
  { "list", ascii_create };

static const struct output_driver_class ascii_driver_class =
  {
    "text",
    ascii_destroy,
    ascii_submit,
    ascii_flush,
  };

static char *ascii_reserve (struct ascii_driver *, int y, int x0, int x1,
                            int n);
static void ascii_layout_cell (struct ascii_driver *,
                               const struct table_cell *,
                               int bb[TABLE_N_AXES][2],
                               int clip[TABLE_N_AXES][2],
                               int *width, int *height);

static void
ascii_draw_line (void *a_, int bb[TABLE_N_AXES][2],
                 enum render_line_style styles[TABLE_N_AXES][2])
{
  struct ascii_driver *a = a_;
  char mbchar[6];
  int x0, x1, y1;
  ucs4_t uc;
  int mblen;
  int x, y;

  /* Clip to the page. */
  if (bb[H][0] >= a->width || bb[V][0] + a->y >= a->length)
    return;
  x0 = bb[H][0];
  x1 = MIN (bb[H][1], a->width);
  y1 = MIN (bb[V][1] + a->y, a->length);

  /* Draw. */
  uc = a->box[make_box_index (styles[V][0], styles[V][1],
                              styles[H][0], styles[H][1])];
  mblen = u8_uctomb (CHAR_CAST (uint8_t *, mbchar), uc, 6);
  for (y = bb[V][0] + a->y; y < y1; y++)
    {
      char *p = ascii_reserve (a, y, x0, x1, mblen * (x1 - x0));
      for (x = x0; x < x1; x++)
        {
          memcpy (p, mbchar, mblen);
          p += mblen;
        }
    }
}

static void
ascii_measure_cell_width (void *a_, const struct table_cell *cell,
                          int *min_width, int *max_width)
{
  struct ascii_driver *a = a_;
  int bb[TABLE_N_AXES][2];
  int clip[TABLE_N_AXES][2];
  int h;

  bb[H][0] = 0;
  bb[H][1] = INT_MAX;
  bb[V][0] = 0;
  bb[V][1] = INT_MAX;
  clip[H][0] = clip[H][1] = clip[V][0] = clip[V][1] = 0;
  ascii_layout_cell (a, cell, bb, clip, max_width, &h);

  if (strchr (cell->contents, ' '))
    {
      bb[H][1] = 1;
      ascii_layout_cell (a, cell, bb, clip, min_width, &h);
    }
  else
    *min_width = *max_width;
}

static int
ascii_measure_cell_height (void *a_, const struct table_cell *cell, int width)
{
  struct ascii_driver *a = a_;
  int bb[TABLE_N_AXES][2];
  int clip[TABLE_N_AXES][2];
  int w, h;

  bb[H][0] = 0;
  bb[H][1] = width;
  bb[V][0] = 0;
  bb[V][1] = INT_MAX;
  clip[H][0] = clip[H][1] = clip[V][0] = clip[V][1] = 0;
  ascii_layout_cell (a, cell, bb, clip, &w, &h);
  return h;
}

static void
ascii_draw_cell (void *a_, const struct table_cell *cell,
                 int bb[TABLE_N_AXES][2], int clip[TABLE_N_AXES][2])
{
  struct ascii_driver *a = a_;
  int w, h;

  ascii_layout_cell (a, cell, bb, clip, &w, &h);
}

static int
u8_mb_to_display (int *wp, const uint8_t *s, size_t n)
{
  size_t ofs;
  ucs4_t uc;
  int w;

  ofs = u8_mbtouc (&uc, s, n);
  if (ofs < n && s[ofs] == '\b')
    {
      ofs++;
      ofs += u8_mbtouc (&uc, s + ofs, n - ofs);
    }

  w = uc_width (uc, "UTF-8");
  if (w <= 0)
    {
      *wp = 0;
      return ofs;
    }

  while (ofs < n)
    {
      int mblen = u8_mbtouc (&uc, s + ofs, n - ofs);
      if (uc_width (uc, "UTF-8") > 0)
        break;
      ofs += mblen;
    }

  *wp = w;
  return ofs;
}

struct ascii_pos
  {
    int x0;
    int x1;
    size_t ofs0;
    size_t ofs1;
  };

static void
find_ascii_pos (struct ascii_line *line, int target_x, struct ascii_pos *c)
{
  const uint8_t *s = CHAR_CAST (const uint8_t *, ds_cstr (&line->s));
  size_t length = ds_length (&line->s);
  size_t ofs;
  int mblen;
  int x;

  x = 0;
  for (ofs = 0; ; ofs += mblen)
    {
      int w;

      mblen = u8_mb_to_display (&w, s + ofs, length - ofs);
      if (x + w > target_x)
        {
          c->x0 = x;
          c->x1 = x + w;
          c->ofs0 = ofs;
          c->ofs1 = ofs + mblen;
          return;
        }
      x += w;
    }
}

static char *
ascii_reserve (struct ascii_driver *a, int y, int x0, int x1, int n)
{
  struct ascii_line *line;
  assert (y < a->allocated_lines);
  line = &a->lines[y];

  if (x0 >= line->width)
    {
      /* The common case: adding new characters at the end of a line. */
      ds_put_byte_multiple (&line->s, ' ', x0 - line->width);
      line->width = x1;
      return ds_put_uninit (&line->s, n);
    }
  else if (x0 == x1)
    return NULL;
  else
    {
      /* An unusual case: overwriting characters in the middle of a line.  We
         don't keep any kind of mapping from bytes to display positions, so we
         have to iterate over the whole line starting from the beginning. */
      struct ascii_pos p0, p1;
      char *s;

      /* Find the positions of the first and last character.  We must find the
         both characters' positions before changing the line, because that
         would prevent finding the other character's position. */
      find_ascii_pos (line, x0, &p0);
      if (x1 < line->width)
        find_ascii_pos (line, x1, &p1);

      /* If a double-width character occupies both x0 - 1 and x0, then replace
         its first character width by '?'. */
      s = ds_data (&line->s);
      while (p0.x0 < x0)
        {
          s[p0.ofs0++] = '?';
          p0.x0++;
        }

      if (x1 >= line->width)
        {
          ds_truncate (&line->s, p0.ofs0);
          line->width = x1;
          return ds_put_uninit (&line->s, n);
        }

      /* If a double-width character occupies both x1 - 1 and x1, then we need
         to replace its second character width by '?'. */
      if (p1.x0 < x1)
        {
          do
            {
              s[--p1.ofs1] = '?';
              p1.x0++;
            }
          while (p1.x0 < x1);
          return ds_splice_uninit (&line->s, p0.ofs0, p1.ofs1 - p0.ofs0, n);
        }

      return ds_splice_uninit (&line->s, p0.ofs0, p1.ofs0 - p0.ofs0, n);
    }
}

static void
text_draw (struct ascii_driver *a, unsigned int options,
           int bb[TABLE_N_AXES][2], int clip[TABLE_N_AXES][2],
           int y, const uint8_t *string, int n, size_t width)
{
  int x0 = MAX (0, clip[H][0]);
  int y0 = MAX (0, clip[V][0] + a->y);
  int x1 = clip[H][1];
  int y1 = MIN (a->length, clip[V][1] + a->y);
  int x;

  y += a->y;
  if (y < y0 || y >= y1)
    return;

  switch (options & TAB_ALIGNMENT)
    {
    case TAB_LEFT:
      x = bb[H][0];
      break;
    case TAB_CENTER:
      x = (bb[H][0] + bb[H][1] - width + 1) / 2;
      break;
    case TAB_RIGHT:
      x = bb[H][1] - width;
      break;
    default:
      NOT_REACHED ();
    }
  if (x >= x1)
    return;

  while (x < x0)
    {
      ucs4_t uc;
      int mblen;
      int w;

      if (n == 0)
        return;
      mblen = u8_mbtouc (&uc, string, n);

      string += mblen;
      n -= mblen;

      w = uc_width (uc, "UTF-8");
      if (w > 0)
        {
          x += w;
          width -= w;
        }
    }
  if (n == 0)
    return;

  if (x + width > x1)
    {
      int ofs;

      ofs = width = 0;
      for (ofs = 0; ofs < n; )
        {
          ucs4_t uc;
          int mblen;
          int w;

          mblen = u8_mbtouc (&uc, string + ofs, n - ofs);

          w = uc_width (uc, "UTF-8");
          if (w > 0)
            {
              if (width + w > x1 - x)
                break;
              width += w;
            }
          ofs += mblen;
        }
      n = ofs;
      if (n == 0)
        return;
    }

  if (!(options & TAB_EMPH) || a->emphasis == EMPH_NONE)
    memcpy (ascii_reserve (a, y, x, x + width, n), string, n);
  else
    {
      size_t n_out;
      size_t ofs;
      char *out;
      int mblen;

      /* First figure out how many bytes need to be inserted. */
      n_out = n;
      for (ofs = 0; ofs < n; ofs += mblen)
        {
          ucs4_t uc;
          int w;

          mblen = u8_mbtouc (&uc, string + ofs, n - ofs);
          w = uc_width (uc, "UTF-8");

          if (w > 0)
            n_out += a->emphasis == EMPH_UNDERLINE ? 2 : 1 + mblen;
        }

      /* Then insert them. */
      out = ascii_reserve (a, y, x, x + width, n_out);
      for (ofs = 0; ofs < n; ofs += mblen)
        {
          ucs4_t uc;
          int w;

          mblen = u8_mbtouc (&uc, string + ofs, n - ofs);
          w = uc_width (uc, "UTF-8");

          if (w > 0)
            {
              if (a->emphasis == EMPH_UNDERLINE)
                *out++ = '_';
              else
                out = mempcpy (out, string + ofs, mblen);
              *out++ = '\b';
            }
          out = mempcpy (out, string + ofs, mblen);
        }
    }
}

static void
ascii_layout_cell (struct ascii_driver *a, const struct table_cell *cell,
                   int bb[TABLE_N_AXES][2], int clip[TABLE_N_AXES][2],
                   int *widthp, int *heightp)
{
  const char *text = cell->contents;
  size_t length = strlen (text);
  char *breaks;
  int bb_width;
  size_t pos;
  int y;

  *widthp = 0;
  *heightp = 0;
  if (length == 0)
    return;

  text = cell->contents;
  breaks = xmalloc (length + 1);
  u8_possible_linebreaks (CHAR_CAST (const uint8_t *, text), length,
                          "UTF-8", breaks);
  breaks[length] = (breaks[length - 1] == UC_BREAK_MANDATORY
                    ? UC_BREAK_PROHIBITED : UC_BREAK_POSSIBLE);

  pos = 0;
  bb_width = bb[H][1] - bb[H][0];
  for (y = bb[V][0]; y < bb[V][1] && pos < length; y++)
    {
      const uint8_t *line = CHAR_CAST (const uint8_t *, text + pos);
      const char *b = breaks + pos;
      size_t n = length - pos;

      size_t last_break_ofs = 0;
      int last_break_width = 0;
      int width = 0;
      size_t ofs;

      for (ofs = 0; ofs < n; )
        {
          ucs4_t uc;
          int mblen;
          int w;

          mblen = u8_mbtouc (&uc, line + ofs, n - ofs);
          if (b[ofs] == UC_BREAK_MANDATORY)
            break;
          else if (b[ofs] == UC_BREAK_POSSIBLE)
            {
              last_break_ofs = ofs;
              last_break_width = width;
            }

          w = uc_width (uc, "UTF-8");
          if (w > 0)
            {
              if (width + w > bb_width)
                {
                  if (isspace (line[ofs]))
                    break;
                  else if (last_break_ofs != 0)
                    {
                      ofs = last_break_ofs;
                      width = last_break_width;
                      break;
                    }
                }
              width += w;
            }
          ofs += mblen;
        }
      if (b[ofs] != UC_BREAK_MANDATORY)
        {
          while (ofs > 0 && isspace (line[ofs - 1]))
            {
              ofs--;
              width--;
            }
        }
      if (width > *widthp)
        *widthp = width;

      /* Draw text. */
      text_draw (a, cell->options, bb, clip, y, line, ofs, width);

      /* Next line. */
      pos += ofs;
      if (ofs < n && isspace (line[ofs]))
        pos++;

    }
  *heightp = y - bb[V][0];

  free (breaks);
}

void
ascii_test_write (struct output_driver *driver,
                  const char *s, int x, int y, unsigned int options)
{
  struct ascii_driver *a = ascii_driver_cast (driver);
  struct table_cell cell;
  int bb[TABLE_N_AXES][2];
  int width, height;

  if (a->file == NULL && !ascii_open_page (a))
    return;
  a->y = 0;

  memset (&cell, 0, sizeof cell);
  cell.contents = s;
  cell.options = options | TAB_LEFT;

  bb[TABLE_HORZ][0] = x;
  bb[TABLE_HORZ][1] = a->width;
  bb[TABLE_VERT][0] = y;
  bb[TABLE_VERT][1] = a->length;

  ascii_layout_cell (a, &cell, bb, bb, &width, &height);

  a->y = 1;
}

/* ascii_close_page () and support routines. */

#if HAVE_DECL_SIGWINCH
static struct ascii_driver *the_driver;

static void
winch_handler (int signum UNUSED)
{
  update_page_size (the_driver, false);
}
#endif

static bool
ascii_open_page (struct ascii_driver *a)
{
  int i;

  if (a->error)
    return false;

  if (a->file == NULL)
    {
      a->file = fn_open (a->file_name, a->append ? "a" : "w");
      if (a->file != NULL)
        {
#if HAVE_DECL_SIGWINCH
	  if ( isatty (fileno (a->file)))
	    {
	      struct sigaction action;
	      sigemptyset (&action.sa_mask);
	      action.sa_flags = 0;
	      action.sa_handler = winch_handler;
	      the_driver = a;
	      a->auto_width = true;
	      a->auto_length = true;
	      sigaction (SIGWINCH, &action, NULL);
	    }
#endif
        }
      else
        {
          error (0, errno, _("ascii: opening output file `%s'"),
                 a->file_name);
          a->error = true;
          return false;
        }
    }

  a->page_number++;

  if (a->length > a->allocated_lines)
    {
      a->lines = xnrealloc (a->lines, a->length, sizeof *a->lines);
      for (i = a->allocated_lines; i < a->length; i++)
        {
          struct ascii_line *line = &a->lines[i];
          ds_init_empty (&line->s);
          line->width = 0;
        }
      a->allocated_lines = a->length;
    }

  for (i = 0; i < a->length; i++)
    {
      struct ascii_line *line = &a->lines[i];
      ds_clear (&line->s);
      line->width = 0;
    }

  return true;
}

static void
output_title_line (FILE *out, int width, const char *left, const char *right)
{
  struct string s = DS_EMPTY_INITIALIZER;
  ds_put_byte_multiple (&s, ' ', width);
  if (left != NULL)
    {
      size_t length = MIN (strlen (left), width);
      memcpy (ds_end (&s) - width, left, length);
    }
  if (right != NULL)
    {
      size_t length = MIN (strlen (right), width);
      memcpy (ds_end (&s) - length, right, length);
    }
  ds_put_byte (&s, '\n');
  fputs (ds_cstr (&s), out);
  ds_destroy (&s);
}

static void
ascii_close_page (struct ascii_driver *a)
{
  bool any_blank;
  int i, y;

  a->y = 0;
  if (a->file == NULL)
    return;

  if (!a->top_margin && !a->bottom_margin && a->squeeze_blank_lines
      && !a->paginate && a->page_number > 1)
    putc ('\n', a->file);

  for (i = 0; i < a->top_margin; i++)
    putc ('\n', a->file);
  if (a->headers)
    {
      char *r1, *r2;

      r1 = xasprintf (_("%s - Page %d"), get_start_date (), a->page_number);
      r2 = xasprintf ("%s - %s" , version, host_system);

      output_title_line (a->file, a->width, a->title, r1);
      output_title_line (a->file, a->width, a->subtitle, r2);
      putc ('\n', a->file);

      free (r1);
      free (r2);
    }

  any_blank = false;
  for (y = 0; y < a->allocated_lines; y++)
    {
      struct ascii_line *line = &a->lines[y];

      if (a->squeeze_blank_lines && y > 0 && line->width == 0)
        any_blank = true;
      else
        {
          if (any_blank)
            {
              putc ('\n', a->file);
              any_blank = false;
            }

          while (ds_chomp_byte (&line->s, ' '))
            continue;
          fwrite (ds_data (&line->s), 1, ds_length (&line->s), a->file);
          putc ('\n', a->file);
        }
    }
  if (!a->squeeze_blank_lines)
    for (y = a->allocated_lines; y < a->length; y++)
      putc ('\n', a->file);

  for (i = 0; i < a->bottom_margin; i++)
    putc ('\n', a->file);
  if (a->paginate)
    putc ('\f', a->file);
}
