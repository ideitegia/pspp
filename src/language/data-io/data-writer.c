/* PSPP - computes sample statistics.
   Copyright (C) 1997-2004, 2006 Free Software Foundation, Inc.
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

#include <data/file-name.h>
#include <language/data-io/file-handle.h>
#include <libpspp/alloc.h>
#include <libpspp/assertion.h>
#include <libpspp/message.h>
#include <libpspp/str.h>

#include "minmax.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Data file writer. */
struct dfm_writer
  {
    struct file_handle *fh;     /* File handle. */
    FILE *file;                 /* Associated file. */
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

/* Writes record REC (which need not be null-terminated) having
   length LEN to the file corresponding to HANDLE.  Adds any
   needed formatting, such as a trailing new-line.  Returns true
   on success, false on failure. */
bool
dfm_put_record (struct dfm_writer *w, const char *rec, size_t len)
{
  assert (w != NULL);

  if (dfm_write_error (w))
    return false;

  switch (fh_get_mode (w->fh)) 
    {
    case FH_MODE_TEXT:
      fwrite (rec, len, 1, w->file);
      putc ('\n', w->file);
      break;

    case FH_MODE_BINARY:
      {
        size_t record_width = fh_get_record_width (w->fh);
        size_t write_bytes = MIN (len, record_width);
        size_t pad_bytes = record_width - write_bytes;
        fwrite (rec, write_bytes, 1, w->file);
        while (pad_bytes > 0) 
          {
            static const char spaces[32] = "                                ";
            size_t chunk = MIN (pad_bytes, sizeof spaces);
            fwrite (spaces, chunk, 1, w->file);
            pad_bytes -= chunk;
          }
      }
      break;

    default:
      NOT_REACHED ();
    }

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
  free (w);
  free (file_name);

  return ok;
}
