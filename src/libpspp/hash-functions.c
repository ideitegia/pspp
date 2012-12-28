/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2008, 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include "libpspp/hash-functions.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

/* Based on http://burtleburtle.net/bob/c/lookup3.c, by Bob
   Jenkins <bob_jenkins@burtleburtle.net>, as retrieved on April
   8, 2009.  The license information there says the following:
   "You can use this free for any purpose.  It's in the public
   domain.  It has no warranty." and "You may use this code any
   way you wish, private, educational, or commercial.  It's
   free." */

#define HASH_ROT(x, k) (((x) << (k)) | ((x) >> (32 - (k))))

#define HASH_MIX(a, b, c)                               \
        do                                              \
          {                                             \
            a -= c;  a ^= HASH_ROT (c,  4);  c += b;    \
            b -= a;  b ^= HASH_ROT (a,  6);  a += c;    \
            c -= b;  c ^= HASH_ROT (b,  8);  b += a;    \
            a -= c;  a ^= HASH_ROT (c, 16);  c += b;    \
            b -= a;  b ^= HASH_ROT (a, 19);  a += c;    \
            c -= b;  c ^= HASH_ROT (b,  4);  b += a;    \
          }                                             \
        while (0)

#define HASH_FINAL(a, b, c)                     \
        do                                      \
          {                                     \
            c ^= b; c -= HASH_ROT (b, 14);      \
            a ^= c; a -= HASH_ROT (c, 11);      \
            b ^= a; b -= HASH_ROT (a, 25);      \
            c ^= b; c -= HASH_ROT (b, 16);      \
            a ^= c; a -= HASH_ROT (c, 4);       \
            b ^= a; b -= HASH_ROT (a, 14);      \
            c ^= b; c -= HASH_ROT (b, 24);      \
          }                                     \
        while (0)

/* Returns a hash value for the N bytes starting at P, starting
   from BASIS. */
unsigned int
hash_bytes (const void *p_, size_t n, unsigned int basis)
{
  const uint8_t *p = p_;
  uint32_t a, b, c;
  uint32_t tmp[3];

  a = b = c = 0xdeadbeef + n + basis;

  while (n >= 12)
    {
      memcpy (tmp, p, 12);
      a += tmp[0];
      b += tmp[1];
      c += tmp[2];
      HASH_MIX (a, b, c);
      n -= 12;
      p += 12;
    }

  if (n > 0)
    {
      memset (tmp, 0, 12);
      memcpy (tmp, p, n);
      a += tmp[0];
      b += tmp[1];
      c += tmp[2];
    }

  HASH_FINAL (a, b, c);
  return c;
}

/* Returns a hash value for null-terminated string S, starting
   from BASIS. */
unsigned int
hash_string (const char *s, unsigned int basis)
{
  return hash_bytes (s, strlen (s), basis);
}

/* Returns a hash value for integer X, starting from BASIS. */
unsigned int
hash_int (int x, unsigned int basis)
{
  x -= x << 6;
  x ^= x >> 17;
  x -= x << 9;
  x ^= x << 4;
  x -= x << 3;
  x ^= x << 10;
  x ^= x >> 15;
  return x + basis;
}

/* Returns a hash value for double D, starting from BASIS. */
unsigned int
hash_double (double d, unsigned int basis)
{
  if (sizeof (double) == 8)
    {
      uint32_t tmp[2];
      uint32_t a, b, c;

      a = b = c = 0xdeadbeef + 8 + basis;

      memcpy (tmp, &d, 8);
      a += tmp[0];
      b += tmp[1];
      HASH_FINAL (a, b, c);
      return c;
    }
  else
    return hash_bytes (&d, sizeof d, basis);
}

/* Returns a hash value for pointer P, starting from BASIS. */
unsigned int
hash_pointer (const void *p, unsigned int basis)
{
  /* Casting to uintptr_t before casting to int suppresses a GCC warning about
     on 64-bit platforms. */
  return hash_int ((int) (uintptr_t) p, basis);
}
