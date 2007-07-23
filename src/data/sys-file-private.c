/* PSPP - a program for statistical analysis.
   Copyright (C) 2006 Free Software Foundation, Inc.

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
#include "sys-file-private.h"

#include <data/value.h>
#include <libpspp/assertion.h>

/* Return the number of bytes used when writing case_data for a variable
   of WIDTH */
int
sfm_width_to_bytes (int width)
{
  assert (width >= 0);

  if (width == 0)
    return MAX_SHORT_STRING;
  else if (width < MIN_VERY_LONG_STRING)
    return ROUND_UP (width, MAX_SHORT_STRING);
  else
    {
      int chunks = width / EFFECTIVE_LONG_STRING_LENGTH ;
      int remainder = width % EFFECTIVE_LONG_STRING_LENGTH ;
      int bytes = remainder + (chunks * MIN_VERY_LONG_STRING);
      return ROUND_UP (bytes, MAX_SHORT_STRING);
    }
}

/* Returns the number of "segments" used for writing case data
   for a variable of the given WIDTH.  A segment is a physical
   variable in the system file that represents some piece of a
   logical variable as seen by a PSPP user.  Only very long
   string variables have more than one segment. */
int
sfm_width_to_segments (int width)
{
  assert (width >= 0);

  return (width < MIN_VERY_LONG_STRING ? 1
          : DIV_RND_UP (width, EFFECTIVE_LONG_STRING_LENGTH));
}
