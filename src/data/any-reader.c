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
#include "any-reader.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <libpspp/message.h>
#include "file-handle-def.h"
#include "filename.h"
#include "por-file-reader.h"
#include "sys-file-reader.h"
#include <libpspp/str.h>
#include "scratch-reader.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Type of file backing an any_reader. */
enum any_reader_type
  {
    SYSTEM_FILE,                /* System file. */
    PORTABLE_FILE,              /* Portable file. */
    SCRATCH_FILE                /* Scratch file. */
  };

/* Reader for any type of case-structured file. */
struct any_reader 
  {
    enum any_reader_type type;  /* Type of file. */
    void *private;              /* Private data. */
  };

/* Result of type detection. */
enum detect_result 
  {
    YES,                        /* It is this type. */
    NO,                         /* It is not this type. */
    IO_ERROR                    /* File couldn't be opened. */
  };

/* Tries to detect whether HANDLE represents a given type of
   file, by opening the file and passing it to DETECT, and
   returns a detect_result. */
static enum detect_result
try_detect (struct file_handle *handle, bool (*detect) (FILE *))
{
  FILE *file;
  bool is_type;

  file = fn_open (fh_get_filename (handle), "rb");
  if (file == NULL)
    {
      msg (ME, _("An error occurred while opening \"%s\": %s."),
           fh_get_filename (handle), strerror (errno));
      return IO_ERROR;
    }
    
  is_type = detect (file);
  
  fn_close (fh_get_filename (handle), file);

  return is_type ? YES : NO;
}

/* If PRIVATE is non-null, creates and returns a new any_reader,
   initializing its fields to TYPE and PRIVATE.  If PRIVATE is a
   null pointer, just returns a null pointer. */   
static struct any_reader *
make_any_reader (enum any_reader_type type, void *private) 
{
  if (private != NULL) 
    {
      struct any_reader *reader = xmalloc (sizeof *reader);
      reader->type = type;
      reader->private = private;
      return reader;
    }
  else
    return NULL;
}

/* Creates an any_reader for HANDLE.  On success, returns the new
   any_reader and stores the file's dictionary into *DICT.  On
   failure, returns a null pointer. */
struct any_reader *
any_reader_open (struct file_handle *handle, struct dictionary **dict)
{
  switch (fh_get_referent (handle)) 
    {
    case FH_REF_FILE:
      {
        enum detect_result result;

        result = try_detect (handle, sfm_detect);
        if (result == IO_ERROR)
          return NULL;
        else if (result == YES)
          return make_any_reader (SYSTEM_FILE,
                                  sfm_open_reader (handle, dict, NULL));

        result = try_detect (handle, pfm_detect);
        if (result == IO_ERROR)
          return NULL;
        else if (result == YES)
          return make_any_reader (PORTABLE_FILE,
                                  pfm_open_reader (handle, dict, NULL));

        msg (SE, _("\"%s\" is not a system or portable file."),
             fh_get_filename (handle));
        return NULL;
      }

    case FH_REF_INLINE:
      msg (SE, _("The inline file is not allowed here."));
      return NULL;

    case FH_REF_SCRATCH:
      return make_any_reader (SCRATCH_FILE,
                              scratch_reader_open (handle, dict));
    }
  abort ();
}

/* Reads a single case from READER into C.
   Returns true if successful, false at end of file or on error. */
bool
any_reader_read (struct any_reader *reader, struct ccase *c) 
{
  switch (reader->type) 
    {
    case SYSTEM_FILE:
      return sfm_read_case (reader->private, c);

    case PORTABLE_FILE:
      return pfm_read_case (reader->private, c);

    case SCRATCH_FILE:
      return scratch_reader_read_case (reader->private, c);
    }
  abort ();
}

/* Returns true if an I/O error has occurred on READER, false
   otherwise. */
bool
any_reader_error (struct any_reader *reader) 
{
  switch (reader->type) 
    {
    case SYSTEM_FILE:
      return sfm_read_error (reader->private);

    case PORTABLE_FILE:
      return pfm_read_error (reader->private);

    case SCRATCH_FILE:
      return scratch_reader_error (reader->private);
    }
  abort ();
}

/* Closes READER. */
void
any_reader_close (struct any_reader *reader) 
{
  if (reader == NULL)
    return;

  switch (reader->type) 
    {
    case SYSTEM_FILE:
      sfm_close_reader (reader->private);
      break;

    case PORTABLE_FILE:
      pfm_close_reader (reader->private);
      break;

    case SCRATCH_FILE:
      scratch_reader_close (reader->private);
      break;

    default:
      abort ();
    }
}
