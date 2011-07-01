/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2011 Free Software Foundation, Inc.

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

#if !levene_h
#define levene_h 1

struct nl;

union value;

struct levene *levene_create (int indep_width, const union value *cutpoint);

void levene_pass_one (struct levene *, double value, double weight, const union value *gv);
void levene_pass_two (struct levene *, double value, double weight, const union value *gv);
void levene_pass_three (struct levene *, double value, double weight, const union value *gv);

double levene_calculate (struct levene*);

void levene_destroy (struct levene*);

#endif
