/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2005, 2006 Free Software Foundation, Inc.

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

#ifndef FILE_HANDLE_DEF_H
#define FILE_HANDLE_DEF_H

#include <stdbool.h>
#include <stddef.h>

/* What a file handle refers to.
   (Ordinarily only a single value is allowed, but fh_open()
   and fh_parse() take a mask.) */
enum fh_referent
  {
    FH_REF_FILE = 001,          /* Ordinary file (the most common case). */
    FH_REF_INLINE = 002,        /* The inline file. */
    FH_REF_SCRATCH = 004        /* Temporary dataset. */
  };

/* File modes. */
enum fh_mode
  {
    FH_MODE_TEXT,               /* New-line delimited lines. */
    FH_MODE_BINARY              /* Fixed-length records. */
  };

/* Ways to access a file. */
enum fh_access
  {
    FH_ACC_READ,                /* Read from it. */
    FH_ACC_WRITE                /* Write to it. */
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
struct file_handle *fh_create_file (const char *handle_name,
                                    const char *file_name,
                                    const struct fh_properties *);
struct file_handle *fh_create_scratch (const char *handle_name);
const struct fh_properties *fh_default_properties (void);

/* Reference management. */
struct file_handle *fh_ref (struct file_handle *);
void fh_unref (struct file_handle *);
void fh_unname (struct file_handle *);

/* Finding file handles. */
struct file_handle *fh_from_id (const char *handle_name);
struct file_handle *fh_from_file_name (const char *file_name);
struct file_handle *fh_inline_file (void);

/* Generic properties of file handles. */
const char *fh_get_id (const struct file_handle *);
const char *fh_get_name (const struct file_handle *);
enum fh_referent fh_get_referent (const struct file_handle *);

/* Properties of FH_REF_FILE file handles. */
const char *fh_get_file_name (const struct file_handle *);
enum fh_mode fh_get_mode (const struct file_handle *) ;

/* Properties of FH_REF_FILE and FH_REF_INLINE file handles. */
size_t fh_get_record_width (const struct file_handle *);
size_t fh_get_tab_width (const struct file_handle *);

/* Properties of FH_REF_SCRATCH file handles. */
struct scratch_handle *fh_get_scratch_handle (const struct file_handle *);
void fh_set_scratch_handle (struct file_handle *, struct scratch_handle *);

/* Mutual exclusion for access . */
struct fh_lock *fh_lock (struct file_handle *, enum fh_referent mask,
                         const char *type, enum fh_access, bool exclusive);
bool fh_unlock (struct fh_lock *);
void *fh_lock_get_aux (const struct fh_lock *);
void fh_lock_set_aux (struct fh_lock *, void *aux);
bool fh_is_locked (const struct file_handle *, enum fh_access);

/* Default file handle for DATA LIST, REREAD, REPEATING DATA
   commands. */
struct file_handle *fh_get_default_handle (void);
void fh_set_default_handle (struct file_handle *);

#endif
