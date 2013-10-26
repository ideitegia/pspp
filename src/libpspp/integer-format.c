/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2010, 2011, 2013 Free Software Foundation, Inc.

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

#include "libpspp/integer-format.h"

#include <assert.h>

/* Returns true if FORMAT is a valid integer format. */
static inline bool
is_integer_format (enum integer_format format)
{
  return (format == INTEGER_MSB_FIRST
          || format == INTEGER_LSB_FIRST
          || format == INTEGER_VAX);
}

/* Converts the CNT bytes in INTEGER from SRC integer_format to DST
   integer_format. */
void
integer_convert (enum integer_format src, const void *from,
                 enum integer_format dst, void *to,
                 size_t cnt)
{
  if (src != dst)
    integer_put (integer_get (src, from, cnt), dst, to, cnt);
  else if (from != to)
    memcpy (to, from, cnt);
}

/* Returns the value of the CNT-byte integer at FROM, which is in
   the given FORMAT. */
uint64_t
integer_get (enum integer_format format, const void *from_, size_t cnt)
{
  const uint8_t *from = from_;
  uint64_t value = 0;
  size_t i;

  assert (is_integer_format (format));
  assert (cnt <= 8);

  switch (format)
    {
    case INTEGER_MSB_FIRST:
      for (i = 0; i < cnt; i++)
        value = (value << 8) | from[i];
      break;
    case INTEGER_LSB_FIRST:
      for (i = 0; i < cnt; i++)
        value = (value << 8) | from[cnt - i - 1];
      break;
    case INTEGER_VAX:
      for (i = 0; i < (cnt & ~1); i++)
        value = (value << 8) | from[i ^ 1];
      if (cnt & 1)
        value = (value << 8) | from[cnt - 1];
      break;
    }

  return value;
}

/* Stores VALUE as a CNT-byte integer at TO, in the given
   FORMAT. */
void
integer_put (uint64_t value, enum integer_format format, void *to_, size_t cnt)
{
  uint8_t *to = to_;
  size_t i;

  assert (is_integer_format (format));
  assert (cnt <= 8);

  value <<= 8 * (8 - cnt);

  switch (format)
    {
    case INTEGER_MSB_FIRST:
      for (i = 0; i < cnt; i++)
        {
          to[i] = value >> 56;
          value <<= 8;
        }
      break;
    case INTEGER_LSB_FIRST:
      for (i = 0; i < cnt; i++)
        {
          to[cnt - i - 1] = value >> 56;
          value <<= 8;
        }
      break;
    case INTEGER_VAX:
      for (i = 0; i < (cnt & ~1); i++)
        {
          to[i ^ 1] = value >> 56;
          value <<= 8;
        }
      if (cnt & 1)
        to[cnt - 1] = value >> 56;
      break;
    }
}

/* Returns true if bytes with index IDX1 and IDX2 in VALUE differ
   in value. */
static inline bool
bytes_differ (uint64_t value, unsigned int idx1, unsigned int idx2)
{
  uint8_t byte1 = value >> (idx1 * 8);
  uint8_t byte2 = value >> (idx2 * 8);
  return byte1 != byte2;
}

/* Attempts to identify the integer format in which the LENGTH
   bytes in INTEGER represent the given EXPECTED_VALUE.  Returns
   true if successful, false otherwise.  On success, stores the
   format in *FORMAT. */
bool
integer_identify (uint64_t expected_value, const void *integer, size_t length,
                  enum integer_format *format)
{
  /* Odd-length integers are confusing. */
  assert (length % 2 == 0);

  /* LENGTH must be greater than 2 because VAX format is
     equivalent to little-endian for 2-byte integers. */
  assert (length > 2);

  /* EXPECTED_VALUE must contain different byte values, because
     otherwise all formats are identical. */
  assert (bytes_differ (expected_value, 0, 1)
          || bytes_differ (expected_value, 0, 2)
          || bytes_differ (expected_value, 0, 3)
          || (length > 4
              && (bytes_differ (expected_value, 0, 4)
                  || bytes_differ (expected_value, 0, 5)))
          || (length > 6
              && (bytes_differ (expected_value, 0, 6)
                  || bytes_differ (expected_value, 0, 7))));

  if (integer_get (INTEGER_MSB_FIRST, integer, length) == expected_value)
    *format = INTEGER_MSB_FIRST;
  else if (integer_get (INTEGER_LSB_FIRST, integer, length) == expected_value)
    *format = INTEGER_LSB_FIRST;
  else if (integer_get (INTEGER_VAX, integer, length) == expected_value)
    *format = INTEGER_VAX;
  else
    return false;

  return true;
}
