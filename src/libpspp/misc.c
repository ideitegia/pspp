/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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
#include "misc.h"
#include <gl/ftoastr.h>

/* Returns the number of digits in X. */
int
intlog10 (unsigned x)
{
  int digits = 0;

  do
    {
      digits++;
      x /= 10;
    }
  while (x > 0);

  return digits;
}


/* A locale independent version of dtoastr (from gnulib) */
int
c_dtoastr (char *buf, size_t bufsize, int flags, int width, double x)
{
  int i;
  int result = dtoastr (buf, bufsize, flags, width, x);

  /* Replace the first , (if any) by a . */
  for (i = 0; i < result; ++i)
    {
      if (buf[i] == ',')
	{
	  buf[i] = '.';
	  break;
	}
    }

  return result;
}
