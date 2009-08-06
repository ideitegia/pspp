/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2009 Free Software Foundation, Inc.

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

#ifndef __TUKEY_HINGES_H__
#define __TUKEY_HINGES_H__

#include <stddef.h>

#include "order-stats.h"


struct tukey_hinges
{
  struct order_stats parent;
};

struct tukey_hinges * tukey_hinges_create (double W, double c_min);


void tukey_hinges_calculate (const struct tukey_hinges *h, double hinge[3]);



#endif
