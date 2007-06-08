/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

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


