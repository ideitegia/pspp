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

#ifndef PFM_WRITE_H
#define PFM_WRITE_H

/* Portable file writing. */

struct file_handle;
struct dictionary;
struct ccase;
struct pfm_writer *pfm_open_writer (struct file_handle *, struct dictionary *);
int pfm_write_case (struct pfm_writer *, struct ccase *);
void pfm_close_writer (struct pfm_writer *);

#endif /* pfm-write.h */
