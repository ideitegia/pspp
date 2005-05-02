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

#ifndef SFM_WRITE_H
#define SFM_WRITE_H 1

/* Writing system files. */

struct file_handle;
struct dictionary;
struct ccase;
struct sfm_writer *sfm_open_writer (struct file_handle *, struct dictionary *, 
				    int compress, short omit_longnames);

int sfm_write_case (struct sfm_writer *, struct ccase *);
void sfm_close_writer (struct sfm_writer *);

#endif /* sfm-write.h */
