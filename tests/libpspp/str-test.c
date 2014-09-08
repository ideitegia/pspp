/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2010, 2014 Free Software Foundation, Inc.

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

#include <libpspp/str.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Exit with a failure code.
   (Place a breakpoint on this function while debugging.) */
static void
check_die (void)
{
  exit (EXIT_FAILURE);
}

static void
check_26adic (unsigned long int number, const char *expected_string)
{
  char string[8];
  str_format_26adic (number, true, string, sizeof string);
  if (strcmp (string, expected_string))
    {
      printf ("base-26 of %lu: expected \"%s\", got \"%s\"\n",
              number, expected_string, string);
      check_die ();
    }
}

static void
test_format_26adic (void)
{
  check_26adic (0, "");
  check_26adic (1, "A");
  check_26adic (2, "B");
  check_26adic (26, "Z");
  check_26adic (27, "AA");
  check_26adic (28, "AB");
  check_26adic (29, "AC");
  check_26adic (18278, "ZZZ");
  check_26adic (18279, "AAAA");
  check_26adic (19010, "ABCD");
}

/* Main program. */

struct test
  {
    const char *name;
    const char *description;
    void (*function) (void);
  };

static const struct test tests[] =
  {
    {
      "format-26adic",
      "format 26-adic strings",
      test_format_26adic
    }
  };

enum { N_TESTS = sizeof tests / sizeof *tests };

int
main (int argc, char *argv[])
{
  int i;

  if (argc != 2)
    {
      fprintf (stderr, "exactly one argument required; use --help for help\n");
      return EXIT_FAILURE;
    }
  else if (!strcmp (argv[1], "--help"))
    {
      printf ("%s: test string library\n"
              "usage: %s TEST-NAME\n"
              "where TEST-NAME is one of the following:\n",
              argv[0], argv[0]);
      for (i = 0; i < N_TESTS; i++)
        printf ("  %s\n    %s\n", tests[i].name, tests[i].description);
      return 0;
    }
  else
    {
      for (i = 0; i < N_TESTS; i++)
        if (!strcmp (argv[1], tests[i].name))
          {
            tests[i].function ();
            return 0;
          }

      fprintf (stderr, "unknown test %s; use --help for help\n", argv[1]);
      return EXIT_FAILURE;
    }
}
