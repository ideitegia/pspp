/* PSPP - a program for statistical analysis.
   Copyright (C) 2007 Free Software Foundation, Inc.

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

#include <data/casereader.h>
#include <data/casereader-provider.h>

#include <stdlib.h>

#include <data/casewindow.h>
#include <data/casewriter.h>
#include <data/settings.h>
#include <libpspp/assertion.h>
#include <libpspp/heap.h>
#include <libpspp/taint.h>

#include "xalloc.h"

/* A casereader. */
struct casereader
  {
    struct taint *taint;                  /* Corrupted? */
    size_t value_cnt;                     /* Values per case. */
    casenumber case_cnt;                  /* Number of cases,
                                             CASENUMBER_MAX if unknown. */
    const struct casereader_class *class; /* Class. */
    void *aux;                            /* Auxiliary data for class. */
  };

static void insert_shim (struct casereader *);

/* Creates a new case in C and reads the next case from READER
   into it.  The caller owns C and must destroy C when its data
   is no longer needed.  Return true if successful, false when
   cases have been exhausted or upon detection of an I/O error.
   In the latter case, C is set to the null case.

   The case returned is effectively consumed: it can never be
   read again through READER.  If this is inconvenient, READER
   may be cloned in advance with casereader_clone, or
   casereader_peek may be used instead. */
bool
casereader_read (struct casereader *reader, struct ccase *c)
{
  if (reader->case_cnt != 0 && reader->class->read (reader, reader->aux, c))
    {
      assert (case_get_value_cnt (c) >= reader->value_cnt);
      if (reader->case_cnt != CASENUMBER_MAX)
        reader->case_cnt--;
      return true;
    }
  else
    {
      reader->case_cnt = 0;
      case_nullify (c);
      return false;
    }
}

/* Destroys READER.
   Returns false if an I/O error was detected on READER, true
   otherwise. */
bool
casereader_destroy (struct casereader *reader)
{
  bool ok = true;
  if (reader != NULL)
    {
      reader->class->destroy (reader, reader->aux);
      ok = taint_destroy (reader->taint);
      free (reader);
    }
  return ok;
}

/* Returns a clone of READER.  READER and its clone may be used
   to read the same sequence of cases in the same order, barring
   I/O errors. */
struct casereader *
casereader_clone (const struct casereader *reader_)
{
  struct casereader *reader = (struct casereader *) reader_;
  struct casereader *clone;
  if ( reader == NULL ) 
    return NULL;

  if (reader->class->clone == NULL)
    insert_shim (reader);
  clone = reader->class->clone (reader, reader->aux);
  assert (clone != NULL);
  assert (clone != reader);
  return clone;
}

/* Makes a copy of ORIGINAL into *NEW1 (if NEW1 is non-null) and
   *NEW2 (if NEW2 is non-null), then destroys ORIGINAL. */
void
casereader_split (struct casereader *original,
                  struct casereader **new1, struct casereader **new2)
{
  if (new1 != NULL && new2 != NULL)
    {
      *new1 = casereader_rename (original);
      *new2 = casereader_clone (*new1);
    }
  else if (new1 != NULL)
    *new1 = casereader_rename (original);
  else if (new2 != NULL)
    *new2 = casereader_rename (original);
  else
    casereader_destroy (original);
}

/* Returns a copy of READER, which is itself destroyed.
   Useful for taking over ownership of a casereader, to enforce
   preventing the original owner from accessing the casereader
   again. */
struct casereader *
casereader_rename (struct casereader *reader)
{
  struct casereader *new = xmemdup (reader, sizeof *reader);
  free (reader);
  return new;
}

/* Exchanges the casereaders referred to by A and B. */
void
casereader_swap (struct casereader *a, struct casereader *b)
{
  if (a != b)
    {
      struct casereader tmp = *a;
      *a = *b;
      *b = tmp;
    }
}

/* Creates a new case in C and reads the (IDX + 1)'th case from
   READER into it.  The caller owns C and must destroy C when its
   data is no longer needed.  Return true if successful, false
   when cases have been exhausted or upon detection of an I/O
   error.  In the latter case, C is set to the null case. */
bool
casereader_peek (struct casereader *reader, casenumber idx, struct ccase *c)
{
  if (idx < reader->case_cnt)
    {
      if (reader->class->peek == NULL)
        insert_shim (reader);
      if (reader->class->peek (reader, reader->aux, idx, c))
        return true;
      else if (casereader_error (reader))
        reader->case_cnt = 0;
    }
  if (reader->case_cnt > idx)
    reader->case_cnt = idx;
  case_nullify (c);
  return false;
}

/* Returns true if an I/O error or another hard error has
   occurred on READER, a clone of READER, or on some object on
   which READER's data has a dependency, false otherwise. */
bool
casereader_error (const struct casereader *reader)
{
  return taint_is_tainted (reader->taint);
}

/* Marks READER as having encountered an error.

   Ordinarily, this function should be called by the
   implementation of a casereader, not by the casereader's
   client.  Instead, casereader clients should usually ensure
   that a casereader's error state is correct by using
   taint_propagate to propagate to the casereader's taint
   structure, which may be obtained via casereader_get_taint. */
void
casereader_force_error (struct casereader *reader)
{
  taint_set_taint (reader->taint);
}

/* Returns READER's associate taint object, for use with
   taint_propagate and other taint functions. */
const struct taint *
casereader_get_taint (const struct casereader *reader)
{
  return reader->taint;
}

/* Returns the number of cases that will be read by successive
   calls to casereader_read for READER, assuming that no errors
   occur.  Upon an error condition, the case count drops to 0, so
   that no more cases can be obtained.

   Not all casereaders can predict the number of cases that they
   will produce without actually reading all of them.  In that
   case, this function returns CASENUMBER_MAX.  To obtain the
   actual number of cases in such a casereader, use
   casereader_count_cases. */
casenumber
casereader_get_case_cnt (struct casereader *reader)
{
  return reader->case_cnt;
}

/* Returns the number of cases that will be read by successive
   calls to casereader_read for READER, assuming that no errors
   occur.  Upon an error condition, the case count drops to 0, so
   that no more cases can be obtained.

   For a casereader that cannot predict the number of cases it
   will produce, this function actually reads (and discards) all
   of the contents of a clone of READER.  Thus, the return value
   is always correct in the absence of I/O errors. */
casenumber
casereader_count_cases (struct casereader *reader)
{
  if (reader->case_cnt == CASENUMBER_MAX)
    {
      casenumber n_cases = 0;
      struct ccase c;

      struct casereader *clone = casereader_clone (reader);

      for (; casereader_read (clone, &c); case_destroy (&c))
        n_cases++;

      casereader_destroy (clone);
      reader->case_cnt = n_cases;
    }

  return reader->case_cnt;
}

/* Returns the number of struct values in each case in READER. */
size_t
casereader_get_value_cnt (struct casereader *reader)
{
  return reader->value_cnt;
}

/* Copies all the cases in READER to WRITER, propagating errors
   appropriately. */
void
casereader_transfer (struct casereader *reader, struct casewriter *writer)
{
  struct ccase c;

  taint_propagate (casereader_get_taint (reader),
                   casewriter_get_taint (writer));
  while (casereader_read (reader, &c))
    casewriter_write (writer, &c);
  casereader_destroy (reader);
}

/* Creates and returns a new casereader.  This function is
   intended for use by casereader implementations, not by
   casereader clients.

   This function is most suited for creating a casereader for a
   data source that is naturally sequential.
   casereader_create_random may be more appropriate for a data
   source that supports random access.

   Ordinarily, specify a null pointer for TAINT, in which case
   the new casereader will have a new, unique taint object.  If
   the new casereader should have a clone of an existing taint
   object, specify that object as TAINT.  (This is most commonly
   useful in an implementation of the "clone" casereader_class
   function, in which case the cloned casereader should have the
   same taint object as the original casereader.)

   VALUE_CNT must be the number of struct values per case read
   from the casereader.

   CASE_CNT is an upper limit on the number of cases that
   casereader_read will return from the casereader in successive
   calls.  Ordinarily, this is the actual number of cases in the
   data source or CASENUMBER_MAX if the number of cases cannot be
   predicted in advance.

   CLASS and AUX are a set of casereader implementation-specific
   member functions and auxiliary data to pass to those member
   functions, respectively. */
struct casereader *
casereader_create_sequential (const struct taint *taint,
                              size_t value_cnt, casenumber case_cnt,
                              const struct casereader_class *class, void *aux)
{
  struct casereader *reader = xmalloc (sizeof *reader);
  reader->taint = taint != NULL ? taint_clone (taint) : taint_create ();
  reader->value_cnt = value_cnt;
  reader->case_cnt = case_cnt;
  reader->class = class;
  reader->aux = aux;
  return reader;
}

/* Random-access casereader implementation.

   This is a set of wrappers around casereader_create_sequential
   and struct casereader_class to make it easy to create
   efficient casereaders for data sources that natively support
   random access. */

/* One clone of a random reader. */
struct random_reader
  {
    struct random_reader_shared *shared; /* Data shared among clones. */
    struct heap_node heap_node; /* Node in shared data's heap of readers. */
    casenumber offset;          /* Number of cases already read. */
  };

/* Returns the random_reader in which the given heap_node is
   embedded. */
static struct random_reader *
random_reader_from_heap_node (const struct heap_node *node)
{
  return heap_data (node, struct random_reader, heap_node);
}

/* Data shared among clones of a random reader. */
struct random_reader_shared
  {
    struct heap *readers;       /* Heap of struct random_readers. */
    casenumber min_offset;      /* Smallest offset of any random_reader. */
    const struct casereader_random_class *class;
    void *aux;
  };

static struct casereader_class random_reader_casereader_class;

/* Creates and returns a new random_reader with the given SHARED
   data and OFFSET.  Inserts the new random reader into the
   shared heap. */
static struct random_reader *
make_random_reader (struct random_reader_shared *shared, casenumber offset)
{
  struct random_reader *br = xmalloc (sizeof *br);
  br->offset = offset;
  br->shared = shared;
  heap_insert (shared->readers, &br->heap_node);
  return br;
}

/* Compares random_readers A and B by offset and returns a
   strcmp()-like result. */
static int
compare_random_readers_by_offset (const struct heap_node *a_,
                                  const struct heap_node *b_,
                                  const void *aux UNUSED)
{
  const struct random_reader *a = random_reader_from_heap_node (a_);
  const struct random_reader *b = random_reader_from_heap_node (b_);
  return a->offset < b->offset ? -1 : a->offset > b->offset;
}

/* Creates and returns a new casereader.  This function is
   intended for use by casereader implementations, not by
   casereader clients.

   This function is most suited for creating a casereader for a
   data source that supports random access.
   casereader_create_sequential is more appropriate for a data
   source that is naturally sequential.

   VALUE_CNT must be the number of struct values per case read
   from the casereader.

   CASE_CNT is an upper limit on the number of cases that
   casereader_read will return from the casereader in successive
   calls.  Ordinarily, this is the actual number of cases in the
   data source or CASENUMBER_MAX if the number of cases cannot be
   predicted in advance.

   CLASS and AUX are a set of casereader implementation-specific
   member functions and auxiliary data to pass to those member
   functions, respectively. */
struct casereader *
casereader_create_random (size_t value_cnt, casenumber case_cnt,
                          const struct casereader_random_class *class,
                          void *aux)
{
  struct random_reader_shared *shared = xmalloc (sizeof *shared);
  shared->readers = heap_create (compare_random_readers_by_offset, NULL);
  shared->class = class;
  shared->aux = aux;
  shared->min_offset = 0;
  return casereader_create_sequential (NULL, value_cnt, case_cnt,
                                       &random_reader_casereader_class,
                                       make_random_reader (shared, 0));
}

/* Reassesses the min_offset in SHARED based on the minimum
   offset in the heap.   */
static void
advance_random_reader (struct casereader *reader,
                       struct random_reader_shared *shared)
{
  casenumber old, new;

  old = shared->min_offset;
  new = random_reader_from_heap_node (heap_minimum (shared->readers))->offset;
  assert (new >= old);
  if (new > old)
    {
      shared->min_offset = new;
      shared->class->advance (reader, shared->aux, new - old);
    }
}

/* struct casereader_class "read" function for random reader. */
static bool
random_reader_read (struct casereader *reader, void *br_, struct ccase *c)
{
  struct random_reader *br = br_;
  struct random_reader_shared *shared = br->shared;

  if (shared->class->read (reader, shared->aux,
                           br->offset - shared->min_offset, c))
    {
      br->offset++;
      heap_changed (shared->readers, &br->heap_node);
      advance_random_reader (reader, shared);
      return true;
    }
  else
    return false;
}

/* struct casereader_class "destroy" function for random
   reader. */
static void
random_reader_destroy (struct casereader *reader, void *br_)
{
  struct random_reader *br = br_;
  struct random_reader_shared *shared = br->shared;

  heap_delete (shared->readers, &br->heap_node);
  if (heap_is_empty (shared->readers))
    {
      heap_destroy (shared->readers);
      shared->class->destroy (reader, shared->aux);
      free (shared);
    }
  else
    advance_random_reader (reader, shared);

  free (br);
}

/* struct casereader_class "clone" function for random reader. */
static struct casereader *
random_reader_clone (struct casereader *reader, void *br_)
{
  struct random_reader *br = br_;
  struct random_reader_shared *shared = br->shared;
  return casereader_create_sequential (casereader_get_taint (reader),
                                       casereader_get_value_cnt (reader),
                                       casereader_get_case_cnt (reader),
                                       &random_reader_casereader_class,
                                       make_random_reader (shared,
                                                           br->offset));
}

/* struct casereader_class "peek" function for random reader. */
static bool
random_reader_peek (struct casereader *reader, void *br_,
                    casenumber idx, struct ccase *c)
{
  struct random_reader *br = br_;
  struct random_reader_shared *shared = br->shared;

  return shared->class->read (reader, shared->aux,
                              br->offset - shared->min_offset + idx, c);
}

/* Casereader class for random reader. */
static struct casereader_class random_reader_casereader_class =
  {
    random_reader_read,
    random_reader_destroy,
    random_reader_clone,
    random_reader_peek,
  };

/* Buffering shim for implementing clone and peek operations.

   The "clone" and "peek" operations aren't implemented by all
   types of casereaders, but we have to expose a uniform
   interface anyhow.  We do this by interposing a buffering
   casereader on top of the existing casereader on the first call
   to "clone" or "peek".  The buffering casereader maintains a
   window of cases that spans the positions of the original
   casereader and all of its clones (the "clone set"), from the
   position of the casereader that has read the fewest cases to
   the position of the casereader that has read the most.

   Thus, if all of the casereaders in the clone set are at
   approximately the same position, only a few cases are buffered
   and there is little inefficiency.  If, on the other hand, one
   casereader is not used to read any cases at all, but another
   one is used to read all of the cases, the entire contents of
   the casereader is copied into the buffer.  This still might
   not be so inefficient, given that case data in memory is
   shared across multiple identical copies, but in the worst case
   the window implementation will write cases to disk instead of
   maintaining them in-memory. */

/* A buffering shim for a non-clonable or non-peekable
   casereader. */
struct shim
  {
    struct casewindow *window;          /* Window of buffered cases. */
    struct casereader *subreader;       /* Subordinate casereader. */
  };

static struct casereader_random_class shim_class;

/* Interposes a buffering shim atop READER. */
static void
insert_shim (struct casereader *reader)
{
  size_t value_cnt = casereader_get_value_cnt (reader);
  casenumber case_cnt = casereader_get_case_cnt (reader);
  struct shim *b = xmalloc (sizeof *b);
  b->window = casewindow_create (value_cnt, get_workspace_cases (value_cnt));
  b->subreader = casereader_create_random (value_cnt, case_cnt,
                                           &shim_class, b);
  casereader_swap (reader, b->subreader);
  taint_propagate (casewindow_get_taint (b->window),
                   casereader_get_taint (reader));
  taint_propagate (casereader_get_taint (b->subreader),
                   casereader_get_taint (reader));
}

/* Ensures that B's window contains at least CASE_CNT cases.
   Return true if successful, false upon reaching the end of B's
   subreader or an I/O error. */
static bool
prime_buffer (struct shim *b, casenumber case_cnt)
{
  while (casewindow_get_case_cnt (b->window) < case_cnt)
    {
      struct ccase tmp;
      if (!casereader_read (b->subreader, &tmp))
        return false;
      casewindow_push_head (b->window, &tmp);
    }
  return true;
}

/* Reads the case at the given 0-based OFFSET from the front of
   the window into C.  Returns true if successful, false if
   OFFSET is beyond the end of file or upon I/O error. */
static bool
shim_read (struct casereader *reader UNUSED, void *b_,
           casenumber offset, struct ccase *c)
{
  struct shim *b = b_;
  return (prime_buffer (b, offset + 1)
          && casewindow_get_case (b->window, offset, c));
}

/* Destroys B. */
static void
shim_destroy (struct casereader *reader UNUSED, void *b_)
{
  struct shim *b = b_;
  casewindow_destroy (b->window);
  casereader_destroy (b->subreader);
  free (b);
}

/* Discards CNT cases from the front of B's window. */
static void
shim_advance (struct casereader *reader UNUSED, void *b_, casenumber case_cnt)
{
  struct shim *b = b_;
  casewindow_pop_tail (b->window, case_cnt);
}

/* Class for the buffered reader. */
static struct casereader_random_class shim_class =
  {
    shim_read,
    shim_destroy,
    shim_advance,
  };
