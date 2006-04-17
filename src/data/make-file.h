/* PSPP - computes sample statistics.
   Copyright (C) 2004 Free Software Foundation, Inc.
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

#ifndef MKFILE_H
#define MKFILE_H


/* Creates a temporary file and stores its name in *FILE_NAME and
   a file descriptor for it in *FD.  Returns success.  Caller is
   responsible for freeing *FILE_NAME. */
int make_temp_file (int *fd, char **file_name); 


/* Creates a temporary file and stores its name in *FILE_NAME and
   a file stream for it in *FP.  Returns success.  Caller is
   responsible for freeing *FILE_NAME. */
int make_unique_file_stream (FILE **fp, char **file_name) ;

#endif /* make-file.h */
