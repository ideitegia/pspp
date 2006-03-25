/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.
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
#include "any-writer.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <libpspp/message.h>
#include "file-handle-def.h"
#include "filename.h"
#include "por-file-writer.h"
#include "sys-file-writer.h"
#include <libpspp/str.h>
#include "scratch-writer.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Type of file backing an any_writer. */
enum any_writer_type
  {
    SYSTEM_FILE,                /* System file. */
    PORTABLE_FILE,              /* Portable file. */
    SCRATCH_FILE                /* Scratch file. */
  };

/* Writer for any type of case-structured file. */
struct any_writer 
  {
    enum any_writer_type type;  /* Type of file. */
    void *private;              /* Private data. */
  };

/* Creates and returns a writer for HANDLE with the given DICT. */
struct any_writer *
any_writer_open (struct file_handle *handle, struct dictionary *dict)
{
  switch (fh_get_referent (handle)) 
    {
    case FH_REF_FILE:
      {
        struct any_writer *writer;
        char *extension;

        extension = fn_extension (fh_get_filename (handle));
        str_lowercase (extension);

        if (!strcmp (extension, ".por"))
          writer = any_writer_from_pfm_writer (
            pfm_open_writer (handle, dict, pfm_writer_default_options ()));
        else
          writer = any_writer_from_sfm_writer (
            sfm_open_writer (handle, dict, sfm_writer_default_options ()));
        free (extension);

        return writer;
      }

    case FH_REF_INLINE:
      msg (ME, _("The inline file is not allowed here."));
      return NULL;

    case FH_REF_SCRATCH:
      return any_writer_from_scratch_writer (scratch_writer_open (handle,
                                                                  dict));
    }

  abort ();
}

/* If PRIVATE is non-null, creates and returns a new any_writer,
   initializing its fields to TYPE and PRIVATE.  If PRIVATE is a
   null pointer, just returns a null pointer. */   
static struct any_writer *
make_any_writer (enum any_writer_type type, void *private) 
{
  if (private != NULL) 
    {
      struct any_writer *writer = xmalloc (sizeof *writer);
      writer->type = type;
      writer->private = private;
      return writer; 
    }
  else
    return NULL;
}
  
/* If SFM_WRITER is non-null, encapsulates SFM_WRITER in an
   any_writer and returns it.  If SFM_WRITER is null, just
   returns a null pointer.

   Useful when you need to pass options to sfm_open_writer().
   Typical usage:
        any_writer_from_sfm_writer (sfm_open_writer (fh, dict, opts))
   If you don't need to pass options, then any_writer_open() by
   itself is easier and more straightforward. */
struct any_writer *
any_writer_from_sfm_writer (struct sfm_writer *sfm_writer) 
{
  return make_any_writer (SYSTEM_FILE, sfm_writer);
}

/* If PFM_WRITER is non-null, encapsulates PFM_WRITER in an
   any_writer and returns it.  If PFM_WRITER is null, just
   returns a null pointer.

   Useful when you need to pass options to pfm_open_writer().
   Typical usage:
        any_writer_from_pfm_writer (pfm_open_writer (fh, dict, opts))
   If you don't need to pass options, then any_writer_open() by
   itself is easier and more straightforward. */
struct any_writer *
any_writer_from_pfm_writer (struct pfm_writer *pfm_writer) 
{
  return make_any_writer (PORTABLE_FILE, pfm_writer);
}

/* If SCRATCH_WRITER is non-null, encapsulates SCRATCH_WRITER in
   an any_writer and returns it.  If SCRATCH_WRITER is null, just
   returns a null pointer.

   Not particularly useful.  Included just for consistency. */
struct any_writer *
any_writer_from_scratch_writer (struct scratch_writer *scratch_writer) 
{
  return make_any_writer (SCRATCH_FILE, scratch_writer);
}

/* Writes cases C to WRITER.
   Returns true if successful, false on failure. */
bool
any_writer_write (struct any_writer *writer, const struct ccase *c) 
{
  switch (writer->type) 
    {
    case SYSTEM_FILE:
      return sfm_write_case (writer->private, c);

    case PORTABLE_FILE:
      return pfm_write_case (writer->private, c);

    case SCRATCH_FILE:
      return scratch_writer_write_case (writer->private, c);
    }
  abort ();
}

/* Returns true if an I/O error has occurred on WRITER, false
   otherwise. */
bool
any_writer_error (const struct any_writer *writer) 
{
  switch (writer->type) 
    {
    case SYSTEM_FILE:
      return sfm_write_error (writer->private);

    case PORTABLE_FILE:
      return pfm_write_error (writer->private);

    case SCRATCH_FILE:
      return scratch_writer_error (writer->private);
    }
  abort ();
}

/* Closes WRITER.
   Returns true if successful, false if an I/O error occurred. */
bool
any_writer_close (struct any_writer *writer) 
{
  bool ok;
  
  if (writer == NULL)
    return true;

  switch (writer->type) 
    {
    case SYSTEM_FILE:
      ok = sfm_close_writer (writer->private);
      break;

    case PORTABLE_FILE:
      ok = pfm_close_writer (writer->private);
      break;

    case SCRATCH_FILE:
      ok = scratch_writer_close (writer->private);
      break;
      
    default:
      abort ();
    }

  free (writer);
  return ok;
}
