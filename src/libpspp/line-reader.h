/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011 Free Software Foundation, Inc.

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

#ifndef LIBPSPP_LINE_READER_H
#define LIBPSPP_LINE_READER_H 1

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/* line_reader.

   Reads a text file in an arbitrary encoding one line at a time, with
   optional automatic encoding detection.
*/

#define LINE_READER_BUFFER_SIZE 4096

struct string;

struct line_reader *line_reader_for_fd (const char *encoding, int fd);
struct line_reader *line_reader_for_file (const char *encoding,
                                          const char *filename, int flags);

int line_reader_close (struct line_reader *);
void line_reader_free (struct line_reader *);

bool line_reader_read (struct line_reader *, struct string *,
                       size_t max_length);

int line_reader_fileno (const struct line_reader *);
off_t line_reader_tell (const struct line_reader *);

bool line_reader_eof (const struct line_reader *);
int line_reader_error (const struct line_reader *);

const char *line_reader_get_encoding (const struct line_reader *);

bool line_reader_is_auto (const struct line_reader *);

#endif /* libpspp/line-reader.h */
