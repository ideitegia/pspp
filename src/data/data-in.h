/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2010, 2011 Free Software Foundation, Inc.

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

#ifndef DATA_DATA_IN_H
#define DATA_DATA_IN_H 1

#include <stdbool.h>
#include "data/format.h"
#include "libpspp/str.h"

union value;
struct dictionary;

char *data_in (struct substring input, const char *input_encoding,
               enum fmt_type, 
               union value *output, int width, const char *output_encoding);

bool data_in_msg (struct substring input, const char *input_encoding,
                  enum fmt_type,
                  union value *output, int width, const char *output_encoding);

void data_in_imply_decimals (struct substring input, const char *encoding,
                             enum fmt_type format, int d, union value *output);

#endif /* data/data-in.h */
