/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.

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

#ifndef ANY_WRITER_H
#define ANY_WRITER_H 1

#include <stdbool.h>

struct file_handle;
struct dictionary;
struct ccase;
struct sfm_writer;
struct pfm_writer;
struct scratch_writer;

struct any_writer *any_writer_open (struct file_handle *, struct dictionary *);
struct any_writer *any_writer_from_sfm_writer (struct sfm_writer *);
struct any_writer *any_writer_from_pfm_writer (struct pfm_writer *);
struct any_writer *any_writer_from_scratch_writer (struct scratch_writer *);

bool any_writer_write (struct any_writer *, const struct ccase *);
bool any_writer_error (const struct any_writer *);
bool any_writer_close (struct any_writer *);

#endif /* any-writer.h */
