/* PSPP - computes sample statistics.
   Copyright (C) 2004 Free Software Foundation, Inc.

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

#include <config.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "file-name.h"
#include "make-file.h"
#include <libpspp/message.h>
#include <libpspp/alloc.h>

#include "mkstemp.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Non ansi compilers may set this */
#ifndef P_tmpdir
#define P_tmpdir "/tmp"
#endif

/* Creates a temporary file and stores its name in *FILE_NAME and
   a file descriptor for it in *FD.  Returns success.  Caller is
   responsible for freeing *FILE_NAME. */
int
make_temp_file (int *fd, char **file_name)
{
  const char *parent_dir;

  assert (file_name != NULL);
  assert (fd != NULL);

  if (getenv ("TMPDIR") != NULL)
    parent_dir = getenv ("TMPDIR");
  else
    parent_dir = P_tmpdir;

  *file_name = xmalloc (strlen (parent_dir) + 32);
  sprintf (*file_name, "%s/psppXXXXXX", parent_dir);
  *fd = mkstemp (*file_name);
  if (*fd < 0)
    {
      msg (ME, _("%s: Creating temporary file: %s."),
           *file_name, strerror (errno));
      free (*file_name);
      *file_name = NULL;
      return 0;
    }
  return 1;
}


/* Creates a temporary file and stores its name in *FILE_NAME and
   a file stream for it in *FP.  Returns success.  Caller is
   responsible for freeing *FILE_NAME and for closing *FP */
int
make_unique_file_stream (FILE **fp, char **file_name)
{
  static int serial = 0;
  const char *parent_dir;


  /* FIXME: 
     Need to check for pre-existing file name.
     Need also to pass in the directory instead of using /tmp 
  */

  assert (file_name != NULL);
  assert (fp != NULL);

  if (getenv ("TMPDIR") != NULL)
    parent_dir = getenv ("TMPDIR");
  else
    parent_dir = P_tmpdir;

  *file_name = xmalloc (strlen (parent_dir) + 32);


  sprintf (*file_name, "%s/pspp%d.png", parent_dir, serial++);

  *fp = fopen(*file_name, "w");

  if (! *fp )
    {
      msg (ME, _("%s: Creating file: %s."), *file_name, strerror (errno));
      free (*file_name);
      *file_name = NULL;
      return 0;
    }

  return 1;
}




