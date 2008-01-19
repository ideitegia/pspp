/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

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

#ifndef DATA_OUT_H
#define DATA_OUT_H 1

#include <stdbool.h>
#include <libpspp/float-format.h>
#include <libpspp/integer-format.h>
#include <libpspp/legacy-encoding.h>

struct fmt_spec;
union value;

void data_out (const union value *, const struct fmt_spec *, char *);

void data_out_legacy (const union value *, enum legacy_encoding,
                      const struct fmt_spec *, char *);

#endif /* data-out.h */
