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
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include "magic.h"
#include "random.h"
#include "settings.h"

/* Deal with broken system random number generator. */
#if HAVE_GOOD_RANDOM
#define real_rand rand
#define real_srand srand
#define REAL_RAND_MAX RAND_MAX
#else /* !HAVE_GOOD_RANDOM */
#define REAL_RAND_MAX 32767

/* Some systems are so broken that they do not supply a value for
   RAND_MAX.  There is absolutely no reliable way to determine this
   value, either.  So we must supply our own.  This one is the one
   presented in the ANSI C standard as strictly compliant. */
static unsigned long int next = 1;

int
real_rand (void)
{
  next = next * 1103515245 + 12345;
  return (unsigned int)(next / 65536) % 32768;
}

void
real_srand (unsigned int seed)
{
  next = seed;
}
#endif /* !HAVE_GOOD_RANDOM */

/* The random number generator here is an implementation in C of
   Knuth's Algorithm 3.2.2B (Randomizing by Shuffling) in _The Art of
   Computer Programming_, Vol. 2. */

#define k 13
static int V[k];
static int Y;

static double X2;

/* Initializes the random number generator.  Should be called once by
   every cmd_*() that uses random numbers.  Note that this includes
   all procedures that use expressions since they may generate random
   numbers. */
void
setup_randomize (void)
{
  static time_t curtime;
  int i;

  if (set_seed == NOT_LONG)
    {
      if (!curtime)
	time (&curtime);
      real_srand (curtime++);
    }
  else
    real_srand (set_seed);

  set_seed_used = 1;

  for (i = 0; i < k; i++)
    V[i] = real_rand ();
  Y = real_rand ();
  X2 = NOT_DOUBLE;
}

/* Standard shuffling procedure for increasing randomness of the ANSI
   C random number generator. Returns a random number R where 0 <= R
   <= RAND_MAX. */
inline int
shuffle (void)
{
  int j = k * Y / RAND_MAX;
  Y = V[j];
  V[j] = real_rand ();
  return Y;
}

/* Returns a random number R where 0 <= R <= X. */
double 
rand_uniform (double x)
{
  return ((double) shuffle ()) / (((double) RAND_MAX) / x);
}

/* Returns a random number from the distribution with mean 0 and
   standard deviation X.  This uses algorithm P in section 3.4.1C of
   Knuth's _Art of Computer Programming_, Vol 2. */
double 
rand_normal (double x)
{
  double U1, U2;
  double V1, V2;
  double S;
  double X1;

  if (X2 != NOT_DOUBLE)
    {
      double t = X2;
      X2 = NOT_DOUBLE;
      return t * x;
    }
  do
    {
      U1 = ((double) shuffle ()) / RAND_MAX;
      U2 = ((double) shuffle ()) / RAND_MAX;
      V1 = 2 * U1 - 1;
      V2 = 2 * U2 - 1;
      S = V1 * V1 + V2 * V2;
    }
  while (S >= 1);
  X1 = V1 * sqrt (-2. * log (S) / S);
  X2 = V2 * sqrt (-2. * log (S) / S);
  return X1 * x;
}

/* Returns a random integer R, where 0 <= R < X. */
int
rand_simple (int x)
{
  return shuffle () % x;
}

