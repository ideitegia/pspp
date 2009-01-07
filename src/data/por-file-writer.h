/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009 Free Software Foundation, Inc.

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

#ifndef PFM_WRITE_H
#define PFM_WRITE_H

#include <stdbool.h>

/* Portable file writing. */

/* Portable file types. */
enum pfm_type
  {
    PFM_COMM,   /* Formatted for communication. */
    PFM_TAPE    /* Formatted for tape. */
  };

/* Portable file writing options. */
struct pfm_write_options
  {
    bool create_writeable;      /* File perms: writeable or read/only? */
    enum pfm_type type;         /* Type of portable file (TODO). */
    int digits;                 /* Digits of precision. */
  };

struct file_handle;
struct dictionary;
struct casewriter *pfm_open_writer (struct file_handle *, struct dictionary *,
                                    struct pfm_write_options);
struct pfm_write_options pfm_writer_default_options (void);

#endif /* por-file-writer.h */
