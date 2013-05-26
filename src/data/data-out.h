/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2011, 2013 Free Software Foundation, Inc.

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
#include "libpspp/float-format.h"
#include "libpspp/integer-format.h"

struct fmt_spec;
struct string;
union value;

char * data_out (const union value *, const char *encoding, const struct fmt_spec *);

char * data_out_pool (const union value *, const char *encoding, const struct fmt_spec *, struct pool *pool);

char *data_out_stretchy (const union value *, const char *encoding, const struct fmt_spec *, struct pool *);

void data_out_recode (const union value *input, const char *input_encoding,
                      const struct fmt_spec *,
                      struct string *output, const char *output_encoding);

#endif /* data-out.h */
