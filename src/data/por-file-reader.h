/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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

#ifndef PFM_READ_H
#define PFM_READ_H

/* Portable file reading. */

#include <stdbool.h>
#include <stdio.h>

/* Information produced by pfm_read_dictionary() that doesn't fit into
   a dictionary struct. */
struct pfm_read_info
  {
    char creation_date[11];	/* `dd mm yyyy' plus a null. */
    char creation_time[9];	/* `hh:mm:ss' plus a null. */
    char product[61];		/* Product name plus a null. */
    char subproduct[61];	/* Subproduct name plus a null. */
  };

struct dictionary;
struct file_handle;
struct ccase;
struct pfm_reader *pfm_open_reader (struct file_handle *,
                                    struct dictionary **,
                                    struct pfm_read_info *);
bool pfm_read_case (struct pfm_reader *, struct ccase *);
bool pfm_read_error (const struct pfm_reader *);
void pfm_close_reader (struct pfm_reader *);
bool pfm_detect (FILE *);

#endif /* por-file-reader.h */
