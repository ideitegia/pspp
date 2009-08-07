/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009 Free Software Foundation, Inc.

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

/* A interface to allow a temporary file to be treated as an
   array of data. */

#include <config.h>

#include <libpspp/tmpfile.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <libpspp/assertion.h>
#include <libpspp/cast.h>

#include "error.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct tmpfile
  {
    FILE *file;                 /* Underlying file. */

    /* Current byte offset in file.  We track this manually,
       instead of using ftello, because in glibc ftello flushes
       the stream buffer, making the common case of sequential
       access to cases unreasonably slow. */
    off_t position;
  };

/* Creates and returns a new temporary file.  The temporary file
   will be automatically deleted when the process exits. */
struct tmpfile *
tmpfile_create (void)
{
  struct tmpfile *tf = xmalloc (sizeof *tf);
  tf->file = tmpfile ();
  if (tf->file == NULL)
    error (0, errno, _("failed to create temporary file"));
  tf->position = 0;
  return tf;
}

/* Closes and destroys temporary file TF.  Returns true if I/O on
   TF always succeeded, false if an I/O error occurred at some
   point. */
bool
tmpfile_destroy (struct tmpfile *tf)
{
  bool ok = true;
  if (tf != NULL)
    {
      ok = !tmpfile_error (tf);
      if (tf->file != NULL)
        fclose (tf->file);
      free (tf);
    }
  return ok;
}

/* Seeks TF's underlying file to the start of `union value'
   VALUE_IDX within case CASE_IDX.
   Returns true if the seek is successful and TF is not
   otherwise tainted, false otherwise. */
static bool
do_seek (const struct tmpfile *tf_, off_t offset)
{
  struct tmpfile *tf = CONST_CAST (struct tmpfile *, tf_);

  if (!tmpfile_error (tf))
    {
      if (tf->position == offset)
        return true;
      else if (fseeko (tf->file, offset, SEEK_SET) == 0)
        {
          tf->position = offset;
          return true;
        }
      else
        error (0, errno, _("seeking in temporary file"));
    }

  return false;
}

/* Reads BYTES bytes from TF's underlying file into BUFFER.
   TF must not be tainted upon entry into this function.
   Returns true if successful, false upon an I/O error (in which
   case TF is marked tainted). */
static bool
do_read (const struct tmpfile *tf_, void *buffer, size_t bytes)
{
  struct tmpfile *tf = CONST_CAST (struct tmpfile *, tf_);

  assert (!tmpfile_error (tf));
  if (bytes > 0 && fread (buffer, bytes, 1, tf->file) != 1)
    {
      if (ferror (tf->file))
        error (0, errno, _("reading temporary file"));
      else if (feof (tf->file))
        error (0, 0, _("unexpected end of file reading temporary file"));
      else
        NOT_REACHED ();
      return false;
    }
  tf->position += bytes;
  return true;
}

/* Writes BYTES bytes from BUFFER into TF's underlying file.
   TF must not be tainted upon entry into this function.
   Returns true if successful, false upon an I/O error (in which
   case TF is marked tainted). */
static bool
do_write (struct tmpfile *tf, const void *buffer, size_t bytes)
{
  assert (!tmpfile_error (tf));
  if (bytes > 0 && fwrite (buffer, bytes, 1, tf->file) != 1)
    {
      error (0, errno, _("writing to temporary file"));
      return false;
    }
  tf->position += bytes;
  return true;
}

/* Reads N bytes from TF at byte offset OFFSET into DATA.
   Returns true if successful, false on failure.  */
bool
tmpfile_read (const struct tmpfile *tf, off_t offset, size_t n, void *data)
{
  return do_seek (tf, offset) && do_read (tf, data, n);
}

/* Writes the N bytes in DATA to TF at byte offset OFFSET.
   Returns true if successful, false on failure.  */
bool
tmpfile_write (struct tmpfile *tf, off_t offset, size_t n, const void *data)
{
  return do_seek (tf, offset) && do_write (tf, data, n);
}

/* Returns true if an error has occurred in I/O on TF,
   false if no error has been detected. */
bool
tmpfile_error (const struct tmpfile *tf)
{
  return tf->file == NULL || ferror (tf->file) || feof (tf->file);
}
