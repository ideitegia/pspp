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

#ifndef LIBPSPP_U8_ISTREAM_H
#define LIBPSPP_U8_ISTREAM_H 1

#include <sys/types.h>
#include <stdbool.h>

/* u8_istream.

   Reads a text file and reencodes its contents into UTF-8, with optional
   automatic encoding detection.
*/

#define U8_ISTREAM_BUFFER_SIZE 4096

struct u8_istream *u8_istream_for_fd (const char *fromcode, int fd);
struct u8_istream *u8_istream_for_file (const char *fromcode,
                                        const char *filename, int flags);

int u8_istream_close (struct u8_istream *);
void u8_istream_free (struct u8_istream *);

ssize_t u8_istream_read (struct u8_istream *, char *, size_t);

int u8_istream_fileno (const struct u8_istream *);

bool u8_istream_is_auto (const struct u8_istream *);
bool u8_istream_is_utf8 (const struct u8_istream *);

#endif /* libpspp/u8-istream.h */
