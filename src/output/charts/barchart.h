/* PSPP - a program for statistical analysis.
   Copyright (C) 2004 Free Software Foundation, Inc.

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

#ifndef BARCHART_H
#define BARCHART_H

#include <output/chart.h>

enum  bar_opts {
  BAR_GROUPED =  0,
  BAR_STACKED,
  BAR_RANGE
};

void draw_barchart(struct chart *ch, const char *title,
	      const char *xlabel, const char *ylabel, enum bar_opts opt);

#endif
