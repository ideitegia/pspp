/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.

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
#include <libpspp/assertion.h>
#include <libpspp/message.h>
#include "file-handle-def.h"
#include "file-name.h"
#include "por-file-reader.h"
#include "sys-file-reader.h"
#include <libpspp/str.h>
#include "scratch-reader.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

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

  file = fn_open (fh_get_file_name (handle), "rb");
  if (file == NULL)
    {
      msg (ME, _("An error occurred while opening \"%s\": %s."),
           fh_get_file_name (handle), strerror (errno));
      return IO_ERROR;
    }
    
  is_type = detect (file);
  
  fn_close (fh_get_file_name (handle), file);

  return is_type ? YES : NO;
}

/* Returns a casereader for HANDLE.  On success, returns the new
   casereader and stores the file's dictionary into *DICT.  On
   failure, returns a null pointer. */
struct casereader *
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
          return sfm_open_reader (handle, dict, NULL);

        result = try_detect (handle, pfm_detect);
        if (result == IO_ERROR)
          return NULL;
        else if (result == YES)
          return pfm_open_reader (handle, dict, NULL);

        msg (SE, _("\"%s\" is not a system or portable file."),
             fh_get_file_name (handle));
        return NULL;
      }

    case FH_REF_INLINE:
      msg (SE, _("The inline file is not allowed here."));
      return NULL;

    case FH_REF_SCRATCH:
      return scratch_reader_open (handle, dict);
    }
  NOT_REACHED ();
}
