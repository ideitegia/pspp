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

#if !SYNTAX_FILE
#define SYNTAX_FILE 1

#include <stdbool.h>
#include <libpspp/getl.h>

struct string;

bool read_syntax_file (struct getl_interface *s,
                       struct string *line, enum getl_syntax *syntax);

/* Creates a syntax file source with file name FN. */
struct getl_interface * create_syntax_file_source (const char *fn) ;

#endif
