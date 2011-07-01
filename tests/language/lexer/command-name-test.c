/* PSPP - a program for statistical analysis.
   Copyright (C) 2010 Free Software Foundation, Inc.

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
#include "language/lexer/command-name.h"

#include "gl/error.h"
#include "gl/progname.h"

static char **commands, **strings;
static size_t n_commands, n_strings;

static void parse_options (int argc, char **argv);
static void usage (void) NO_RETURN;

int
main (int argc, char *argv[])
{
  size_t i;

  set_program_name (argv[0]);
  parse_options (argc, argv);

  for (i = 0; i < n_strings; i++)
    {
      const char *string = strings[i];
      struct command_matcher cm;
      const char *best;
      size_t j;

      if (i > 0)
        putchar ('\n');
      printf ("string=\"%s\":\n", string);
      for (j = 0; j < n_commands; j++)
        {
          const char *command = commands[j];
          int missing_words;
          bool match, exact;

          match = command_match (ss_cstr (command), ss_cstr (string),
                                 &exact, &missing_words);
          printf ("\tcommand=\"%s\" match=%s",
                  command, match ? "yes" : "no");
          if (match)
            printf (" exact=%s missing_words=%d",
                    exact ? "yes" : "no", missing_words);
          putchar ('\n');
        }

      command_matcher_init (&cm, ss_cstr (string));
      for (j = 0; j < n_commands; j++)
        command_matcher_add (&cm, ss_cstr (commands[j]), commands[j]);
      best = command_matcher_get_match (&cm);
      printf ("match: %s, missing_words=%d\n",
              best ? best : "none", command_matcher_get_missing_words (&cm));
      command_matcher_destroy (&cm);
    }

  return 0;
}

static void
parse_options (int argc, char **argv)
{
  int breakpoint;

  for (;;)
    {
      static const struct option options[] =
        {
          {"help", no_argument, NULL, 'h'},
          {NULL, 0, NULL, 0},
        };

      int c = getopt_long (argc, argv, "h", options, NULL);
      if (c == -1)
        break;

      switch (c)
        {
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

  for (breakpoint = optind; ; breakpoint++)
    if (breakpoint >= argc)
      error (1, 0, "missing ',' on command line; use --help for help");
    else if (!strcmp (argv[breakpoint], ","))
      break;

  commands = &argv[optind];
  n_commands = breakpoint - optind;

  strings = &argv[breakpoint + 1];
  n_strings = argc - (breakpoint + 1);

  if (n_commands == 0 || n_strings == 0)
    error (1, 0, "must specify at least one command and one string; "
           "use --help for help");
}

static void
usage (void)
{
  printf ("\
%s, to match PSPP command names\n\
usage: %s [OPTIONS] COMMAND... , STRING...\n\
\n\
Options:\n\
  -h, --help          print this help message\n",
          program_name, program_name);
  exit (EXIT_SUCCESS);
}
