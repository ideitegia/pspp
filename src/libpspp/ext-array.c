/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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

/* An interface to an array of octets that is stored on disk as a temporary
   file. */

#include <config.h>

#include "libpspp/ext-array.h"
#include "libpspp/message.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/temp-file.h"

#include "gl/unlocked-io.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

enum op
  {
    OP_WRITE, /* writing */
    OP_READ   /* reading */
  };

struct ext_array
  {
    FILE *file;                 /* Underlying file. */

    /* Current byte offset in file.  We track this manually,
       instead of using ftello, because in glibc ftello flushes
       the stream buffer, making the common case of sequential
       access to cases unreasonably slow. */
    off_t position;

    /* The most recent operation performed */
    enum op op;
  };

/* Creates and returns a new external array. */
struct ext_array *
ext_array_create (void)
{
  struct ext_array *ea = xmalloc (sizeof *ea);
  ea->file = create_temp_file ();
  if (ea->file == NULL)
    msg_error (errno, _("failed to create temporary file"));
  ea->position = 0;
  ea->op = OP_WRITE;
  return ea;
}

/* Closes and destroys external array EA.  Returns true if I/O on EA always
   succeeded, false if an I/O error occurred at some point. */
bool
ext_array_destroy (struct ext_array *ea)
{
  bool ok = true;
  if (ea != NULL)
    {
      ok = !ext_array_error (ea);
      if (ea->file != NULL)
        close_temp_file (ea->file);
      free (ea);
    }
  return ok;
}

/* Seeks EA's underlying file to the start of `union value'
   VALUE_IDX within case CASE_IDX.
   Returns true if the seek is successful and EA is not
   otherwise tainted, false otherwise. */
static bool
do_seek (const struct ext_array *ea_, off_t offset, enum op op)
{
  struct ext_array *ea = CONST_CAST (struct ext_array *, ea_);
  if (!ext_array_error (ea))
    {
      if (ea->position == offset && ea->op == op)
        return true;
      else if (fseeko (ea->file, offset, SEEK_SET) == 0)
        {
          ea->position = offset;
          return true;
        }
      else
        msg_error (errno, _("seeking in temporary file"));
    }

  return false;
}

/* Reads BYTES bytes from EA's underlying file into BUFFER.
   EA must not be tainted upon entry into this function.
   Returns true if successful, false upon an I/O error (in which
   case EA is marked tainted). */
static bool
do_read (const struct ext_array *ea_, void *buffer, size_t bytes)
{
  struct ext_array *ea = CONST_CAST (struct ext_array *, ea_);

  assert (!ext_array_error (ea));
  if (bytes > 0 && fread (buffer, bytes, 1, ea->file) != 1)
    {
      if (ferror (ea->file))
        msg_error (errno, _("reading temporary file"));
      else if (feof (ea->file))
        msg_error ( 0, _("unexpected end of file reading temporary file"));
      else
        NOT_REACHED ();
      return false;
    }
  ea->position += bytes;
  ea->op = OP_READ;
  return true;
}

/* Writes BYTES bytes from BUFFER into EA's underlying file.
   EA must not be tainted upon entry into this function.
   Returns true if successful, false upon an I/O error (in which
   case EA is marked tainted). */
static bool
do_write (struct ext_array *ea, const void *buffer, size_t bytes)
{
  assert (!ext_array_error (ea));
  if (bytes > 0 && fwrite (buffer, bytes, 1, ea->file) != 1)
    {
      msg_error (errno, _("writing to temporary file"));
      return false;
    }
  ea->position += bytes;
  ea->op = OP_WRITE;
  return true;
}

/* Reads N bytes from EA at byte offset OFFSET into DATA.
   Returns true if successful, false on failure.  */
bool
ext_array_read (const struct ext_array *ea, off_t offset, size_t n, void *data)
{
  return do_seek (ea, offset, OP_READ) && do_read (ea, data, n);
}


/* Writes the N bytes in DATA to EA at byte offset OFFSET.
   Returns true if successful, false on failure.  */
bool
ext_array_write (struct ext_array *ea, off_t offset, size_t n,
                 const void *data)
{
  return do_seek (ea, offset, OP_WRITE) && do_write (ea, data, n);
}

/* Returns true if an error has occurred in I/O on EA,
   false if no error has been detected. */
bool
ext_array_error (const struct ext_array *ea)
{
  return ea->file == NULL || ferror (ea->file) || feof (ea->file);
}
