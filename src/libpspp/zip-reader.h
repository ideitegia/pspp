/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2013 Free Software Foundation, Inc.

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


#ifndef ZIP_READER_H
#define ZIP_READER_H 1

#include <inttypes.h>

struct zip_reader;
struct string;

enum compression
  {
    COMPRESSION_STORED = 0,
    COMPRESSION_INFLATE,
    n_COMPRESSION
  };

struct zip_member
{
  FILE *fp;                   /* The stream from which the data is read */
  uint32_t offset;            /* Starting offset in file. */
  uint32_t comp_size;         /* Length of member file data, in bytes. */
  uint32_t ucomp_size;        /* Uncompressed length of member file data, in bytes. */
  uint32_t expected_crc;      /* CRC-32 of member file data.. */
  char *name;                 /* Name of member file. */
  uint32_t crc;
  enum compression compression;

  size_t bytes_unread;       /* Number of bytes left in the member available for reading */
  int ref_cnt;
  struct string *errs;
  void *aux;
};

struct decompressor
{
  bool (*init) (struct zip_member *);
  int  (*read) (struct zip_member *, void *, size_t);
  void (*finish) (struct zip_member *);
};


void zm_dump (const struct zip_member *zm);

/* Create zip reader to read the file called FILENAME.
   If ERRS is non-null if will be used to contain any error messages
   which the reader wishes to report.
 */
struct zip_reader *zip_reader_create (const char *filename, struct string *errs);

/* Destroy the zip reader */
void zip_reader_destroy (struct zip_reader *zr);

/* Return the zip member in the reader ZR, called MEMBER */
struct zip_member *zip_member_open (struct zip_reader *zr, const char *member);

/* Read upto N bytes from ZM, storing them in BUF.
   Returns the number of bytes read, or -1 on error */
int zip_member_read (struct zip_member *zm, void *buf, size_t n);

/* Unref (and possibly destroy) the zip member ZM */
void zip_member_unref (struct zip_member *zm);

/* Ref the zip member */
void zip_member_ref (struct zip_member *zm);

void zip_member_finish (struct zip_member *zm);


#endif
