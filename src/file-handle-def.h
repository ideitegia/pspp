/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000,2005 Free Software Foundation, Inc.
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

#ifndef FILE_HANDLE_DEF_H
#define FILE_HANDLE_DEF_H

#include <config.h>

/* File modes. */
enum fh_mode
  {
    MODE_TEXT,                  /* New-line delimited lines. */
    MODE_BINARY                 /* Fixed-length records. */
  };

/* Properties of a file handle. */
struct fh_properties 
  {
    enum fh_mode mode;          /* File mode. */
    size_t record_width;        /* Length of fixed-format records. */
    size_t tab_width;           /* Tab width, 0=do not expand tabs. */
  };

void fh_init (void);
void fh_done (void);

/* Creating file handles. */
struct file_handle *fh_create (const char *handle_name, 
                               const char *filename,
                               const struct fh_properties *);
/* Destroy file handle */
void fh_free(struct file_handle *);

const struct fh_properties *fh_default_properties (void);

/* Finding file handles, based on handle name or filename. */
struct file_handle *fh_from_name (const char *handle_name);
struct file_handle *fh_from_filename (const char *filename);

/* Querying properties of file handles. */
const char *fh_get_name (const struct file_handle *);
const char *fh_get_filename (const struct file_handle *);
enum fh_mode fh_get_mode (const struct file_handle *) ;
size_t fh_get_record_width (const struct file_handle *);
size_t fh_get_tab_width (const struct file_handle *);

/* Opening and closing file handles. */
void **fh_open (struct file_handle *, const char *type, const char *mode);
int fh_close (struct file_handle *, const char *type, const char *mode);

#endif
