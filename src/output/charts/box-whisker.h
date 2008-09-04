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

#ifndef BOX_WHISKER_H
#define BOX_WHISKER_H

struct chart ;
struct box_whisker;

void boxplot_draw_boxplot (struct chart *ch,
			   double box_centre,
			   double box_width,
			   const struct box_whisker *w,
			   const char *name);


void boxplot_draw_yscale (struct chart *ch , double y_max, double y_min);

#endif
