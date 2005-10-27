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
enum file_handle_mode
  {
    MODE_TEXT,                  /* New-line delimited lines. */
    MODE_BINARY                 /* Fixed-length records. */
  };

struct file_handle *create_file_handle_with_defaults (const char *handle_name, 
						      const char *filename);

struct file_handle *create_file_handle (const char *handle_name, 
					const char *filename,
					enum file_handle_mode mode,
					size_t length,
					size_t tab_width
					);



struct file_handle *
get_handle_with_name (const char *handle_name) ;

struct file_handle *
get_handle_for_filename (const char *filename);

const char *handle_get_name (const struct file_handle *handle);

/* Returns the name of the file associated with HANDLE. */
const char *handle_get_filename (const struct file_handle *handle) ;



/* Returns the mode of HANDLE. */
enum file_handle_mode handle_get_mode (const struct file_handle *handle) ;

/* Returns the width of a logical record on HANDLE. */
size_t handle_get_record_width (const struct file_handle *handle);


/* Returns the number of characters per tab stop for HANDLE, or
   zero if tabs are not to be expanded.  Applicable only to
   MODE_TEXT files. */
size_t handle_get_tab_width (const struct file_handle *handle);



void destroy_file_handle(void *fh_, void *aux UNUSED);


/* Tries to open handle H with the given TYPE and MODE.

   TYPE is the sort of file, e.g. "system file".  Only one given
   type of access is allowed on a given file handle at once.

   MODE combines the read or write mode with the sharing mode.
   The first character is 'r' for read, 'w' for write.  The
   second character is 's' to permit sharing, 'e' to require
   exclusive access.

   Returns the address of a void * that the caller can use for
   data specific to the file handle if successful, or a null
   pointer on failure.  For exclusive access modes the void *
   will always be a null pointer at return.  In shared access
   modes the void * will necessarily be null only if no other
   sharers are active.

   If successful, a reference to type is retained, so it should
   probably be a string literal. */

void ** fh_open (struct file_handle *h, const char *type, const char *mode) ;


#endif
