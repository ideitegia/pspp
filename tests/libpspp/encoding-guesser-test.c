/* PSPP - a program for statistical analysis.
   Copyright (C) 2011 Free Software Foundation, Inc.

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

#include "libpspp/encoding-guesser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libpspp/i18n.h"

#include "gl/error.h"
#include "gl/progname.h"
#include "gl/xalloc.h"

static void
usage (void)
{
  printf ("usage: %s [OTHER_ENCODING] [BUFSIZE] < INPUT\n"
          "where OTHER_ENCODING is the fallback encoding (default taken\n"
          "                     from the current locale)\n"
          "  and BUFSIZE is the buffer size (default %d)\n",
          program_name, ENCODING_GUESS_MIN);
  exit (0);
}

int
main (int argc, char *argv[])
{
  const char *encoding, *guess;
  char *buffer;
  int bufsize;
  size_t n;
  int i;

  set_program_name (argv[0]);

  i18n_init ();

  encoding = NULL;
  bufsize = 0;
  for (i = 1; i < argc; i++)
    {
      const char *arg = argv[i];
      if (!strcmp (arg, "--help"))
        usage ();
      else if (isdigit (arg[0]) && bufsize == 0)
        {
          bufsize = atoi (arg);
          if (bufsize < ENCODING_GUESS_MIN)
            error (1, 0, "buffer size %s is less than minimum size %d",
                   arg, ENCODING_GUESS_MIN);
        }
      else if (!isdigit (arg[0]) && encoding == NULL)
        encoding = arg;
      else
        error (1, 0, "bad syntax; use `%s --help' for help", program_name);
    }

  if (bufsize == 0)
    bufsize = ENCODING_GUESS_MIN;

  buffer = xmalloc (bufsize);

  n = fread (buffer, 1, bufsize, stdin);
  guess = encoding_guess_head_encoding (encoding, buffer, n);
  if (!strcmp (guess, "ASCII") && encoding_guess_encoding_is_auto (encoding))
    while (n > 0)
      {
        size_t n_ascii = encoding_guess_count_ascii (buffer, n);
        if (n == n_ascii)
          n = fread (buffer, 1, bufsize, stdin);
        else
          {
            memmove (buffer, buffer + n_ascii, n - n_ascii);
            n -= n_ascii;
            n += fread (buffer + n, 1, bufsize - n, stdin);

            guess = encoding_guess_tail_encoding (encoding, buffer, n);
            break;
          }
      }
  puts (guess);
  free (buffer);

  return 0;
}
