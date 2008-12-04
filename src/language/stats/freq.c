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
#include <data/variable.h>
#include <data/value.h>
#include <libpspp/compiler.h>

#include <stdlib.h>

#include "freq.h"

int
compare_freq ( const void *_f1, const void *_f2, const void *_var)
{
  const struct freq *f1 = _f1;
  const struct freq *f2 = _f2;
  const struct variable *var = _var;

  return  compare_values_short (f1->value, f2->value, var );
}

unsigned int
hash_freq (const void *_f, const void *var)
{
  const struct freq *f = _f;

  return hash_value_short (f->value, var);
}

/* Free function to be used on FR whose value parameter has been copied */
void
free_freq_mutable_hash (void *fr, const void *var UNUSED)
{
  struct freq_mutable *freq = fr;
  free (freq->value);
  free (freq);
}

void
free_freq_hash (void *fr, const void *var UNUSED)
{
  free (fr);
}
