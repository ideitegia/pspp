/* PSPP - a program for statistical analysis.
   Copyright (C) 2006 Free Software Foundation, Inc.

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

#include "scratch-reader.h"

#include <stdlib.h>

#include "dictionary.h"
#include "file-handle-def.h"
#include "scratch-handle.h"
#include <data/case.h>
#include <data/casereader.h>
#include <libpspp/message.h>

#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Opens FH, which must have referent type FH_REF_SCRATCH, and
   returns a scratch_reader for it, or a null pointer on
   failure.  Stores the dictionary for the scratch file into
   *DICT. */
struct casereader *
scratch_reader_open (struct file_handle *fh, struct dictionary **dict)
{
  struct scratch_handle *sh;

  if (!fh_open (fh, FH_REF_SCRATCH, "scratch file", "rs"))
    return NULL;

  sh = fh_get_scratch_handle (fh);
  if (sh == NULL || sh->casereader == NULL)
    {
      msg (SE, _("Scratch file handle %s has not yet been written, "
                 "using SAVE or another procedure, so it cannot yet "
                 "be used for reading."),
           fh_get_name (fh));
      return NULL;
    }

  *dict = dict_clone (sh->dictionary);
  return casereader_clone (sh->casereader);
}
