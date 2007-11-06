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

#include "scratch-writer.h"

#include <stdlib.h>

#include <data/case.h>
#include <data/case-map.h>
#include <data/casereader.h>
#include <data/casewriter-provider.h>
#include <data/casewriter.h>
#include <data/dictionary.h>
#include <data/file-handle-def.h>
#include <data/scratch-handle.h>
#include <data/variable.h>
#include <libpspp/compiler.h>
#include <libpspp/taint.h>

#include "xalloc.h"

#define N_(msgid) (msgid)

/* A scratch file writer. */
struct scratch_writer
  {
    struct file_handle *fh;             /* Underlying file handle. */
    struct fh_lock *lock;               /* Exclusive access to file handle. */
    struct dictionary *dict;            /* Dictionary for subwriter. */
    struct case_map *compactor;         /* Compacts into dictionary. */
    struct casewriter *subwriter;       /* Data output. */
  };

static struct casewriter_class scratch_writer_casewriter_class;

/* Opens FH, which must have referent type FH_REF_SCRATCH, and
   returns a scratch_writer for it, or a null pointer on
   failure.  Cases stored in the scratch_writer will be expected
   to be drawn from DICTIONARY. */
struct casewriter *
scratch_writer_open (struct file_handle *fh,
                     const struct dictionary *dictionary)
{
  struct scratch_writer *writer;
  struct casewriter *casewriter;
  struct fh_lock *lock;
  size_t dict_value_cnt;

  /* Get exclusive write access to handle. */
  /* TRANSLATORS: this fragment will be interpolated into
     messages in fh_lock() that identify types of files. */
  lock = fh_lock (fh, FH_REF_SCRATCH, N_("scratch file"), FH_ACC_WRITE, true);
  if (lock == NULL)
    return NULL;

  /* Create writer. */
  writer = xmalloc (sizeof *writer);
  writer->lock = lock;
  writer->fh = fh_ref (fh);

  writer->dict = dict_clone (dictionary);
  dict_delete_scratch_vars (writer->dict);
  if (dict_count_values (writer->dict, 0)
      < dict_get_next_value_idx (writer->dict))
    {
      writer->compactor = case_map_to_compact_dict (writer->dict, 0);
      dict_compact_values (writer->dict);
    }
  else
    writer->compactor = NULL;
  dict_value_cnt = dict_get_next_value_idx (writer->dict);
  writer->subwriter = autopaging_writer_create (dict_value_cnt);

  casewriter = casewriter_create (dict_value_cnt,
                                  &scratch_writer_casewriter_class, writer);
  taint_propagate (casewriter_get_taint (writer->subwriter),
                   casewriter_get_taint (casewriter));
  return casewriter;
}

/* Writes case C to WRITER. */
static void
scratch_writer_casewriter_write (struct casewriter *w UNUSED, void *writer_,
                                 struct ccase *c)
{
  struct scratch_writer *writer = writer_;
  struct ccase tmp;
  if (writer->compactor)
    {
      case_map_execute (writer->compactor, c, &tmp);
      case_destroy (c);
    }
  else
    case_move (&tmp, c);
  casewriter_write (writer->subwriter, &tmp);
}

/* Closes WRITER. */
static void
scratch_writer_casewriter_destroy (struct casewriter *w UNUSED, void *writer_)
{
  static unsigned int next_unique_id = 0x12345678;

  struct scratch_writer *writer = writer_;
  struct casereader *reader = casewriter_make_reader (writer->subwriter);
  if (!casereader_error (reader))
    {
      /* Destroy previous contents of handle. */
      struct scratch_handle *sh = fh_get_scratch_handle (writer->fh);
      if (sh != NULL)
        scratch_handle_destroy (sh);

      /* Create new contents. */
      sh = xmalloc (sizeof *sh);
      sh->unique_id = ++next_unique_id;
      sh->dictionary = writer->dict;
      sh->casereader = reader;
      fh_set_scratch_handle (writer->fh, sh);
    }
  else
    {
      casereader_destroy (reader);
      dict_destroy (writer->dict);
    }

  fh_unlock (writer->lock);
  fh_unref (writer->fh);
  free (writer);
}

static struct casewriter_class scratch_writer_casewriter_class =
  {
    scratch_writer_casewriter_write,
    scratch_writer_casewriter_destroy,
    NULL,
  };
