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

#if !data_in_h
#define data_in_h 1

#include <stddef.h>
#include <stdbool.h>
#include <libpspp/float-format.h>
#include <libpspp/integer-format.h>
#include <libpspp/str.h>
#include "format.h"

enum integer_format data_in_get_integer_format (void);
void data_in_set_integer_format (enum integer_format);

enum float_format data_in_get_float_format (void);
void data_in_set_float_format (enum float_format);

bool data_in (struct substring input, 
              enum fmt_type, int implied_decimals, int first_column,
              union value *output, int width);

#endif /* data-in.h */
