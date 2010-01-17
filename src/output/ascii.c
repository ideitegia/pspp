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
#include <libpspp/start-date.h>
#include <libpspp/string-map.h>
#include <libpspp/version.h>
#include <output/cairo.h>
#include <output/chart-item-provider.h>
#include "output/options.h"
#include <output/tab.h>
#include <output/text-item.h>
#include <output/driver-provider.h>
#include <output/render.h>
#include <output/table-item.h>

#include "error.h"
#include "minmax.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* This file uses TABLE_HORZ and TABLE_VERT enough to warrant abbreviating. */
#define H TABLE_HORZ
#define V TABLE_VERT

/* Line styles bit shifts. */
enum
  {
    LNS_TOP = 0,
    LNS_LEFT = 2,
    LNS_BOTTOM = 4,
    LNS_RIGHT = 6,

    LNS_COUNT = 256
  };

static inline int
make_box_index (int left, int right, int top, int bottom)
{
  return ((left << LNS_LEFT) | (right << LNS_RIGHT)
          | (top << LNS_TOP) | (bottom << LNS_BOTTOM));
}

/* Character attributes. */
#define ATTR_EMPHASIS   0x100   /* Bold-face. */
#define ATTR_BOX        0x200   /* Line drawing character. */

/* A line of text. */
struct ascii_line
  {
    unsigned short *chars;      /* Characters and attributes. */
    int n_chars;                /* Length. */
    int allocated_chars;        /* Allocated "chars" elements. */
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
    bool append;                /* Append if output-file already exists? */
    bool headers;		/* Print headers at top of page? */
    bool paginate;		/* Insert formfeeds? */
    bool squeeze_blank_lines;   /* Squeeze multiple blank lines into one? */
    enum emphasis_style emphasis; /* How to emphasize text. */
    int tab_width;		/* Width of a tab; 0 not to use tabs. */
    char *chart_file_name;      /* Name of files used for charts. */

    int width;                  /* Page width. */
    int length;                 /* Page length minus margins and header. */
    bool auto_width;            /* Use viewwidth as page width? */
    bool auto_length;           /* Use viewlength as page width? */

    int top_margin;		/* Top margin in lines. */
    int bottom_margin;		/* Bottom margin in lines. */

    char *box[LNS_COUNT];       /* Line & box drawing characters. */
    char *init;                 /* Device initialization string. */

    /* Internal state. */
    char *title;
    char *subtitle;
    char *file_name;            /* Output file name. */
    FILE *file;                 /* Output file. */
    bool reported_error;        /* Reported file open error? */
    int page_number;		/* Current page number. */
    struct ascii_line *lines;   /* Page content. */
    int allocated_lines;        /* Number of lines allocated. */
    int chart_cnt;              /* Number of charts so far. */
    int y;
  };

static int vertical_margins (const struct ascii_driver *);

static const char *get_default_box (int right, int bottom, int left, int top);
static bool update_page_size (struct ascii_driver *, bool issue_error);
static int parse_page_size (struct driver_option *);

static void ascii_close_page (struct ascii_driver *);
static void ascii_open_page (struct ascii_driver *);

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
  assert (driver->class == &ascii_class);
  return UP_CAST (driver, struct ascii_driver, driver);
}

static struct driver_option *
opt (struct output_driver *d, struct string_map *options, const char *key,
     const char *default_value)
{
  return driver_option_get (d, options, key, default_value);
}

static struct output_driver *
ascii_create (const char *name, enum output_device_type device_type,
              struct string_map *o)
{
  struct output_driver *d;
  struct ascii_driver *a;
  int paper_length;
  int right, bottom, left, top;

  a = xzalloc (sizeof *a);
  d = &a->driver;
  output_driver_init (&a->driver, &ascii_class, name, device_type);
  a->append = parse_boolean (opt (d, o, "append", "false"));
  a->headers = parse_boolean (opt (d, o, "headers", "true"));
  a->paginate = parse_boolean (opt (d, o, "paginate", "true"));
  a->squeeze_blank_lines = parse_boolean (opt (d, o, "squeeze", "false"));
  a->emphasis = parse_enum (opt (d, o, "emphasis", "bold"),
                            "bold", EMPH_BOLD,
                            "underline", EMPH_UNDERLINE,
                            "none", EMPH_NONE,
                            (char *) NULL);
  a->tab_width = parse_int (opt (d, o, "tab-width", "0"), 8, INT_MAX);

  if (parse_enum (opt (d, o, "chart-type", "png"),
                  "png", true,
                  "none", false,
                  (char *) NULL))
    a->chart_file_name = parse_chart_file_name (opt (d, o, "chart-files",
                                                     "pspp-#.png"));
  else
    a->chart_file_name = NULL;

  a->top_margin = parse_int (opt (d, o, "top-margin", "2"), 0, INT_MAX);
  a->bottom_margin = parse_int (opt (d, o, "bottom-margin", "2"), 0, INT_MAX);

  a->width = parse_page_size (opt (d, o, "width", "79"));
  paper_length = parse_page_size (opt (d, o, "length", "66"));
  a->auto_width = a->width < 0;
  a->auto_length = paper_length < 0;
  a->length = paper_length - vertical_margins (a);

  for (right = 0; right < 4; right++)
    for (bottom = 0; bottom < 4; bottom++)
      for (left = 0; left < 4; left++)
        for (top = 0; top < 4; top++)
          {
            int indx = make_box_index (left, right, top, bottom);
            const char *default_value;
            char name[16];

            sprintf (name, "box[%d%d%d%d]", right, bottom, left, top);
            default_value = get_default_box (right, bottom, left, top);
            a->box[indx] = parse_string (opt (d, o, name, default_value));
          }
  a->init = parse_string (opt (d, o, "init", ""));

  a->title = xstrdup ("");
  a->subtitle = xstrdup ("");
  a->file_name = parse_string (opt (d, o, "output-file", "pspp.list"));
  a->file = NULL;
  a->reported_error = false;
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

static const char *
get_default_box (int right, int bottom, int left, int top)
{
  switch ((top << 12) | (left << 8) | (bottom << 4) | (right << 0))
    {
    case 0x0000:
      return " ";

    case 0x0100: case 0x0101: case 0x0001:
      return "-";

    case 0x1000: case 0x1010: case 0x0010:
      return "|";

    case 0x0300: case 0x0303: case 0x0003:
    case 0x0200: case 0x0202: case 0x0002:
      return "=";

    default:
      return left > 1 || top > 1 || right > 1 || bottom > 1 ? "#" : "+";
    }
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

  free (a->title);
  free (a->subtitle);
  free (a->file_name);
  free (a->chart_file_name);
  for (i = 0; i < LNS_COUNT; i++)
    free (a->box[i]);
  free (a->init);
  if (a->file != NULL)
    fclose (a->file);
  for (i = 0; i < a->allocated_lines; i++)
    free (a->lines[i].chars);
  free (a->lines);
  free (a);
}

static void
ascii_flush (struct output_driver *driver)
{
  struct ascii_driver *a = ascii_driver_cast (driver);
  if (a->file != NULL)
    fflush (a->file);
}

static void
ascii_init_caption_cell (const char *caption, struct table_cell *cell)
{
  cell->contents = caption;
  cell->options = TAB_LEFT;
  cell->destructor = NULL;
}

static void
ascii_submit (struct output_driver *driver,
              const struct output_item *output_item)
{
  struct ascii_driver *a = ascii_driver_cast (driver);
  if (is_table_item (output_item))
    {
      struct table_item *table_item = to_table_item (output_item);
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

      if (a->file == NULL)
        {
          ascii_open_page (a);
          a->y = 0;
        }

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
                  a->y = 0;
                  ascii_open_page (a);
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
          {
            struct table_item *item;

            item = table_item_create (table_from_string (0, text), NULL);
            ascii_submit (&a->driver, &item->output_item);
            table_item_unref (item);
          }
          break;
        }
    }
}

const struct output_driver_class ascii_class =
  {
    "ascii",
    ascii_create,
    ascii_destroy,
    ascii_submit,
    ascii_flush,
  };

enum wrap_mode
  {
    WRAP_WORD,
    WRAP_CHAR,
    WRAP_WORD_CHAR
  };

static void ascii_expand_line (struct ascii_driver *, int y, int length);
static void ascii_layout_cell (struct ascii_driver *,
                               const struct table_cell *,
                               int bb[TABLE_N_AXES][2],
                               int clip[TABLE_N_AXES][2], enum wrap_mode wrap,
                               int *width, int *height);

static void
ascii_draw_line (void *a_, int bb[TABLE_N_AXES][2],
                 enum render_line_style styles[TABLE_N_AXES][2])
{
  struct ascii_driver *a = a_;
  unsigned short int value;
  int x1, y1;
  int x, y;

  /* Clip to the page. */
  if (bb[H][0] >= a->width || bb[V][0] + a->y >= a->length)
    return;
  x1 = MIN (bb[H][1], a->width);
  y1 = MIN (bb[V][1] + a->y, a->length);

  /* Draw. */
  value = ATTR_BOX | make_box_index (styles[V][0], styles[V][1],
                                     styles[H][0], styles[H][1]);
  for (y = bb[V][0] + a->y; y < y1; y++)
    {
      ascii_expand_line (a, y, x1);
      for (x = bb[H][0]; x < x1; x++)
        a->lines[y].chars[x] = value;
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
  ascii_layout_cell (a, cell, bb, clip, WRAP_WORD, max_width, &h);

  if (strchr (cell->contents, ' '))
    {
      bb[H][1] = 1;
      ascii_layout_cell (a, cell, bb, clip, WRAP_WORD, min_width, &h);
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
  ascii_layout_cell (a, cell, bb, clip, WRAP_WORD, &w, &h);
  return h;
}

static void
ascii_draw_cell (void *a_, const struct table_cell *cell,
                 int bb[TABLE_N_AXES][2], int clip[TABLE_N_AXES][2])
{
  struct ascii_driver *a = a_;
  int w, h;

  ascii_layout_cell (a, cell, bb, clip, WRAP_WORD, &w, &h);
}

/* Ensures that at least the first LENGTH characters of line Y in
   ascii driver A have been cleared out. */
static void
ascii_expand_line (struct ascii_driver *a, int y, int length)
{
  struct ascii_line *line = &a->lines[y];
  if (line->n_chars < length)
    {
      int x;
      if (line->allocated_chars < length)
        {
          line->allocated_chars = MAX (length, MIN (length * 2, a->width));
          line->chars = xnrealloc (line->chars, line->allocated_chars,
                                   sizeof *line->chars);
        }
      for (x = line->n_chars; x < length; x++)
        line->chars[x] = ' ';
      line->n_chars = length;
    }
}

static void
text_draw (struct ascii_driver *a, const struct table_cell *cell,
           int bb[TABLE_N_AXES][2], int clip[TABLE_N_AXES][2],
           int y, const char *string, int n)
{
  int x0 = MAX (0, clip[H][0]);
  int y0 = MAX (0, clip[V][0] + a->y);
  int x1 = clip[H][1];
  int y1 = MIN (a->length, clip[V][1] + a->y);
  int x;

  y += a->y;
  if (y < y0 || y >= y1)
    return;

  switch (cell->options & TAB_ALIGNMENT)
    {
    case TAB_LEFT:
      x = bb[H][0];
      break;
    case TAB_CENTER:
      x = (bb[H][0] + bb[H][1] - n + 1) / 2;
      break;
    case TAB_RIGHT:
      x = bb[H][1] - n;
      break;
    default:
      NOT_REACHED ();
    }

  if (x0 > x)
    {
      n -= x0 - x;
      if (n <= 0)
        return;
      string += x0 - x;
      x = x0;
    }
  if (x + n >= x1)
    n = x1 - x;

  if (n > 0)
    {
      int attr = cell->options & TAB_EMPH ? ATTR_EMPHASIS : 0;
      size_t i;

      ascii_expand_line (a, y, x + n);
      for (i = 0; i < n; i++)
        a->lines[y].chars[x + i] = string[i] | attr;
    }
}

static void
ascii_layout_cell (struct ascii_driver *a, const struct table_cell *cell,
                   int bb[TABLE_N_AXES][2], int clip[TABLE_N_AXES][2],
                   enum wrap_mode wrap, int *width, int *height)
{
  size_t length = strlen (cell->contents);
  int y, pos;

  *width = 0;
  pos = 0;
  for (y = bb[V][0]; y < bb[V][1] && pos < length; y++)
    {
      const char *line = &cell->contents[pos];
      const char *new_line;
      size_t line_len;

      /* Find line length without considering word wrap. */
      line_len = MIN (bb[H][1] - bb[H][0], length - pos);
      new_line = memchr (line, '\n', line_len);
      if (new_line != NULL)
        line_len = new_line - line;

      /* Word wrap. */
      if (pos + line_len < length && wrap != WRAP_CHAR)
        {
          size_t space_len = line_len;
          while (space_len > 0 && !isspace ((unsigned char) line[space_len]))
            space_len--;
          if (space_len > 0)
            line_len = space_len;
          else if (wrap == WRAP_WORD)
            {
              while (pos + line_len < length
                     && !isspace ((unsigned char) line[line_len]))
                line_len++;
            }
        }
      if (line_len > *width)
        *width = line_len;

      /* Draw text. */
      text_draw (a, cell, bb, clip, y, line, line_len);

      /* Next line. */
      pos += line_len;
      if (pos < length && isspace ((unsigned char) cell->contents[pos]))
        pos++;
    }
  *height = y - bb[V][0];
}

/* ascii_close_page () and support routines. */

static void
ascii_open_page (struct ascii_driver *a)
{
  int i;

  if (a->file == NULL)
    {
      a->file = fn_open (a->file_name, a->append ? "a" : "w");
      if (a->file != NULL)
        {
          if (a->init != NULL)
            fputs (a->init, a->file);
        }
      else
        {
          /* Report the error to the user and complete
             initialization.  If we do not finish initialization,
             then calls to other driver functions will segfault
             later.  It would be better to simply drop the driver
             entirely, but we do not have a convenient mechanism
             for this (yet). */
          if (!a->reported_error)
            error (0, errno, _("ascii: opening output file \"%s\""),
                   a->file_name);
          a->reported_error = true;
        }
    }

  a->page_number++;

  if (a->length > a->allocated_lines)
    {
      a->lines = xnrealloc (a->lines, a->length, sizeof *a->lines);
      for (i = a->allocated_lines; i < a->length; i++)
        {
          struct ascii_line *line = &a->lines[i];
          line->chars = NULL;
          line->allocated_chars = 0;
        }
      a->allocated_lines = a->length;
    }

  for (i = 0; i < a->length; i++)
    a->lines[i].n_chars = 0;
}

/* Writes LINE to A's output file.  */
static void
output_line (struct ascii_driver *a, const struct ascii_line *line)
{
  size_t length;
  size_t i;

  length = line->n_chars;
  while (length > 0 && line->chars[length - 1] == ' ')
    length--;

  for (i = 0; i < length; i++)
    {
      int attribute = line->chars[i] & (ATTR_BOX | ATTR_EMPHASIS);
      int ch = line->chars[i] & ~(ATTR_BOX | ATTR_EMPHASIS);

      switch (attribute)
        {
        case ATTR_BOX:
          fputs (a->box[ch], a->file);
          break;

        case ATTR_EMPHASIS:
          if (a->emphasis == EMPH_BOLD)
            fprintf (a->file, "%c\b%c", ch, ch);
          else if (a->emphasis == EMPH_UNDERLINE)
            fprintf (a->file, "_\b%c", ch);
          else
            putc (ch, a->file);
          break;

        default:
          putc (ch, a->file);
          break;
        }
    }

  putc ('\n', a->file);
}

static void
output_title_line (FILE *out, int width, const char *left, const char *right)
{
  struct string s = DS_EMPTY_INITIALIZER;
  ds_put_char_multiple (&s, ' ', width);
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
  ds_put_char (&s, '\n');
  fputs (ds_cstr (&s), out);
  ds_destroy (&s);
}

static void
ascii_close_page (struct ascii_driver *a)
{
  bool any_blank;
  int i, y;

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

      if (a->squeeze_blank_lines && y > 0 && line->n_chars == 0)
        any_blank = true;
      else
        {
          if (any_blank)
            {
              putc ('\n', a->file);
              any_blank = false;
            }

          output_line (a, line);
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
