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
#include "any-writer.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <libpspp/assertion.h>
#include <libpspp/message.h>
#include "file-handle-def.h"
#include "file-name.h"
#include "por-file-writer.h"
#include "sys-file-writer.h"
#include <libpspp/str.h>
#include "scratch-writer.h"
#include "xalloc.h"

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

    case FH_REF_SCRATCH:
      return scratch_writer_open (handle, dict);
    }

  NOT_REACHED ();
}
