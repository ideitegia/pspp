/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include "random.h"
#include "error.h"
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include "alloc.h"
#include "magic.h"
#include "settings.h"

/* Random number generator. */
struct rng 
  {
    /* RC4-based random bytes. */
    uint8_t s[256];
    uint8_t i, j;

    /* Normal distribution. */
    double next_normal;
  };


/* Return a `random' seed by using the real time clock */
unsigned long
random_seed(void)
{
  time_t t;
  
  time(&t);

  return (unsigned long) t;
}

/* Creates a new random number generator, seeds it based on
   the current time, and returns it. */
struct rng *
rng_create (void) 
{
  struct rng *rng;
  static unsigned long seed=0;
  unsigned long s;

  rng = xmalloc (sizeof *rng);


  if ( seed_is_set(&s) ) 
    {
      seed = s;
    }
  else if ( seed == 0 ) 
    {
      seed = random_seed();
    }
  assert(seed);
  /* 
  if (t == 0 || set_seed_used)
  {
    if (set_seed == NOT_LONG) 
      time (&t);
    else
      t = set_seed;
    set_seed_used=0;
  }
  else
    t++;
  */
  rng_seed (rng, &seed, sizeof seed);
  rng->next_normal = NOT_DOUBLE;
  return rng;
}

/* Destroys RNG. */
void
rng_destroy (struct rng *rng) 
{
  free (rng);
}

/* Swap bytes. */
static void
swap_byte (uint8_t *a, uint8_t *b) 
{
  uint8_t t = *a;
  *a = *b;
  *b = t;
}

/* Seeds RNG based on the SIZE bytes in BUF.
   At most the first 256 bytes of BUF are used. */
void
rng_seed (struct rng *rng, const void *key_, size_t size) 
{
  const uint8_t *key = key_;
  size_t key_idx;
  uint8_t *s;
  int i, j;

  assert (rng != NULL);

  s = rng->s;
  rng->i = rng->j = 0;
  for (i = 0; i < 256; i++) 
    s[i] = i;
  for (key_idx = 0, i = j = 0; i < 256; i++) 
    {
      j = (j + s[i] + key[key_idx]) & 255;
      swap_byte (s + i, s + j);
      if (++key_idx >= size)
        key_idx = 0;
    }
}

/* Reads SIZE random bytes from RNG into BUF. */
void
rng_get_bytes (struct rng *rng, void *buf_, size_t size) 
{
  uint8_t *buf = buf_;
  uint8_t *s;
  uint8_t i, j;

  assert (rng != 0);

  s = rng->s;
  i = rng->i;
  j = rng->j;
  while (size-- > 0) 
    {
      i += 1;
      j += s[i];
      swap_byte (s + i, s + j);
      *buf++ = s[(s[i] + s[j]) & 255];
    }
  rng->i = i;
  rng->j = j;
}

/* Returns a random int in the range [0, INT_MAX]. */
int
rng_get_int (struct rng *rng) 
{
  int value;

  do 
    {
      rng_get_bytes (rng, &value, sizeof value);
      value = abs (value);
    }
  while (value < 0);
   
  return value;
}

/* Returns a random unsigned in the range [0, UINT_MAX]. */
unsigned
rng_get_unsigned (struct rng *rng) 
{
  unsigned value;

  rng_get_bytes (rng, &value, sizeof value);
  return value;
}

/* Returns a random number from the uniform distribution with
   range [0,1). */
double
rng_get_double (struct rng *rng) 
{
  for (;;) 
    {
      unsigned long ulng;
      double dbl;
  
      rng_get_bytes (rng, &ulng, sizeof ulng);
      dbl = ulng / (ULONG_MAX + 1.0);
      if (dbl >= 0 && dbl < 1)
        return dbl;
    }
}

/* Returns a random number from the distribution with mean 0 and
   standard deviation 1.  (Multiply the result by the desired
   standard deviation, then add the desired mean.) */
double 
rng_get_double_normal (struct rng *rng)
{
  /* Knuth, _The Art of Computer Programming_, Vol. 2, 3.4.1C,
     Algorithm P. */
  double this_normal;
  
  if (rng->next_normal != NOT_DOUBLE)
    {
      this_normal = rng->next_normal;
      rng->next_normal = NOT_DOUBLE;
    }
  else 
    {
      double v1, v2, s;
      
      do
        {
          double u1 = rng_get_double (rng);
          double u2 = rng_get_double (rng);
          v1 = 2.0 * u1 - 1.0;
          v2 = 2.0 * u2 - 1.0;
          s = v1 * v1 + v2 * v2;
        }
      while (s >= 1);

      this_normal = v1 * sqrt (-2. * log (s) / s);
      rng->next_normal = v2 * sqrt (-2. * log (s) / s); 
    }
  
  return this_normal;
}

/* Gets an initialized RNG for use in PSPP transformations and
   procedures. */
struct rng *
pspp_rng (void)
{
  static struct rng *rng;

  if (rng == NULL)
    rng = rng_create ();
  return rng;
}
