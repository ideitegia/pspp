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

#include <stdio.h>
#include <stdlib.h>

#include "libpspp/i18n.h"

int
main (int argc, char *argv[])
{
  char *s;

  if (argc != 4)
    {
      fprintf (stderr,
               "usage: %s FROM TO STRING\n"
               "where FROM is the source encoding,\n"
               "      TO is the target encoding,\n"
               "      and STRING is the text to recode.\n",
               argv[0]);
      return EXIT_FAILURE;
    }

  i18n_init ();
  s = recode_string (argv[2], argv[1], argv[3], -1);
  puts (s);
  free (s);

  return 0;
}
