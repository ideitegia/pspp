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

#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/misc.h"
#include "language/lexer/scan.h"
#include "language/lexer/token.h"

#include "gl/error.h"
#include "gl/ftoastr.h"
#include "gl/progname.h"
#include "gl/read-file.h"
#include "gl/xalloc.h"

/* -a/--auto, -b/--batch, -i/--interactive: syntax mode. */
static enum segmenter_mode mode = SEG_MODE_AUTO;

static const char *parse_options (int argc, char **argv);
static void usage (void) NO_RETURN;

int
main (int argc, char *argv[])
{
  const char *file_name;
  size_t length;
  char *input;

  struct string_lexer slex;
  bool more;

  set_program_name (argv[0]);
  file_name = parse_options (argc, argv);

  /* Read from stdin into 'input'.  Ensure that 'input' ends in a new-line
     followed by a null byte. */
  input = (!strcmp (file_name, "-")
           ? fread_file (stdin, &length)
           : read_file (file_name, &length));
  if (input == NULL)
    error (EXIT_FAILURE, errno, "reading %s failed", file_name);
  input = xrealloc (input, length + 3);
  if (length == 0 || input[length - 1] != '\n')
    input[length++] = '\n';
  input[length++] = '\0';

  string_lexer_init (&slex, input, mode);
  do
    {
      struct token token;

      more = string_lexer_next (&slex, &token);

      printf ("%s", scan_type_to_string (token.type));
      if (token.number != 0.0)
        {
          double x = token.number;

          if (x > LONG_MIN && x <= LONG_MAX && floor (x) == x)
            printf (" %ld", (long int) x);
          else
            printf (" %.3g", x);
        }
      if (token.string.string != NULL || token.string.length > 0)
        printf (" \"%.*s\"", (int) token.string.length, token.string.string);
      printf ("\n");

      token_destroy (&token);
    }
  while (more);

  free (input);

  return 0;
}

static const char *
parse_options (int argc, char **argv)
{
  for (;;)
    {
      static const struct option options[] =
        {
          {"auto", no_argument, NULL, 'a'},
          {"batch", no_argument, NULL, 'b'},
          {"interactive", no_argument, NULL, 'i'},
          {"help", no_argument, NULL, 'h'},
          {NULL, 0, NULL, 0},
        };

      int c = getopt_long (argc, argv, "abih", options, NULL);
      if (c == -1)
        break;

      switch (c)
        {
        case 'a':
          mode = SEG_MODE_AUTO;
          break;

        case 'b':
          mode = SEG_MODE_BATCH;
          break;

        case 'i':
          mode = SEG_MODE_INTERACTIVE;
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
%s, to test breaking PSPP syntax into tokens\n\
usage: %s [OPTIONS] INPUT\n\
\n\
Options:\n\
  -1, --one-segment   feed one segment at a time\n\
  -a, --auto          use \"auto\" syntax mode\n\
  -b, --batch         use \"batch\" syntax mode\n\
  -i, --interactive   use \"interactive\" syntax mode (default)\n\
  -v, --verbose       include rows and column numbers in output\n\
  -h, --help          print this help message\n",
          program_name, program_name);
  exit (EXIT_SUCCESS);
}
