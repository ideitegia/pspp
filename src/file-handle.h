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

#if !file_handle_h
#define file_handle_h 1

/* File handle provider (fhp).

   This module provides file handles in the form of file_handle
   structures to the dfm and sfm modules, which are known as file
   handle users (fhusers).  fhp does not know anything about file
   contents. */

#include <stddef.h>
#include "error.h"

struct file_handle;

/* Services that fhusers provide to fhp. */
struct fh_ext_class
  {
    int magic;			/* Magic identifier for fhuser. */
    const char *name;		/* String identifier for fhuser. */

    void (*close) (struct file_handle *); /* Closes the file. */
  };

/* Mostly-opaque structure. */
struct file_handle
  {
    struct private_file_handle *private;
    const struct fh_ext_class *class;	/* Polymorphism support. */
    void *ext;			/* Extension struct for fhuser use. */
  };

/* File modes. */
enum file_handle_mode
  {
    MODE_TEXT,                  /* New-line delimited lines. */
    MODE_BINARY                 /* Fixed-length records. */
  };

/* Pointer to the file handle that corresponds to data in the command
   file entered via BEGIN DATA/END DATA. */
extern struct file_handle *inline_file;

/* Initialization. */
void fh_init_files (void);

/* Opening and closing handles. */
struct file_handle *fh_parse_file_handle (void);
void fh_close_handle (struct file_handle *handle);

/* Handle info. */
const char *handle_get_name (const struct file_handle *handle);
const char *handle_get_filename (const struct file_handle *handle);
enum file_handle_mode handle_get_mode (const struct file_handle *);
size_t handle_get_record_width (const struct file_handle *);

#endif /* !file_handle.h */
