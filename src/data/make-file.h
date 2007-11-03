/* PSPP - a program for statistical analysis.
   Copyright (C) 2004 Free Software Foundation, Inc.

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


/* Prepares to atomically replace a (potentially) existing file
   by a new file, by creating a temporary file with the given
   PERMISSIONS bits in the same directory as *FILE_NAME.

   Special files are an exception: they are not atomically
   replaced but simply opened for writing.

   If successful, stores the temporary file's name in *TMP_NAME
   and a stream for it opened according to MODE (which should be
   "w" or "wb") in *FP.  Returns a ticket that can be used to
   commit or abort the file replacement.  If neither action has
   yet been taken, program termination via signal will cause
   *TMP_FILE to be unlinked.

   The caller is responsible for closing *FP, but *TMP_NAME is
   owned by the callee. */
struct replace_file *replace_file_start (const char *file_name,
                                         const char *mode, mode_t permissions,
                                         FILE **fp, char **tmp_name);

/* Commits or aborts the replacement of a (potentially) existing
   file by a new file, using the ticket returned by
   replace_file_start.  Returns success. */
bool replace_file_commit (struct replace_file *);
bool replace_file_abort (struct replace_file *);

#endif /* make-file.h */
