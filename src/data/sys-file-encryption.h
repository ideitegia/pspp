/* PSPP - a program for statistical analysis.
   Copyright (C) 2013 Free Software Foundation, Inc.

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

#ifndef SYS_FILE_ENCRYPTION_H
#define SYS_FILE_ENCRYPTION_H 1

#include <stdbool.h>
#include <stdio.h>

/* Reading encrypted system files. */

struct encrypted_sys_file;

int encrypted_sys_file_open (struct encrypted_sys_file **,
                             const char *filename);
bool encrypted_sys_file_unlock (struct encrypted_sys_file *,
                                const char *password);
size_t encrypted_sys_file_read (struct encrypted_sys_file *, void *, size_t);
int encrypted_sys_file_close (struct encrypted_sys_file *);

#endif /* sys-file-encryption.h */
