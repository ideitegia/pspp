/* PSPP - draws pie charts of sample statistics

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

#ifndef PIECHART_H
#define PIECHART_H

struct slice {
  const char *label;
  double magnetude;
};

/* Draw a piechart */
void piechart_plot(const char *title,
		   const struct slice *slices, int n_slices);

#endif

