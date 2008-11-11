/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2008 Free Software Foundation, Inc.

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
#include <libpspp/hash-functions.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

/* Fowler-Noll-Vo hash constants, for 32-bit word sizes. */
#define FNV_32_PRIME 16777619u
#define FNV_32_BASIS 2166136261u

/* Fowler-Noll-Vo 32-bit hash, for bytes. */
unsigned
hsh_hash_bytes (const void *buf_, size_t size)
{
  const unsigned char *buf = (const unsigned char *) buf_;
  unsigned hash;

  assert (buf != NULL);

  hash = FNV_32_BASIS;
  while (size-- > 0)
    hash = (hash * FNV_32_PRIME) ^ *buf++;

  return hash;
}

/* Fowler-Noll-Vo 32-bit hash, for strings. */
unsigned
hsh_hash_string (const char *s_)
{
  const unsigned char *s = (const unsigned char *) s_;
  unsigned hash;

  assert (s != NULL);

  hash = FNV_32_BASIS;
  while (*s != '\0')
    hash = (hash * FNV_32_PRIME) ^ *s++;

  return hash;
}

/* Fowler-Noll-Vo 32-bit hash, for case-insensitive strings. */
unsigned
hsh_hash_case_string (const char *s_)
{
  const unsigned char *s = (const unsigned char *) s_;
  unsigned hash;

  assert (s != NULL);

  hash = FNV_32_BASIS;
  while (*s != '\0')
    hash = (hash * FNV_32_PRIME) ^ toupper (*s++);

  return hash;
}

/* Hash for ints. */
unsigned
hsh_hash_int (int i)
{
  return hsh_hash_bytes (&i, sizeof i);
}

/* Hash for double. */
unsigned
hsh_hash_double (double d)
{
  if (!isnan (d))
    return hsh_hash_bytes (&d, sizeof d);
  else
    return 0;
}
