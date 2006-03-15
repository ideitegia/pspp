/* PSPP - computes sample statistics.
   Copyright (C) 2004 Free Software Foundation, Inc.
   Written by John Darrington <john@darrington.wattle.id.au>

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

#ifndef BOX_WHISKER_H
#define BOX_WHISKER_H

#include <config.h>

struct chart ;
struct weighted_value;
struct metrics;

/* Draw an outlier on the plot CH
 * at CENTRELINE
 * The outlier is in (*wvp)[idx]
 * If EXTREME is non zero, then consider it to be an extreme
 * value
 */
void  draw_outlier(struct chart *ch, double centreline, 
	     struct weighted_value **wvp, 
	     int idx,
	     short extreme);


void boxplot_draw_boxplot(struct chart *ch,
		     double box_centre, 
		     double box_width,
		     struct metrics *m,
		     const char *name);


void boxplot_draw_yscale(struct chart *ch , double y_max, double y_min);

#endif
