/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2011, 2012, 2013, 2014 Free Software Foundation, Inc.

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

#include "data/case.h"
#include "data/sys-file.h"
#include "libpspp/float-format.h"
#include "libpspp/integer-format.h"

/* Reading system files. */

/* System file info that doesn't fit in struct dictionary.

   The strings in this structure are encoded in UTF-8.  (They are normally in
   the ASCII subset of UTF-8.) */
struct sfm_read_info
  {
    char *creation_date;	/* "dd mmm yy". */
    char *creation_time;	/* "hh:mm:ss". */
    enum integer_format integer_format;
    enum float_format float_format;
    enum sfm_compression compression;
    casenumber case_cnt;        /* -1 if unknown. */
    char *product;		/* Product name. */
    char *product_ext;          /* Extra product info. */

    /* Writer's version number in X.Y.Z format.
       The version number is not always present; if not, then
       all of these are set to 0. */
    int version_major;          /* X. */
    int version_minor;          /* Y. */
    int version_revision;       /* Z. */
  };

void sfm_read_info_destroy (struct sfm_read_info *);

struct dictionary;
struct file_handle;
struct casereader *sfm_open_reader (struct file_handle *, const char *encoding,
                                    struct dictionary **,
                                    struct sfm_read_info *);
bool sfm_detect (FILE *);

#endif /* sys-file-reader.h */
