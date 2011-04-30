/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2010, 2011 Free Software Foundation, Inc.

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

#include "data/any-writer.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "data/dataset-writer.h"
#include "data/file-handle-def.h"
#include "data/file-name.h"
#include "data/por-file-writer.h"
#include "data/sys-file-writer.h"
#include "libpspp/assertion.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Creates and returns a writer for HANDLE with the given DICT. */
struct casewriter *
any_writer_open (struct file_handle *handle, struct dictionary *dict)
{
  switch (fh_get_referent (handle))
    {
    case FH_REF_FILE:
      {
        struct casewriter *writer;
        char *extension;

        extension = fn_extension (fh_get_file_name (handle));
        str_lowercase (extension);

        if (!strcmp (extension, ".por"))
          writer = pfm_open_writer (handle, dict,
                                    pfm_writer_default_options ());
        else
          writer = sfm_open_writer (handle, dict,
                                    sfm_writer_default_options ());
        free (extension);

        return writer;
      }

    case FH_REF_INLINE:
      msg (ME, _("The inline file is not allowed here."));
      return NULL;

    case FH_REF_DATASET:
      return dataset_writer_open (handle, dict);
    }

  NOT_REACHED ();
}
