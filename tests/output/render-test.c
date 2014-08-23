/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011, 2012, 2013, 2014 Free Software Foundation, Inc.

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
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/string-map.h"
#include "output/ascii.h"
#include "output/driver.h"
#include "output/tab.h"
#include "output/table-item.h"

#include "gl/error.h"
#include "gl/progname.h"
#include "gl/xalloc.h"
#include "gl/xvasprintf.h"

/* --transpose: Transpose the table before outputting? */
static int transpose;

/* --emphasis: ASCII driver emphasis option. */
static char *emphasis;

/* --box: ASCII driver box option. */
static char *box;

/* --draw-mode: special ASCII driver test mode. */
static int draw_mode;

/* --no-txt: Whether to render to <base>.txt. */
static int render_txt = true;

/* --no-stdout: Whether to render to stdout. */
static int render_stdout = true;

/* --pdf: Also render PDF output. */
static int render_pdf;

/* ASCII driver, for ASCII driver test mode. */
static struct output_driver *ascii_driver;

/* -o, --output: Base name for output files. */
static const char *output_base = "render";

static const char *parse_options (int argc, char **argv);
static void usage (void) NO_RETURN;
static struct table *read_table (FILE *);
static void draw (FILE *);

int
main (int argc, char **argv)
{
  const char *input_file_name;
  FILE *input;

  set_program_name (argv[0]);
  input_file_name = parse_options (argc, argv);

  if (!strcmp (input_file_name, "-"))
    input = stdin;
  else
    {
      input = fopen (input_file_name, "r");
      if (input == NULL)
        error (1, errno, "%s: open failed", input_file_name);
    }

  if (!draw_mode)
    {
      struct table *table;

      table = read_table (input);

      if (transpose)
        table = table_transpose (table);

      table_item_submit (table_item_create (table, NULL));
    }
  else
    draw (input);

  if (input != stdin)
    fclose (input);

  output_close ();

  return 0;
}

static void
configure_drivers (int width, int length, int min_break)
{
  struct string_map options, tmp;
  struct output_driver *driver;

  string_map_init (&options);
  string_map_insert (&options, "format", "txt");
  string_map_insert (&options, "output-file", "-");
  string_map_insert_nocopy (&options, xstrdup ("width"),
                            xasprintf ("%d", width));
  string_map_insert_nocopy (&options, xstrdup ("length"),
                            xasprintf ("%d", length));
  if (min_break >= 0)
    {
      string_map_insert_nocopy (&options, xstrdup ("min-hbreak"),
                                xasprintf ("%d", min_break));
      string_map_insert_nocopy (&options, xstrdup ("min-vbreak"),
                                xasprintf ("%d", min_break));
    }
  if (emphasis != NULL)
    string_map_insert (&options, "emphasis", emphasis);
  if (box != NULL)
    string_map_insert (&options, "box", box);

  /* Render to stdout. */
  if (render_stdout)
    {
      string_map_clone (&tmp, &options);
      ascii_driver = driver = output_driver_create (&tmp);
      if (driver == NULL)
        exit (EXIT_FAILURE);
      output_driver_register (driver);
      string_map_destroy (&tmp);
    }

  if (draw_mode)
   {
     string_map_destroy (&options);
     return;
   }

  /* Render to <base>.txt. */
  if (render_txt)
    {
      string_map_clear (&options);
      string_map_insert_nocopy (&options, xstrdup ("output-file"),
                                xasprintf ("%s.txt", output_base));
      driver = output_driver_create (&options);
      if (driver == NULL)
        exit (EXIT_FAILURE);
      output_driver_register (driver);
    }

#ifdef HAVE_CAIRO
  /* Render to <base>.pdf. */
  if (render_pdf)
    {
      string_map_clear (&options);
      string_map_insert_nocopy (&options, xstrdup ("output-file"),
                                 xasprintf ("%s.pdf", output_base));
      string_map_insert (&options, "top-margin", "0");
      string_map_insert (&options, "bottom-margin", "0");
      string_map_insert (&options, "left-margin", "0");
      string_map_insert (&options, "right-margin", "0");
      string_map_insert_nocopy (&options, xstrdup ("paper-size"),
                                xasprintf ("%dx%dpt", width * 5, length * 8));
      if (min_break >= 0)
        {
          string_map_insert_nocopy (&options, xstrdup ("min-hbreak"),
                                    xasprintf ("%d", min_break * 5));
          string_map_insert_nocopy (&options, xstrdup ("min-vbreak"),
                                    xasprintf ("%d", min_break * 8));
        }
      driver = output_driver_create (&options);
      if (driver == NULL)
        exit (EXIT_FAILURE);
      output_driver_register (driver);
    }
#endif

  /* Render to <base>.odt. */
  string_map_replace_nocopy (&options, xstrdup ("output-file"),
                             xasprintf ("%s.odt", output_base));
  driver = output_driver_create (&options);
  if (driver == NULL)
    exit (EXIT_FAILURE);
  output_driver_register (driver);

  string_map_destroy (&options);
}

static const char *
parse_options (int argc, char **argv)
{
  int width = 79;
  int length = 66;
  int min_break = -1;

  for (;;)
    {
      enum {
        OPT_WIDTH = UCHAR_MAX + 1,
        OPT_LENGTH,
        OPT_MIN_BREAK,
        OPT_EMPHASIS,
        OPT_BOX,
        OPT_HELP
      };
      static const struct option options[] =
        {
          {"width", required_argument, NULL, OPT_WIDTH},
          {"length", required_argument, NULL, OPT_LENGTH},
          {"min-break", required_argument, NULL, OPT_MIN_BREAK},
          {"transpose", no_argument, &transpose, 1},
          {"emphasis", required_argument, NULL, OPT_EMPHASIS},
          {"box", required_argument, NULL, OPT_BOX},
          {"draw-mode", no_argument, &draw_mode, 1},
          {"no-txt", no_argument, &render_txt, 0},
          {"no-stdout", no_argument, &render_stdout, 0},
          {"pdf", no_argument, &render_pdf, 1},
          {"output", required_argument, NULL, 'o'},
          {"help", no_argument, NULL, OPT_HELP},
          {NULL, 0, NULL, 0},
        };

      int c = getopt_long (argc, argv, "o:", options, NULL);
      if (c == -1)
        break;

      switch (c)
        {
        case OPT_WIDTH:
          width = atoi (optarg);
          break;

        case OPT_LENGTH:
          length = atoi (optarg);
          break;

        case OPT_MIN_BREAK:
          min_break = atoi (optarg);
          break;

        case OPT_EMPHASIS:
          emphasis = optarg;
          break;

        case OPT_BOX:
          box = optarg;
          break;

        case 'o':
          output_base = optarg;
          break;

        case OPT_HELP:
          usage ();

        case 0:
          break;

        case '?':
          exit(EXIT_FAILURE);
          break;

        default:
          NOT_REACHED ();
        }

    }

  configure_drivers (width, length, min_break);

  if (optind + 1 != argc)
    error (1, 0, "exactly one non-option argument required; "
           "use --help for help");
  return argv[optind];
}

static void
usage (void)
{
  printf ("%s, to test rendering of PSPP tables\n"
          "usage: %s [OPTIONS] INPUT\n"
          "\nOptions:\n"
          "  --width=WIDTH   set page width in characters\n"
          "  --length=LINE   set page length in lines\n",
          program_name, program_name);
  exit (EXIT_SUCCESS);
}

static void
replace_newlines (char *p)
{
  char *q;

  for (q = p; *p != '\0'; )
    if (*p == '\\' && p[1] == 'n')
      {
        *q++ = '\n';
        p += 2;
      }
    else
      *q++ = *p++;
  *q = '\0';
}

static struct table *
read_table (FILE *stream)
{
  struct tab_table *tab;
  char buffer[1024];
  int input[6];
  int n_input = 0;
  int nr, nc, hl, hr, ht, hb;
  int r, c;

  if (fgets (buffer, sizeof buffer, stream) == NULL
      || (n_input = sscanf (buffer, "%d %d %d %d %d %d",
                            &input[0], &input[1], &input[2],
                            &input[3], &input[4], &input[5])) < 2)
    error (1, 0, "syntax error reading row and column count");

  nr = input[0];
  nc = input[1];
  hl = n_input >= 3 ? input[2] : 0;
  hr = n_input >= 4 ? input[3] : 0;
  ht = n_input >= 5 ? input[4] : 0;
  hb = n_input >= 6 ? input[5] : 0;

  tab = tab_create (nc, nr);
  tab_headers (tab, hl, hr, ht, hb);
  for (r = 0; r < nr; r++)
    for (c = 0; c < nc; c++)
      if (tab_cell_is_empty (tab, c, r))
        {
          char *new_line;
          char *text;
          int rs, cs;

          if (fgets (buffer, sizeof buffer, stream) == NULL)
            error (1, 0, "unexpected end of input reading row %d, column %d",
                   r, c);
          new_line = strchr (buffer, '\n');
          if (new_line != NULL)
            *new_line = '\0';

          text = buffer;
          if (sscanf (text, "%d*%d", &rs, &cs) == 2)
            {
              while (*text != ' ' && *text != '\0')
                text++;
              if (*text == ' ')
                text++;
            }
          else
            {
              rs = 1;
              cs = 1;
            }

          while (*text && strchr ("<>^,@", *text))
            switch (*text++)
              {
              case '<':
                tab_vline (tab, TAL_1, c, r, r + rs - 1);
                break;

              case '>':
                tab_vline (tab, TAL_1, c + cs, r, r + rs - 1);
                break;

              case '^':
                tab_hline (tab, TAL_1, c, c + cs - 1, r);
                break;

              case ',':
                tab_hline (tab, TAL_1, c, c + cs - 1, r + rs);
                break;

              case '@':
                tab_box (tab, TAL_1, TAL_1, -1, -1, c, r,
                         c + cs - 1, r + rs - 1);
                break;

              default:
                NOT_REACHED ();
              }

          replace_newlines (text);

          tab_joint_text (tab, c, r, c + cs - 1, r + rs - 1, 0, text);
        }

  if (getc (stream) != EOF)
    error (1, 0, "unread data at end of input");

  return &tab->table;
}

static void
draw (FILE *stream)
{
  char buffer[1024];
  int line = 0;

  while (fgets (buffer, sizeof buffer, stream))
    {
      char text[sizeof buffer];
      int length;
      int emph;
      int x, y;

      line++;
      if (strchr ("#\r\n", buffer[0]))
        continue;

      if (sscanf (buffer, "%d %d %d %[^\n]", &x, &y, &emph, text) == 4)
        ascii_test_write (ascii_driver, text, x, y, emph ? TAB_EMPH : 0);
      else if (sscanf (buffer, "set-length %d %d", &y, &length) == 2)
        ascii_test_set_length (ascii_driver, y, length);
      else
        error (1, 0, "line %d has invalid format", line);
    }
}
