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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#if !filename_h
#define filename_h 1

#include <stdio.h>

/* Search path for configuration files. */
extern const char *config_path;

void fn_init (void);

char *fn_interp_vars (const char *input, const char *(*getenv) (const char *));
char *fn_tilde_expand (const char *fn);
char *fn_search_path (const char *basename, const char *path,
		      const char *prepend);
char *fn_prepend_dir (const char *filename, const char *directory);
char *fn_normalize (const char *fn);
char *fn_dirname (const char *fn);
char *fn_basename (const char *fn);

char *fn_get_cwd (void);

int fn_absolute_p (const char *fn);
int fn_special_p (const char *fn);
int fn_exists_p (const char *fn);
char *fn_readlink (const char *fn);

const char *fn_getenv (const char *variable);
const char *fn_getenv_default (const char *variable, const char *def);

FILE *fn_open (const char *fn, const char *mode);
int fn_close (const char *fn, FILE *file);

struct file_identity *fn_get_identity (const char *filename);
void fn_free_identity (struct file_identity *);
int fn_compare_file_identities (const struct file_identity *,
                                const struct file_identity *);

/* Extended file routines. */
struct file_ext;

typedef int (*file_callback) (struct file_ext *);

/* File callbacks may not return zero to indicate failure unless they
   set errno to a sensible value. */
struct file_ext
  {
    char *filename;		/* Filename. */
    const char *mode;		/* Open mode, i.e, "wb". */
    FILE *file;			/* File. */
    int *sequence_no;		/* Page number, etc. */
    void *param;		/* User data. */
    file_callback postopen;	/* Called after FILE opened. */
    file_callback preclose;	/* Called before FILE closed. */
  };

int fn_open_ext (struct file_ext *file);
int fn_close_ext (struct file_ext *file);

#endif /* filename_h */
