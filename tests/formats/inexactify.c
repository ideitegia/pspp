/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2010 Free Software Foundation, Inc.

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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Replaces insignificant digits by # to facilitate textual
   comparisons.  Not a perfect solution to the general-purpose
   comparison problem, because rounding that affects earlier
   digits can still cause differences. */
int
main (void)
{
  bool in_quotes = false;
  bool in_exponent = false;
  int digits = 0;

  for (;;)
    {
      int c = getchar ();
      if (c == EOF)
        break;
      else if (c == '\n')
        in_quotes = false;
      else if (c == '"')
        {
          in_quotes = !in_quotes;
          in_exponent = false;
          digits = 0;
        }
      else if (in_quotes && !in_exponent)
        {
          if (strchr ("+dDeE", c) != NULL || (c == '-' && digits))
            in_exponent = true;
          else if (strchr ("0123456789}JKLMNOPQR", c) != NULL)
            {
              if (digits || c >= '1')
                digits++;
              if (digits > 13)
                c = isdigit (c) ? '#' : '@';
            }
        }
      putchar (c);
    }
  return EXIT_SUCCESS;
}
