/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

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

#ifndef SFM_READ_H
#define SFM_READ_H 1

/* Reading system files. */

/* System file info that doesn't fit in struct dictionary. */
struct sfm_read_info
  {
    char creation_date[10];	/* `dd mmm yy' plus a null. */
    char creation_time[9];	/* `hh:mm:ss' plus a null. */
    int big_endian;		/* 1=big-endian, 0=little-endian. */
    int compressed;		/* 0=no, 1=yes. */
    int case_cnt;               /* -1 if unknown. */
    char product[61];		/* Product name plus a null. */
  };

struct dictionary;
struct file_handle;
struct ccase;
struct sfm_reader *sfm_open_reader (struct file_handle *,
                                    struct dictionary **,
                                    struct sfm_read_info *);
int sfm_read_case (struct sfm_reader *, struct ccase *);
void sfm_close_reader (struct sfm_reader *);

#endif /* sfm-read.h */
