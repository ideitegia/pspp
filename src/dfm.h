/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#if !dfm_h
#define dfm_h 1

/* Data file manager (dfm).

   This module is in charge of reading and writing data files (other
   than system files).  dfm is an fhuser, so see file-handle.h for the
   fhuser interface. */

#include <stddef.h>

/* I/O utilities. */
struct file_handle;
int dfm_open_for_reading (struct file_handle *handle);
int dfm_open_for_writing (struct file_handle *handle);
char *dfm_get_record (struct file_handle *handle, int *len);
int dfm_put_record (struct file_handle *handle, const char *rec, size_t len);

/* Motion control. */
void dfm_fwd_record (struct file_handle *handle);
void dfm_bkwd_record (struct file_handle *handle, int column);

/* Weirdness. */
void dfm_set_record (struct file_handle *handle, char *new_line);
int dfm_get_cur_col (struct file_handle *handle);
void dfm_push (struct file_handle *handle);
void dfm_pop (struct file_handle *handle);

#endif /* dfm_h */
