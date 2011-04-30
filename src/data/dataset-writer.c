/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2009-2011 Free Software Foundation, Inc.

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

#include "data/dataset-writer.h"

#include <stdlib.h>

#include "data/case.h"
#include "data/case-map.h"
#include "data/casereader.h"
#include "data/casewriter-provider.h"
#include "data/casewriter.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/file-handle-def.h"
#include "data/variable.h"
#include "libpspp/compiler.h"
#include "libpspp/taint.h"

#include "gl/xalloc.h"

#define N_(msgid) (msgid)

/* A dataset file writer. */
struct dataset_writer
  {
    struct dataset *ds;                 /* Underlying dataset. */
    struct fh_lock *lock;               /* Exclusive access to file handle. */
    struct dictionary *dict;            /* Dictionary for subwriter. */
    struct case_map *compactor;         /* Compacts into dictionary. */
    struct casewriter *subwriter;       /* Data output. */
  };

static const struct casewriter_class dataset_writer_casewriter_class;

/* Opens FH, which must have referent type FH_REF_DATASET, and
   returns a dataset_writer for it, or a null pointer on
   failure.  Cases stored in the dataset_writer will be expected
   to be drawn from DICTIONARY. */
struct casewriter *
dataset_writer_open (struct file_handle *fh,
                     const struct dictionary *dictionary)
{
  struct dataset_writer *writer;
  struct casewriter *casewriter;
  struct fh_lock *lock;

  /* Get exclusive write access to handle. */
  /* TRANSLATORS: this fragment will be interpolated into
     messages in fh_lock() that identify types of files. */
  lock = fh_lock (fh, FH_REF_DATASET, N_("dataset"), FH_ACC_WRITE, true);
  if (lock == NULL)
    return NULL;

  /* Create writer. */
  writer = xmalloc (sizeof *writer);
  writer->lock = lock;
  writer->ds = fh_get_dataset (fh);

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
  writer->subwriter = autopaging_writer_create (dict_get_proto (writer->dict));

  casewriter = casewriter_create (dict_get_proto (writer->dict),
                                  &dataset_writer_casewriter_class, writer);
  taint_propagate (casewriter_get_taint (writer->subwriter),
                   casewriter_get_taint (casewriter));
  return casewriter;
}

/* Writes case C to WRITER. */
static void
dataset_writer_casewriter_write (struct casewriter *w UNUSED, void *writer_,
                                 struct ccase *c)
{
  struct dataset_writer *writer = writer_;
  casewriter_write (writer->subwriter,
                    case_map_execute (writer->compactor, c));
}

/* Closes WRITER. */
static void
dataset_writer_casewriter_destroy (struct casewriter *w UNUSED, void *writer_)
{
  struct dataset_writer *writer = writer_;
  struct casereader *reader = casewriter_make_reader (writer->subwriter);
  if (!casereader_error (reader))
    {
      dataset_set_dict (writer->ds, writer->dict);
      dataset_set_source (writer->ds, reader);
    }
  else
    {
      casereader_destroy (reader);
      dict_destroy (writer->dict);
    }

  fh_unlock (writer->lock);
  free (writer);
}

static const struct casewriter_class dataset_writer_casewriter_class =
  {
    dataset_writer_casewriter_write,
    dataset_writer_casewriter_destroy,
    NULL,
  };
