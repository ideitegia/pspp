/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2013, 2014 Free Software Foundation, Inc.

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

#ifndef SFM_WRITE_H
#define SFM_WRITE_H 1

#include <stdbool.h>
#include "any-reader.h"

/* Writing system files. */

/* Options for creating a system file. */
struct sfm_write_options
  {
    enum any_compression compression;
    bool create_writeable;      /* File perms: writeable or read/only? */
    int version;                /* System file version (currently 2 or 3). */
  };

struct file_handle;
struct dictionary;
struct casewriter *sfm_open_writer (struct file_handle *, struct dictionary *,
                                    struct sfm_write_options);
struct sfm_write_options sfm_writer_default_options (void);

#endif /* sys-file-writer.h */
