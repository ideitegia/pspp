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

#ifndef SFM_READ_H
#define SFM_READ_H 1

#include <stdbool.h>
#include <stdio.h>

#include <data/case.h>
#include <libpspp/float-format.h>
#include <libpspp/integer-format.h>

/* Reading system files. */

/* System file info that doesn't fit in struct dictionary. */
struct sfm_read_info
  {
    char creation_date[10];	/* `dd mmm yy' plus a null. */
    char creation_time[9];	/* `hh:mm:ss' plus a null. */
    enum integer_format integer_format;
    enum float_format float_format;
    bool compressed;		/* 0=no, 1=yes. */
    casenumber case_cnt;        /* -1 if unknown. */
    char product[61];		/* Product name plus a null. */

    /* Writer's version number in X.Y.Z format.
       The version number is not always present; if not, then
       all of these are set to 0. */
    int version_major;          /* X. */
    int version_minor;          /* Y. */
    int version_revision;       /* Z. */
  };

struct dictionary;
struct file_handle;
struct ccase;
struct casereader *sfm_open_reader (struct file_handle *,
                                    struct dictionary **,
                                    struct sfm_read_info *);
bool sfm_detect (FILE *);

#endif /* sys-file-reader.h */
