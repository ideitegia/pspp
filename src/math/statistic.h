/* PSPP - a program for statistical analysis.
   Copyright (C) 2008 Free Software Foundation, Inc.

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

#ifndef __STATISTIC_H__
#define __STATISTIC_H__

#include <stddef.h>

struct ccase ;

struct statistic
{
  void (*accumulate) (struct statistic *, const struct ccase *cx, double c, double cc, double y);
  void (*destroy) (struct statistic *);
};

static inline void statistic_destroy (struct statistic *s);


static inline void
statistic_destroy (struct statistic *s)
{
  if (s) s->destroy (s);
}


#endif
