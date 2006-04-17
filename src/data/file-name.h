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

#ifndef FILE_NAME_H
#define FILE_NAME_H 1

#include <stdio.h>

/* Search path for configuration files. */
extern const char *config_path;

void fn_init (void);

struct string;
void fn_interp_vars (struct string *target, 
                     const char *(*getenv) (const char *));
char *fn_tilde_expand (const char *fn);
char *fn_search_path (const char *base_name, const char *path,
		      const char *prefix);
char *fn_normalize (const char *fn);
char *fn_dir_name (const char *fn);
char *fn_extension (const char *fn);

char *fn_get_cwd (void);

int fn_is_absolute (const char *fn);
int fn_is_special (const char *fn);
int fn_exists (const char *fn);
char *fn_readlink (const char *fn);

const char *fn_getenv (const char *variable);
const char *fn_getenv_default (const char *variable, const char *def);

FILE *fn_open (const char *fn, const char *mode);
int fn_close (const char *fn, FILE *file);

struct file_identity *fn_get_identity (const char *file_name);
void fn_free_identity (struct file_identity *);
int fn_compare_file_identities (const struct file_identity *,
                                const struct file_identity *);

#endif /* file-name.h */
