/* PSPP - a program for statistical analysis.
   Copyright (C) 2006-2007, 2009-2013 Free Software Foundation, Inc.

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

/* Infrastructure common to system file reader and writer.

   Old versions of SPSS limited string variables to a width of
   255 bytes.  For backward compatibility with these older
   versions, the system file format represents a string longer
   than 255 bytes, called a "very long string", as a collection
   of strings no longer than 255 bytes each.  The strings
   concatenated to make a very long string are called its
   "segments"; for consistency, variables other than very long
   strings are considered to have a single segment.

   The interfaces in this file primarily provide support for
   dealing with very long strings.  */

#ifndef DATA_SYS_FILE_PRIVATE_H
#define DATA_SYS_FILE_PRIVATE_H 1

#include <stddef.h>

struct dictionary;

/* ASCII magic numbers. */
#define ASCII_MAGIC  "$FL2"     /* For regular files. */
#define ASCII_ZMAGIC "$FL3"     /* For ZLIB compressed files. */

/* EBCDIC magic number, the same as ASCII_MAGIC but encoded in EBCDIC.

   No EBCDIC ZLIB compressed files have been observed, so we do not define
   EBCDIC_ZMAGIC even though the value is obvious. */
#define EBCDIC_MAGIC "\x5b\xc6\xd3\xf2"

/* Amount of data that ZLIB compressed data blocks typically decompress to. */
#define ZBLOCK_SIZE 0x3ff000

/* A variable in a system file. */
struct sfm_var
  {
    int var_width;              /* Variable width (0 to 32767). */
    int segment_width;          /* Segment width (0 to 255). */
    int case_index;             /* Index into case. */

    /* The following members are interesting only for string
       variables (width != 0).  For numeric variables (width ==
       0) their values are always 0.

       Note: width + padding is always a multiple of 8. */
    int offset;                 /* Offset within string variable in case. */
    int padding;                /* Number of padding bytes following data. */
  };

int sfm_dictionary_to_sfm_vars (const struct dictionary *,
                                struct sfm_var **, size_t *);

int sfm_width_to_octs (int width);
int sfm_width_to_segments (int width);

int sfm_segment_effective_offset (int width, int segment);
int sfm_segment_alloc_width (int width, int segment);

/* A mapping between an encoding name and a Windows codepage. */
struct sys_encoding
  {
    int number;
    const char *name;
  };

extern struct sys_encoding sys_codepage_number_to_name[];
extern struct sys_encoding sys_codepage_name_to_number[];

int sys_get_codepage_from_encoding (const char *);
const char *sys_get_encoding_from_codepage (int);

#endif /* data/sys-file-private.h */
