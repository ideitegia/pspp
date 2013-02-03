/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011, 2013 Free Software Foundation, Inc.

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
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistr.h>

#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/misc.h"
#include "language/lexer/segment.h"

#include "gl/error.h"
#include "gl/minmax.h"
#include "gl/progname.h"
#include "gl/read-file.h"
#include "gl/xalloc.h"
#include "gl/xmemdup0.h"

/* -a/--auto, -b/--batch, -i/--interactive: syntax mode. */
static enum segmenter_mode mode = SEG_MODE_AUTO;

/* -v, --verbose: Print row and column information. */
static bool verbose;

/* -1, --one-byte: Feed in one byte at a time? */
static bool one_byte;

/* -0, --truncations: Check that every truncation of input yields a result. */
static bool check_truncations;

static const char *parse_options (int argc, char **argv);
static void usage (void) NO_RETURN;

static void check_segmentation (const char *input, size_t length,
                                bool print_segments);

int
main (int argc, char *argv[])
{
  const char *file_name;
  size_t length;
  char *input;

  set_program_name (argv[0]);
  file_name = parse_options (argc, argv);

  /* Read from stdin into 'input'.  Ensure that 'input' ends in a new-line
     followed by a null byte. */
  input = (!strcmp (file_name, "-")
           ? fread_file (stdin, &length)
           : read_file (file_name, &length));
  if (input == NULL)
    error (EXIT_FAILURE, errno, "reading %s failed", file_name);

  if (!check_truncations)
    {
      input = xrealloc (input, length + 3);
      if (length == 0 || input[length - 1] != '\n')
        input[length++] = '\n';
      input[length++] = '\0';

      check_segmentation (input, length, true);
    }
  else
    {
      size_t test_len;

      for (test_len = 0; test_len <= length; test_len++)
        {
          char *copy = xmemdup0 (input, test_len);
          check_segmentation (copy, test_len + 1, false);
          free (copy);
        }
    }
  free (input);

  return 0;
}

static void
check_segmentation (const char *input, size_t length, bool print_segments)
{
  size_t offset, line_number, line_offset;
  struct segmenter s;
  int prev_type;

  segmenter_init (&s, mode);

  line_number = 1;
  line_offset = 0;
  prev_type = -1;
  for (offset = 0; offset < length; )
    {
      enum segment_type type;
      const char *type_name, *p;
      int n;

      if (one_byte)
        {
          int n_newlines = 0;
          int i;

          for (i = 0; i <= length - offset; i++)
            {
              /* Make a copy to ensure that segmenter_push() isn't actually
                 looking ahead. */
              char *copy;

              if (i > 0 && input[offset + i - 1] == '\n')
                n_newlines++;

              copy = xmemdup (input + offset, i);
              n = segmenter_push (&s, copy, i, &type);
              free (copy);

              if (n >= 0)
                break;
            }
          assert (n_newlines <= 2);
        }
      else
        n = segmenter_push (&s, input + offset, length - offset, &type);

      if (n < 0)
        error (EXIT_FAILURE, 0, "segmenter_push returned -1 at offset %zu",
               offset);
      assert (offset + n <= length);

      if (type == SEG_NEWLINE)
        assert ((n == 1 && input[offset] == '\n')
                || (n == 2
                    && input[offset] == '\r' && input[offset + 1] == '\n'));
      else
        assert (memchr (&input[offset], '\n', n) == NULL);

      if (!print_segments)
        {
          offset += n;
          continue;
        }

      if (!verbose)
        {
          if (prev_type != SEG_SPACES && prev_type != -1
              && type == SEG_SPACES && n == 1 && input[offset] == ' ')
            {
              printf ("    space\n");
              offset++;
              prev_type = -1;
              continue;
            }
        }
      if (prev_type != -1)
        putchar ('\n');
      prev_type = type;

      if (verbose)
        printf ("%2zu:%2zu: ", line_number, offset - line_offset);

      type_name = segment_type_to_string (type);
      for (p = type_name; *p != '\0'; p++)
        putchar (tolower ((unsigned char) *p));
      if (n > 0)
        {
          int i;

          for (i = MIN (15, strlen (type_name)); i < 16; i++)
            putchar (' ');
          for (i = 0; i < n; )
            {
              const uint8_t *u_input = CHAR_CAST (const uint8_t *, input);
              ucs4_t uc;
              int mblen;

              mblen = u8_mbtoucr (&uc, u_input + (offset + i), n - i);
              if (mblen < 0)
                {
                  int j;

                  mblen = u8_mbtouc (&uc, u_input + (offset + i), n - i);
                  putchar ('<');
                  for (j = 0; j < mblen; j++)
                    {
                      if (j > 0)
                        putchar (' ');
                      printf ("%02x", input[offset + i + j]);
                    }
                  putchar ('>');
                }
              else
                {
                  switch (uc)
                    {
                    case ' ':
                      printf ("_");
                      break;

                    case '_':
                      printf ("\\_");
                      break;

                    case '\\':
                      printf ("\\\\");
                      break;

                    case '\t':
                      printf ("\\t");
                      break;

                    case '\r':
                      printf ("\\r");
                      break;

                    case '\n':
                      printf ("\\n");
                      break;

                    case '\v':
                      printf ("\\v");
                      break;

                    default:
                      if (uc < 0x20 || uc == 0x00a0)
                        printf ("<U+%04X>", uc);
                      else
                        fwrite (input + offset + i, 1, mblen, stdout);
                      break;
                    }
                }

              i += mblen;
            }
        }

      offset += n;
      if (type == SEG_NEWLINE)
        {
          enum prompt_style prompt;

          line_number++;
          line_offset = offset;

          prompt = segmenter_get_prompt (&s);
          printf (" (%s)\n", prompt_style_to_string (prompt));
        }
    }

  if (print_segments)
    putchar ('\n');
}

static const char *
parse_options (int argc, char **argv)
{
  for (;;)
    {
      static const struct option options[] =
        {
          {"one-byte", no_argument, NULL, '1'},
          {"truncations", no_argument, NULL, '0'},
          {"auto", no_argument, NULL, 'a'},
          {"batch", no_argument, NULL, 'b'},
          {"interactive", no_argument, NULL, 'i'},
          {"verbose", no_argument, NULL, 'v'},
          {"help", no_argument, NULL, 'h'},
          {NULL, 0, NULL, 0},
        };

      int c = getopt_long (argc, argv, "01abivh", options, NULL);
      if (c == -1)
        break;

      switch (c)
        {
        case '1':
          one_byte = true;
          break;

        case '0':
          check_truncations = true;
          break;

        case 'a':
          mode = SEG_MODE_AUTO;
          break;

        case 'b':
          mode = SEG_MODE_BATCH;
          break;

        case 'i':
          mode = SEG_MODE_INTERACTIVE;
          break;

        case 'v':
          verbose = true;
          break;

        case 'h':
          usage ();

        case 0:
          break;

        case '?':
          exit (EXIT_FAILURE);
          break;

        default:
          NOT_REACHED ();
        }

    }

  if (optind + 1 != argc)
    error (1, 0, "exactly one non-option argument required; "
           "use --help for help");
  return argv[optind];
}

static void
usage (void)
{
  printf ("\
%s, to test breaking PSPP syntax into lexical segments\n\
usage: %s [OPTIONS] INPUT\n\
\n\
Options:\n\
  -1, --one-byte      feed one byte at a time\n\
  -0, --truncations   check null truncation of each prefix of input\n\
  -a, --auto          use \"auto\" syntax mode\n\
  -b, --batch         use \"batch\" syntax mode\n\
  -i, --interactive   use \"interactive\" syntax mode (default)\n\
  -v, --verbose       include rows and column numbers in output\n\
  -h, --help          print this help message\n",
          program_name, program_name);
  exit (EXIT_SUCCESS);
}
