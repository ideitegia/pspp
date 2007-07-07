/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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

#ifndef DFM_READ_H
#define DFM_READ_H

/* Data file manager (dfm).

   This module is in charge of reading and writing data files (other
   than system files).  dfm is an fhuser, so see file-handle.h for the
   fhuser interface. */

#include <stdbool.h>
#include <stddef.h>

struct file_handle;
struct string;
struct lexer;

/* Input. */
struct dfm_reader *dfm_open_reader (struct file_handle *, struct lexer *);
void dfm_close_reader (struct dfm_reader *);
bool dfm_reader_error (const struct dfm_reader *);
unsigned dfm_eof (struct dfm_reader *);
struct substring dfm_get_record (struct dfm_reader *);
void dfm_expand_tabs (struct dfm_reader *);

/* Line control. */
void dfm_forward_record (struct dfm_reader *);
void dfm_reread_record (struct dfm_reader *, size_t column);
void dfm_forward_columns (struct dfm_reader *, size_t columns);
size_t dfm_column_start (const struct dfm_reader *);
size_t dfm_columns_past_end (const struct dfm_reader *);
size_t dfm_get_column (const struct dfm_reader *, const char *);

/* File stack. */
void dfm_push (struct dfm_reader *);
void dfm_pop (struct dfm_reader *);

#endif /* data-reader.h */
