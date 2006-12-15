/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

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

#ifndef DATA_OUT_H
#define DATA_OUT_H 1

#include <stdbool.h>
#include <libpspp/integer-format.h>

struct fmt_spec;
union value;

void data_out (const union value *, const struct fmt_spec *, char *);

enum integer_format data_out_get_integer_format (void);
void data_out_set_integer_format (enum integer_format);

enum float_format data_out_get_float_format (void);
void data_out_set_float_format (enum float_format);

#endif /* data-out.h */
