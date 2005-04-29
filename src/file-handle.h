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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#if !file_handle_h
#define file_handle_h 1

/* File handles. */

#include <stddef.h>

/* File modes. */
enum file_handle_mode
  {
    MODE_TEXT,                  /* New-line delimited lines. */
    MODE_BINARY                 /* Fixed-length records. */
  };



void fh_init(void);
void fh_done(void);

/* Parsing handles. */
struct file_handle *fh_parse (void);


/* Opening and closing handles. */
void **fh_open (struct file_handle *, const char *type, const char *mode);
int fh_close (struct file_handle *, const char *type, const char *mode);

/* Handle info. */
const char *handle_get_name (const struct file_handle *);
const char *handle_get_filename (const struct file_handle *);
enum file_handle_mode handle_get_mode (const struct file_handle *);
size_t handle_get_record_width (const struct file_handle *);
size_t handle_get_tab_width (const struct file_handle *);

#endif /* !file_handle.h */
