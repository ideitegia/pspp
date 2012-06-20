/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2011, 2012 Free Software Foundation, Inc.

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

#ifndef DFM_WRITE_H
#define DFM_WRITE_H

/* Writing data files. */

#include <stdbool.h>
#include <stddef.h>

struct file_handle;
struct dfm_writer *dfm_open_writer (struct file_handle *,
                                    const char *encoding);
bool dfm_close_writer (struct dfm_writer *);
bool dfm_write_error (const struct dfm_writer *);
bool dfm_put_record (struct dfm_writer *, const char *rec, size_t len);
const char *dfm_writer_get_encoding (const struct dfm_writer *);

#endif /* data-writer.h */
