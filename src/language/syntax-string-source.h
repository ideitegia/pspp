/* PSPPIRE - a graphical interface for PSPP.
   Copyright (C) 2007, 2010 Free Software Foundation, Inc.

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

#ifndef SYNTAX_STRING_SOURCE_H
#define SYNTAX_STRING_SOURCE_H

#include "libpspp/compiler.h"

struct getl_interface;

struct syntax_string_source;

struct getl_interface *create_syntax_string_source (const char *);
struct getl_interface *create_syntax_format_source (const char *, ...)
  PRINTF_FORMAT (1, 2);

const char * syntax_string_source_get_syntax (const struct syntax_string_source *s);


#endif
