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

#include <config.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <float.h>
#include <assert.h>
#include <math.h>


#include "chart-geometry.h"
#include "chart.h"
#include "str.h"
#include "alloc.h"
#include "manager.h"
#include "output.h"


#ifndef PLOT_CHART_H
#define PLOT_CHART_H

							     
extern const char *data_colour[];

enum tick_orientation {
  TICK_ABSCISSA=0,
  TICK_ORDINATE
};


/* Draw a tick mark at position
   If label is non zero, then print it at the tick mark
*/
void draw_tick(struct chart *chart, 
	  enum tick_orientation orientation, 
	  double position, 
	       const char *label, ...);


/* Write the title on a chart*/
void   chart_write_title(struct chart *chart, const char *title, ...);


/* Set the scale for the abscissa */
void  chart_write_xscale(struct chart *ch, double min, double max, int ticks);


/* Set the scale for the ordinate */
void  chart_write_yscale(struct chart *ch, double smin, double smax, int ticks);


#endif
