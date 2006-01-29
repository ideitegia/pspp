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
#include "scratch-reader.h"
#include <stdlib.h>
#include "casefile.h"
#include "dictionary.h"
#include "error.h"
#include "file-handle-def.h"
#include "scratch-handle.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* A reader for a scratch file. */
struct scratch_reader 
  {
    struct file_handle *fh;             /* Underlying file handle. */
    struct casereader *casereader;      /* Case reader. */
  };

/* Opens FH, which must have referent type FH_REF_SCRATCH, and
   returns a scratch_reader for it, or a null pointer on
   failure.  Stores the dictionary for the scratch file into
   *DICT.

   If you use an any_reader instead, then your code can be more
   flexible without being any harder to write. */
struct scratch_reader *
scratch_reader_open (struct file_handle *fh, struct dictionary **dict)
{
  struct scratch_handle *sh;
  struct scratch_reader *reader;
  
  if (!fh_open (fh, FH_REF_SCRATCH, "scratch file", "rs"))
    return NULL;
  
  sh = fh_get_scratch_handle (fh);
  if (sh == NULL) 
    {
      msg (SE, _("Scratch file handle %s has not yet been written, "
                 "using SAVE or another procedure, so it cannot yet "
                 "be used for reading."),
           fh_get_name (fh));
      return NULL;
    }

  *dict = dict_clone (sh->dictionary);
  reader = xmalloc (sizeof *reader);
  reader->fh = fh;
  reader->casereader = casefile_get_reader (sh->casefile);
  return reader;
}

/* Reads a case from READER into C.
   Returns true if successful, false on error or at end of file. */
bool
scratch_reader_read_case (struct scratch_reader *reader, struct ccase *c)
{
  return casereader_read (reader->casereader, c);
}

/* Closes READER. */
void
scratch_reader_close (struct scratch_reader *reader) 
{
  fh_close (reader->fh, "scratch file", "rs");
  casereader_destroy (reader->casereader);
  free (reader);
}
