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
#include "dfm-write.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include "alloc.h"
#include "error.h"
#include "file-handle.h"
#include "filename.h"
#include "str.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Data file writer. */
struct dfm_writer
  {
    struct file_handle *fh;     /* File handle. */
    struct file_ext file;	/* Associated file. */
    char *bounce;               /* Bounce buffer for fixed-size fields. */
  };

/* Opens a file handle for writing as a data file. */
struct dfm_writer *
dfm_open_writer (struct file_handle *fh)
{
  struct dfm_writer *w;
  void **aux;
  
  aux = fh_open (fh, "data file", "ws");
  if (aux == NULL)
    return NULL;
  if (*aux != NULL)
    return *aux;

  w = *aux = xmalloc (sizeof *w);
  w->fh = fh;
  w->file.file = NULL;
  w->bounce = NULL;

  w->file.filename = xstrdup (fh_get_filename (w->fh));
  w->file.mode = "wb";
  w->file.file = NULL;
  w->file.sequence_no = NULL;
  w->file.param = NULL;
  w->file.postopen = NULL;
  w->file.preclose = NULL;
      
  if (!fn_open_ext (&w->file))
    {
      msg (ME, _("An error occurred while opening \"%s\" for writing "
                 "as a data file: %s."),
           fh_get_filename (w->fh), strerror (errno));
      goto error;
    }

  return w;

 error:
  err_cond_fail ();
  dfm_close_writer (w);
  return NULL;
}

/* Writes record REC having length LEN to the file corresponding to
   HANDLE.  REC is not null-terminated.  Returns nonzero on success,
   zero on failure. */
int
dfm_put_record (struct dfm_writer *w, const char *rec, size_t len)
{
  assert (w != NULL);

  if (fh_get_mode (w->fh) == MODE_BINARY
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

  if (fwrite (rec, len, 1, w->file.file) != 1)
    {
      msg (ME, _("Error writing file %s: %s."),
           fh_get_name (w->fh), strerror (errno));
      err_cond_fail ();
      return 0;
    }

  return 1;
}

/* Closes data file writer W. */
void
dfm_close_writer (struct dfm_writer *w)
{
  if (fh_close (w->fh, "data file", "ws"))
    return;
  
  if (w->file.file)
    {
      fn_close_ext (&w->file);
      free (w->file.filename);
      w->file.filename = NULL;
    }
  free (w->bounce);
  free (w);
}
