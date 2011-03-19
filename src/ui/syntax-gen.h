/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2008, 2011 Free Software Foundation, Inc.

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

#ifndef SYNTAX_GEN_H
#define SYNTAX_GEN_H 1

/* These functions aid in composing PSPP syntax. */

#include <stdarg.h>
#include <stddef.h>
#include "libpspp/compiler.h"
#include "libpspp/str.h"

struct fmt_spec;
struct substring;
struct string;
union value;

void syntax_gen_string (struct string *output, struct substring in);
void syntax_gen_number (struct string *output,
                        double, const struct fmt_spec *format);
void syntax_gen_value (struct string *output, const union value *value,
                       int width, const struct fmt_spec *format);
void syntax_gen_num_range (struct string *output, double low, double high,
                           const struct fmt_spec *format);

void syntax_gen_pspp_valist (struct string *output, const char *format,
                             va_list)
  PRINTF_FORMAT (2, 0);
void syntax_gen_pspp (struct string *output, const char *format, ...)
  PRINTF_FORMAT (2, 3);

#endif /* format-syntax.h */
