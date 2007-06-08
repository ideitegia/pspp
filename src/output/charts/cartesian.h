/* PSPP - computes sample statistics.
   Copyright (C) 2004 Free Software Foundation, Inc.

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



#ifndef CARTESIAN_H
#define CARTESIAN_H


enum CHART_DIM
  {
    CHART_DIM_X,
    CHART_DIM_Y
  };



/* Plot a data point */
void chart_datum(struct chart *ch, int dataset UNUSED, double x, double y);

/* Draw a line with slope SLOPE and intercept INTERCEPT.
   between the points limit1 and limit2.
   If lim_dim is CHART_DIM_Y then the limit{1,2} are on the
   y axis otherwise the x axis
*/
void chart_line(struct chart *ch, double slope, double intercept,
		double limit1, double limit2, enum CHART_DIM lim_dim);


#endif
