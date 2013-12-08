/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2010, 2012 Free Software Foundation, Inc.

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

#ifndef ANY_READER_H
#define ANY_READER_H 1

#include <stdbool.h>

/* Result of type detection. */
enum detect_result
  {
    ANY_YES,                        /* It is this type. */
    ANY_NO,                         /* It is not this type. */
    ANY_ERROR                    /* File couldn't be opened. */
  };


struct file_handle;
struct dictionary;
enum detect_result any_reader_may_open (const char *file_name);
struct casereader *any_reader_open (struct file_handle *, const char *encoding,
                                    struct dictionary **);

#endif /* any-reader.h */
