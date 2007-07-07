/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2005 Free Software Foundation, Inc.

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

#ifndef RANDOM_H
#define RANDOM_H 1

#include <gsl/gsl_rng.h>

void random_init (void);
void random_done (void);

gsl_rng *get_rng (void);
void set_rng (unsigned long seed);

#endif /* random.h */
