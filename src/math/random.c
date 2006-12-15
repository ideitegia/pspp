/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2005 Free Software Foundation, Inc.

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
#include "random.h"
#include <time.h>
#include "xalloc.h"

static gsl_rng *rng;

void
random_init (void) 
{
}

void
random_done (void) 
{
  if (rng != NULL) 
    gsl_rng_free (rng);
}

/* Returns the current random number generator. */
gsl_rng *
get_rng (void)
{
  if (rng == NULL)
    set_rng (time (0));
  return rng;
}

/* Initializes or reinitializes the random number generator with
   the given SEED. */
void
set_rng (unsigned long seed) 
{
  rng = gsl_rng_alloc (gsl_rng_mt19937);
  if (rng == NULL)
    xalloc_die ();
  gsl_rng_set (rng, seed);
}
