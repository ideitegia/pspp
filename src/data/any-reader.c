/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2010, 2011, 2012, 2014 Free Software Foundation, Inc.

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

#include "data/any-reader.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "data/dataset-reader.h"
#include "data/file-handle-def.h"
#include "data/file-name.h"
#include "data/por-file-reader.h"
#include "data/sys-file-reader.h"
#include "libpspp/assertion.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Tries to detect whether FILE is a given type of file, by opening the file
   and passing it to DETECT, and returns a detect_result. */
static enum detect_result
try_detect (const char *file_name, bool (*detect) (FILE *))
{
  FILE *file;
  bool is_type;

  file = fn_open (file_name, "rb");
  if (file == NULL)
    {
      msg (ME, _("An error occurred while opening `%s': %s."),
           file_name, strerror (errno));
      return ANY_ERROR;
    }

  is_type = detect (file);

  fn_close (file_name, file);

  return is_type ? ANY_YES : ANY_NO;
}

/* Returns true if any_reader_open() would be able to open FILE as a data
   file, false otherwise. */
enum detect_result
any_reader_may_open (const char *file)
{
  enum detect_result res = try_detect (file, sfm_detect);
  
  if (res == ANY_NO)
    res = try_detect (file, pfm_detect);

  return res;
}

/* Returns a casereader for HANDLE.  On success, returns the new
   casereader and stores the file's dictionary into *DICT.  On
   failure, returns a null pointer.

   Ordinarily the reader attempts to automatically detect the character
   encoding based on the file's contents.  This isn't always possible,
   especially for files written by old versions of SPSS or PSPP, so specifying
   a nonnull ENCODING overrides the choice of character encoding.  */
struct casereader *
any_reader_open (struct file_handle *handle, const char *encoding,
                 struct dictionary **dict)
{
  switch (fh_get_referent (handle))
    {
    case FH_REF_FILE:
      {
        enum detect_result result;

        result = try_detect (fh_get_file_name (handle), sfm_detect);
        if (result == ANY_ERROR)
          return NULL;
        else if (result == ANY_YES)
          {
            struct sfm_reader *r;

            r = sfm_open (handle);
            if (r == NULL)
              return NULL;

            return sfm_decode (r, encoding, dict, NULL);
          }

        result = try_detect (fh_get_file_name (handle), pfm_detect);
        if (result == ANY_ERROR)
          return NULL;
        else if (result == ANY_YES)
          return pfm_open_reader (handle, dict, NULL);

        msg (SE, _("`%s' is not a system or portable file."),
             fh_get_file_name (handle));
        return NULL;
      }

    case FH_REF_INLINE:
      msg (SE, _("The inline file is not allowed here."));
      return NULL;

    case FH_REF_DATASET:
      return dataset_reader_open (handle, dict);
    }
  NOT_REACHED ();
}
