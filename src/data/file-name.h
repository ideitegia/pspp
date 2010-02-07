/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010 Free Software Foundation, Inc.

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

#ifndef FILE_NAME_H
#define FILE_NAME_H 1

#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>

struct string_set;

char *fn_search_path (const char *base_name, char **path);
char *fn_dir_name (const char *fn);
char *fn_extension (const char *fn);

bool fn_is_absolute (const char *fn);
bool fn_is_special (const char *fn);
bool fn_exists (const char *fn);

const char *fn_getenv (const char *variable);
const char *fn_getenv_default (const char *variable, const char *def);

FILE *fn_open (const char *fn, const char *mode);
int fn_close (const char *fn, FILE *file);

FILE *create_stream (const char *fn, const char *mode, mode_t permissions);

struct file_identity *fn_get_identity (const char *file_name);
void fn_free_identity (struct file_identity *);
int fn_compare_file_identities (const struct file_identity *,
                                const struct file_identity *);
unsigned int fn_hash_identity (const struct file_identity *);

const char * default_output_path (void);

#endif /* file-name.h */
