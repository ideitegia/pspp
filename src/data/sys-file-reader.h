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

/* Reading system files.

   To read a system file:

      1. Open it with sfm_open().

      2. Figure out what encoding to read it with.  sfm_get_encoding() can
         help.

      3. Obtain a casereader with sfm_decode().

   If, after step 1 or 2, you decide that you don't want the system file
   anymore, you can close it with sfm_close().  Otherwise, don't call
   sfm_close(), because sfm_decode() consumes it. */

struct dictionary;
struct file_handle;
struct sfm_read_info;

/* Opening and closing an sfm_reader. */
struct sfm_reader *sfm_open (struct file_handle *);
bool sfm_close (struct sfm_reader *);

/* Obtaining information about an sfm_reader before . */
const char *sfm_get_encoding (const struct sfm_reader *);
size_t sfm_get_strings (const struct sfm_reader *, struct pool *pool,
                        char ***labels, bool **ids, char ***values);

/* Decoding a system file's dictionary and obtaining a casereader. */
struct casereader *sfm_decode (struct sfm_reader *, const char *encoding,
                               struct dictionary **, struct sfm_read_info *);

/* Detecting whether a file is a system file. */
bool sfm_detect (FILE *);

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

#endif /* sys-file-reader.h */
