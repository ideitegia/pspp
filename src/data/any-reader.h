/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2010, 2012, 2014 Free Software Foundation, Inc.

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

#ifndef ANY_READER_H
#define ANY_READER_H 1

#include <stdbool.h>
#include <stdio.h>
#include "data/case.h"
#include "libpspp/float-format.h"
#include "libpspp/integer-format.h"

struct any_read_info;
struct dictionary;
struct file_handle;

struct any_reader
  {
    const struct any_reader_class *klass;
  };

struct any_reader_class
  {
    const char *name;

    int (*detect) (FILE *);

    struct any_reader *(*open) (struct file_handle *);
    bool (*close) (struct any_reader *);
    struct casereader *(*decode) (struct any_reader *, const char *encoding,
                                  struct dictionary **,
                                  struct any_read_info *);
    size_t (*get_strings) (const struct any_reader *, struct pool *pool,
                           char ***labels, bool **ids, char ***values);
  };

extern const struct any_reader_class sys_file_reader_class;
extern const struct any_reader_class por_file_reader_class;
extern const struct any_reader_class pcp_file_reader_class;

enum any_type
  {
    ANY_SYS,                    /* SPSS System File. */
    ANY_PCP,                    /* SPSS/PC+ System File. */
    ANY_POR,                    /* SPSS Portable File. */
  };

enum any_compression
  {
    ANY_COMP_NONE,              /* No compression. */
    ANY_COMP_SIMPLE,            /* Bytecode compression of integer values. */
    ANY_COMP_ZLIB               /* ZLIB "deflate" compression. */
  };

/* Data file info that doesn't fit in struct dictionary.

   The strings in this structure are encoded in UTF-8.  (They are normally in
   the ASCII subset of UTF-8.) */
struct any_read_info
  {
    const struct any_reader_class *klass;
    char *creation_date;
    char *creation_time;
    enum integer_format integer_format;
    enum float_format float_format;
    enum any_compression compression;
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

void any_read_info_destroy (struct any_read_info *);

struct file_handle;
struct dictionary;

int any_reader_detect (const char *file_name,
                       const struct any_reader_class **);

struct any_reader *any_reader_open (struct file_handle *);
bool any_reader_close (struct any_reader *);
struct casereader *any_reader_decode (struct any_reader *,
                                      const char *encoding,
                                      struct dictionary **,
                                      struct any_read_info *);
size_t any_reader_get_strings (const struct any_reader *, struct pool *pool,
                               char ***labels, bool **ids, char ***values);

struct casereader *any_reader_open_and_decode (struct file_handle *,
                                               const char *encoding,
                                               struct dictionary **,
                                               struct any_read_info *);

#endif /* any-reader.h */
