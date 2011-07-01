/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2011 Free Software Foundation, Inc.

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

#include "data/casewriter.h"
#include "data/casewriter-provider.h"

#include <assert.h>
#include <stdlib.h>

#include "data/casereader.h"
#include "data/casereader-provider.h"
#include "data/casewindow.h"
#include "data/settings.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/taint.h"

#include "gl/xalloc.h"

/* A casewriter. */
struct casewriter
  {
    struct taint *taint;
    struct caseproto *proto;
    casenumber case_cnt;
    const struct casewriter_class *class;
    void *aux;
  };

static struct casewriter *create_casewriter_window (const struct caseproto *,
                                                    casenumber max_in_core);

/* Writes case C to WRITER.  Ownership of C is transferred to
   WRITER. */
void
casewriter_write (struct casewriter *writer, struct ccase *c)
{
  size_t n_widths UNUSED = caseproto_get_n_widths (writer->proto);
  assert (case_get_value_cnt (c) >= n_widths);
  expensive_assert (caseproto_equal (case_get_proto (c), 0,
                                     writer->proto, 0, n_widths));
  writer->class->write (writer, writer->aux, c);
}

/* Destroys WRITER.
   Returns true if successful, false if an I/O error was
   encountered on WRITER or on some object on which WRITER has a
   dependency. */
bool
casewriter_destroy (struct casewriter *writer)
{
  bool ok = true;
  if (writer != NULL)
    {
      writer->class->destroy (writer, writer->aux);
      ok = taint_destroy (writer->taint);
      caseproto_unref (writer->proto);
      free (writer);
    }
  return ok;
}

/* Returns the prototype for that cases written to WRITER must
   follow. */
const struct caseproto *
casewriter_get_proto (const struct casewriter *writer)
{
  return writer->proto;
}

/* Destroys WRITER and in its place returns a casereader that can
   be used to read back the data written to WRITER.  WRITER must
   not be used again after calling this function, even as an
   argument to casewriter_destroy.

   Not all casewriters implement this function.  Behavior is
   undefined if it is called on one that does not.

   If an I/O error was encountered on WRITER or on some object on
   which WRITER has a dependency, then the error will be
   propagated to the new casereader. */
struct casereader *
casewriter_make_reader (struct casewriter *writer)
{
  struct casereader *reader = writer->class->convert_to_reader (writer, writer->aux);
  taint_propagate (writer->taint, casereader_get_taint (reader));

  caseproto_unref (writer->proto);
  taint_destroy (writer->taint);
  free (writer);
  return reader;
}

/* Returns a copy of WRITER, which is itself destroyed.
   Useful for taking over ownership of a casewriter, to enforce
   preventing the original owner from accessing the casewriter
   again. */
struct casewriter *
casewriter_rename (struct casewriter *writer)
{
  struct casewriter *new = xmemdup (writer, sizeof *writer);
  free (writer);
  return new;
}

/* Returns true if an I/O error or another hard error has
   occurred on WRITER, a clone of WRITER, or on some object on
   which WRITER's data has a dependency, false otherwise. */
bool
casewriter_error (const struct casewriter *writer)
{
  return taint_is_tainted (writer->taint);
}

/* Marks WRITER as having encountered an error.

   Ordinarily, this function should be called by the
   implementation of a casewriter, not by the casewriter's
   client.  Instead, casewriter clients should usually ensure
   that a casewriter's error state is correct by using
   taint_propagate to propagate to the casewriter's taint
   structure, which may be obtained via casewriter_get_taint. */
void
casewriter_force_error (struct casewriter *writer)
{
  taint_set_taint (writer->taint);
}

/* Returns WRITER's associate taint object, for use with
   taint_propagate and other taint functions. */
const struct taint *
casewriter_get_taint (const struct casewriter *writer)
{
  return writer->taint;
}

/* Creates and returns a new casewriter with the given CLASS and
   auxiliary data AUX.  The casewriter accepts cases that match
   case prototype PROTO, of which the caller retains
   ownership. */
struct casewriter *
casewriter_create (const struct caseproto *proto,
                   const struct casewriter_class *class, void *aux)
{
  struct casewriter *writer = xmalloc (sizeof *writer);
  writer->taint = taint_create ();
  writer->proto = caseproto_ref (proto);
  writer->case_cnt = 0;
  writer->class = class;
  writer->aux = aux;
  return writer;
}

/* Returns a casewriter for cases that match case prototype
   PROTO.  The cases written to the casewriter will be kept in
   memory, unless the amount of memory used grows too large, in
   which case they will be written to disk.

   A casewriter created with this function may be passed to
   casewriter_make_reader.

   This is usually the right kind of casewriter to use. */
struct casewriter *
autopaging_writer_create (const struct caseproto *proto)
{
  return create_casewriter_window (proto,
                                   settings_get_workspace_cases (proto));
}

/* Returns a casewriter for cases that match case prototype
   PROTO.  The cases written to the casewriter will be kept in
   memory.

   A casewriter created with this function may be passed to
   casewriter_make_reader. */
struct casewriter *
mem_writer_create (const struct caseproto *proto)
{
  return create_casewriter_window (proto, CASENUMBER_MAX);
}

/* Returns a casewriter for cases that match case prototype
   PROTO.  The cases written to the casewriter will be written
   to disk.

   A casewriter created with this function may be passed to
   casewriter_make_reader. */
struct casewriter *
tmpfile_writer_create (const struct caseproto *proto)
{
  return create_casewriter_window (proto, 0);
}

static const struct casewriter_class casewriter_window_class;
static const struct casereader_random_class casereader_window_class;

/* Creates and returns a new casewriter based on a casewindow.
   Each of the casewriter's cases are composed of VALUE_CNT
   struct values.  The casewriter's cases will be maintained in
   memory until MAX_IN_CORE_CASES have been written, at which
   point they will be written to disk. */
static struct casewriter *
create_casewriter_window (const struct caseproto *proto,
                          casenumber max_in_core_cases)
{
  struct casewindow *window = casewindow_create (proto, max_in_core_cases);
  struct casewriter *writer = casewriter_create (proto,
                                                 &casewriter_window_class,
                                                 window);
  taint_propagate (casewindow_get_taint (window),
                   casewriter_get_taint (writer));
  return writer;
}

/* Writes case C to casewindow writer WINDOW. */
static void
casewriter_window_write (struct casewriter *writer UNUSED, void *window_,
                         struct ccase *c)
{
  struct casewindow *window = window_;
  casewindow_push_head (window, c);
}

/* Destroys casewindow writer WINDOW. */
static void
casewriter_window_destroy (struct casewriter *writer UNUSED, void *window_)
{
  struct casewindow *window = window_;
  casewindow_destroy (window);
}

/* Converts casewindow writer WINDOW to a casereader and returns
   the casereader. */
static struct casereader *
casewriter_window_convert_to_reader (struct casewriter *writer UNUSED,
                                     void *window_)
{
  struct casewindow *window = window_;
  struct casereader *reader =
    casereader_create_random (casewindow_get_proto (window),
			      casewindow_get_case_cnt (window),
			      &casereader_window_class, window);

  taint_propagate (casewindow_get_taint (window),
                   casereader_get_taint (reader));
  return reader;
}

/* Reads and returns the case at the given 0-based OFFSET from
   the front of WINDOW into C.  Returns a null pointer if OFFSET
   is beyond the end of file or upon I/O error.  The caller must
   call case_unref() on the returned case when it is no longer
   needed.*/
static struct ccase *
casereader_window_read (struct casereader *reader UNUSED, void *window_,
                        casenumber offset)
{
  struct casewindow *window = window_;
  if (offset >= casewindow_get_case_cnt (window))
    return NULL;
  return casewindow_get_case (window, offset);
}

/* Destroys casewindow reader WINDOW. */
static void
casereader_window_destroy (struct casereader *reader UNUSED, void *window_)
{
  struct casewindow *window = window_;
  casewindow_destroy (window);
}

/* Discards CASE_CNT cases from the front of WINDOW. */
static void
casereader_window_advance (struct casereader *reader UNUSED, void *window_,
                           casenumber case_cnt)
{
  struct casewindow *window = window_;
  casewindow_pop_tail (window, case_cnt);
}

/* Class for casewindow writer. */
static const struct casewriter_class casewriter_window_class =
  {
    casewriter_window_write,
    casewriter_window_destroy,
    casewriter_window_convert_to_reader,
  };

/* Class for casewindow reader. */
static const struct casereader_random_class casereader_window_class =
  {
    casereader_window_read,
    casereader_window_destroy,
    casereader_window_advance,
  };

