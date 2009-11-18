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

#include <config.h>
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

#include <data/file-name.h>
#include <data/make-file.h>
#include <libpspp/ll.h>
#include <libpspp/message.h>

#include "fatal-signal.h"
#include "tempname.h"
#include "xalloc.h"

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

struct replace_file
  {
    struct ll ll;
    char *file_name;
    char *tmp_name;
  };

static struct ll_list all_files = LL_INITIALIZER (all_files);

static void free_replace_file (struct replace_file *);
static void unlink_replace_files (void);

struct replace_file *
replace_file_start (const char *file_name, const char *mode,
                    mode_t permissions, FILE **fp, char **tmp_name)
{
  static bool registered;
  struct stat s;
  struct replace_file *rf;
  int fd;

  /* If FILE_NAME represents a special file, write to it directly
     instead of trying to replace it. */
  if (stat (file_name, &s) == 0 && !S_ISREG (s.st_mode))
    {
      /* Open file descriptor. */
      fd = open (file_name, O_WRONLY);
      if (fd < 0)
        {
          msg (ME, _("Opening %s for writing: %s."),
               file_name, strerror (errno));
          return NULL;
        }

      /* Open file as stream. */
      *fp = fdopen (fd, mode);
      if (*fp == NULL)
        {
          msg (ME, _("Opening stream for %s: %s."),
               file_name, strerror (errno));
          close (fd);
          return NULL;
        }

      rf = xmalloc (sizeof *rf);
      rf->file_name = NULL;
      rf->tmp_name = xstrdup (file_name);
      if (tmp_name != NULL)
        *tmp_name = rf->tmp_name;
      return rf;
    }

  if (!registered)
    {
      at_fatal_signal (unlink_replace_files);
      registered = true;
    }
  block_fatal_signals ();

  rf = xmalloc (sizeof *rf);
  rf->file_name = xstrdup (file_name);
  for (;;)
    {
      /* Generate unique temporary file name. */
      rf->tmp_name = xasprintf ("%s.tmpXXXXXX", file_name);
      if (gen_tempname (rf->tmp_name, 0, 0600, GT_NOCREATE) < 0)
        {
          msg (ME, _("Creating temporary file to replace %s: %s."),
               rf->file_name, strerror (errno));
          goto error;
        }

      /* Create file by that name. */
      fd = open (rf->tmp_name, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, permissions);
      if (fd >= 0)
        break;
      if (errno != EEXIST)
        {
          msg (ME, _("Creating temporary file %s: %s."),
               rf->tmp_name, strerror (errno));
          goto error;
        }
      free (rf->tmp_name);
    }


  /* Open file as stream. */
  *fp = fdopen (fd, mode);
  if (*fp == NULL)
    {
      msg (ME, _("Opening stream for temporary file %s: %s."),
           rf->tmp_name, strerror (errno));
      close (fd);
      unlink (rf->tmp_name);
      goto error;
    }

  /* Register file for deletion. */
  ll_push_head (&all_files, &rf->ll);
  unblock_fatal_signals ();

  if (tmp_name != NULL)
    *tmp_name = rf->tmp_name;

  return rf;

error:
  unblock_fatal_signals ();
  free_replace_file (rf);
  *fp = NULL;
  if (tmp_name != NULL)
    *tmp_name = NULL;
  return NULL;
}

bool
replace_file_commit (struct replace_file *rf)
{
  bool ok = true;

  if (rf->file_name != NULL)
    {
      int save_errno;

      block_fatal_signals ();
      ok = rename (rf->tmp_name, rf->file_name) == 0;
      save_errno = errno;
      ll_remove (&rf->ll);
      unblock_fatal_signals ();

      if (!ok)
        msg (ME, _("Replacing %s by %s: %s."),
             rf->tmp_name, rf->file_name, strerror (save_errno));
    }
  else
    {
      /* Special file: no temporary file to rename. */
    }
  free_replace_file (rf);

  return ok;
}

bool
replace_file_abort (struct replace_file *rf)
{
  bool ok = true;

  if (rf->file_name != NULL)
    {
      int save_errno;

      block_fatal_signals ();
      ok = unlink (rf->tmp_name) == 0;
      save_errno = errno;
      ll_remove (&rf->ll);
      unblock_fatal_signals ();

      if (!ok)
        msg (ME, _("Removing %s: %s."), rf->tmp_name, strerror (save_errno));
    }
  else
    {
      /* Special file: no temporary file to unlink. */
    }
  free_replace_file (rf);

  return ok;
}

static void
free_replace_file (struct replace_file *rf)
{
  free (rf->file_name);
  free (rf->tmp_name);
  free (rf);
}

static void
unlink_replace_files (void)
{
  struct replace_file *rf;

  block_fatal_signals ();
  ll_for_each (rf, struct replace_file, ll, &all_files)
    {
      /* We don't free_replace_file(RF) because calling free is unsafe
         from an asynchronous signal handler. */
      unlink (rf->tmp_name);
      fflush (stdout);
    }
  unblock_fatal_signals ();
}
