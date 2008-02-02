/* PSPP - a program for statistical analysis.
   Copyright (C) 2008 Free Software Foundation, Inc.

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

/* Currently running test. */
static const char *test_name;

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
  str_format_26adic (number, string, sizeof string);
  if (strcmp (string, expected_string))
    {
      printf ("base-26 of %lu: expected \"%s\", got \"%s\"\n",
              number, expected_string, string);
      check_die ();
    }
}

static void
test_str_format_26adic (void)
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

/* Runs TEST_FUNCTION and prints a message about NAME. */
static void
run_test (void (*test_function) (void), const char *name)
{
  test_name = name;
  putchar ('.');
  fflush (stdout);
  test_function ();
}

int
main (void)
{
  run_test (test_str_format_26adic, "format 26-adic strings");
  putchar ('\n');

  return 0;
}
