/* PSPP - computes sample statistics.
   Copyright (C) 1997-2004 Free Software Foundation, Inc.
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

#include <config.h>
#include <language/data-io/data-writer.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <libpspp/alloc.h>
#include <libpspp/message.h>
#include <language/data-io/file-handle.h>
#include <data/file-name.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Data file writer. */
struct dfm_writer
  {
    struct file_handle *fh;     /* File handle. */
    FILE *file;                 /* Associated file. */
    char *bounce;               /* Bounce buffer for fixed-size fields. */
  };

/* Opens a file handle for writing as a data file. */
struct dfm_writer *
dfm_open_writer (struct file_handle *fh)
{
  struct dfm_writer *w;
  void **aux;
  
  aux = fh_open (fh, FH_REF_FILE, "data file", "ws");
  if (aux == NULL)
    return NULL;
  if (*aux != NULL)
    return *aux;

  w = *aux = xmalloc (sizeof *w);
  w->fh = fh;
  w->file = fn_open (fh_get_file_name (w->fh), "wb");
  w->bounce = NULL;

  if (w->file == NULL)
    {
      msg (ME, _("An error occurred while opening \"%s\" for writing "
                 "as a data file: %s."),
           fh_get_file_name (w->fh), strerror (errno));
      goto error;
    }

  return w;

 error:
  dfm_close_writer (w);
  return NULL;
}

/* Returns false if an I/O error occurred on WRITER, true otherwise. */
bool
dfm_write_error (const struct dfm_writer *writer) 
{
  return ferror (writer->file);
}

/* Writes record REC having length LEN to the file corresponding to
   HANDLE.  REC is not null-terminated.  Returns true on success,
   false on failure. */
bool
dfm_put_record (struct dfm_writer *w, const char *rec, size_t len)
{
  assert (w != NULL);

  if (dfm_write_error (w))
    return false;
  
  if (fh_get_mode (w->fh) == FH_MODE_BINARY
      && len < fh_get_record_width (w->fh))
    {
      size_t rec_width = fh_get_record_width (w->fh);
      if (w->bounce == NULL)
        w->bounce = xmalloc (rec_width);
      memcpy (w->bounce, rec, len);
      memset (&w->bounce[len], 0, rec_width - len);
      rec = w->bounce;
      len = rec_width;
    }

  fwrite (rec, len, 1, w->file);
  return !dfm_write_error (w);
}

/* Closes data file writer W. */
bool
dfm_close_writer (struct dfm_writer *w)
{
  char *file_name;
  bool ok;

  if (w == NULL)
    return true;
  file_name = xstrdup (fh_get_name (w->fh));
  if (fh_close (w->fh, "data file", "ws"))
    {
      free (file_name);
      return true; 
    }

  ok = true;
  if (w->file != NULL)
    {
      ok = !dfm_write_error (w) && !fn_close (file_name, w->file);

      if (!ok)
        msg (ME, _("I/O error occurred writing data file \"%s\"."), file_name);
    }
  free (w->bounce);
  free (w);
  free (file_name);

  return ok;
}
