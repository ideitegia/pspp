/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010, 2013 Free Software Foundation, Inc.

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

#include "data/casereader.h"
#include "data/casereader-provider.h"

#include <stdlib.h>

#include "data/casereader-shim.h"
#include "data/casewriter.h"
#include "libpspp/assertion.h"
#include "libpspp/heap.h"
#include "libpspp/taint.h"

#include "gl/xalloc.h"

/* A casereader. */
struct casereader
  {
    struct taint *taint;                  /* Corrupted? */
    struct caseproto *proto;              /* Format of contained cases. */
    casenumber case_cnt;                  /* Number of cases,
                                             CASENUMBER_MAX if unknown. */
    const struct casereader_class *class; /* Class. */
    void *aux;                            /* Auxiliary data for class. */
  };

/* Reads and returns the next case from READER.  The caller owns
   the returned case and must call case_unref on it when its data
   is no longer needed.  Returns a null pointer if cases have
   been exhausted or upon detection of an I/O error.

   The case returned is effectively consumed: it can never be
   read again through READER.  If this is inconvenient, READER
   may be cloned in advance with casereader_clone, or
   casereader_peek may be used instead. */
struct ccase *
casereader_read (struct casereader *reader)
{
  if (reader->case_cnt != 0)
    {
      /* ->read may use casereader_swap to replace itself by
         another reader and then delegate to that reader by
         recursively calling casereader_read.  Currently only
         lazy_casereader does this and, with luck, nothing else
         ever will.

         To allow this to work, however, we must decrement
         case_cnt before calling ->read.  If we decremented
         case_cnt after calling ->read, then this would actually
         drop two cases from case_cnt instead of one, and we'd
         lose the last case in the casereader. */
      struct ccase *c;
      if (reader->case_cnt != CASENUMBER_MAX)
        reader->case_cnt--;
      c = reader->class->read (reader, reader->aux);
      if (c != NULL)
        {
          size_t n_widths UNUSED = caseproto_get_n_widths (reader->proto);
          assert (case_get_value_cnt (c) >= n_widths);
          expensive_assert (caseproto_equal (case_get_proto (c), 0,
                                             reader->proto, 0, n_widths));
          return c;
        }
    }
  reader->case_cnt = 0;
  return NULL;
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
      caseproto_unref (reader->proto);
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
  struct casereader *reader = CONST_CAST (struct casereader *, reader_);
  struct casereader *clone;
  if ( reader == NULL ) 
    return NULL;

  if (reader->class->clone == NULL)
    casereader_shim_insert (reader);
  clone = reader->class->clone (reader, reader->aux);
  assert (clone != NULL);
  assert (clone != reader);
  return clone;
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

/* Reads and returns the (IDX + 1)'th case from READER.  The
   caller owns the returned case and must call case_unref on it
   when it is no longer needed.  Returns a null pointer if cases
   have been exhausted or upon detection of an I/O error. */
struct ccase *
casereader_peek (struct casereader *reader, casenumber idx)
{
  if (idx < reader->case_cnt)
    {
      struct ccase *c;
      if (reader->class->peek == NULL)
        casereader_shim_insert (reader);
      c = reader->class->peek (reader, reader->aux, idx);
      if (c != NULL)
        return c;
      else if (casereader_error (reader))
        reader->case_cnt = 0;
    }
  if (reader->case_cnt > idx)
    reader->case_cnt = idx;
  return NULL;
}

/* Returns true if no cases remain to be read from READER, or if
   an error has occurred on READER.  (A return value of false
   does *not* mean that the next call to casereader_peek or
   casereader_read will return true, because an error can occur
   in the meantime.) */
bool
casereader_is_empty (struct casereader *reader)
{
  if (reader->case_cnt == 0)
    return true;
  else
    {
      struct ccase *c = casereader_peek (reader, 0);
      if (c == NULL)
        return true;
      else
        {
          case_unref (c);
          return false;
        }
    }
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

static casenumber
casereader_count_cases__ (const struct casereader *reader,
                          casenumber max_cases)
{
  struct casereader *clone;
  casenumber n_cases;

  clone = casereader_clone (reader);
  n_cases = casereader_advance (clone, max_cases);
  casereader_destroy (clone);

  return n_cases;
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
casereader_count_cases (const struct casereader *reader)
{
  if (reader->case_cnt == CASENUMBER_MAX)
    {
      struct casereader *reader_rw = CONST_CAST (struct casereader *, reader);
      reader_rw->case_cnt = casereader_count_cases__ (reader, CASENUMBER_MAX);
    }
  return reader->case_cnt;
}

/* Truncates READER to at most N cases. */
void
casereader_truncate (struct casereader *reader, casenumber n)
{
  /* This could be optimized, if it ever becomes too expensive, by adding a
     "max_cases" member to struct casereader.  We could also add a "truncate"
     function to the casereader implementation, to allow the casereader to
     throw away data that cannot ever be read. */
  if (reader->case_cnt == CASENUMBER_MAX)
    reader->case_cnt = casereader_count_cases__ (reader, n);
  if (reader->case_cnt > n)
    reader->case_cnt = n;
}

/* Returns the prototype for the cases in READER.  The caller
   must not unref the returned prototype. */
const struct caseproto *
casereader_get_proto (const struct casereader *reader)
{
  return reader->proto;
}

/* Skips past N cases in READER, stopping when the last case in
   READER has been read or on an input error.  Returns the number
   of cases successfully skipped. */
casenumber
casereader_advance (struct casereader *reader, casenumber n)
{
  casenumber i;

  for (i = 0; i < n; i++)
    {
      struct ccase *c = casereader_read (reader);
      if (c == NULL)
        break;
      case_unref (c);
    }

  return i;
}


/* Copies all the cases in READER to WRITER, propagating errors
   appropriately. READER is destroyed by this function */
void
casereader_transfer (struct casereader *reader, struct casewriter *writer)
{
  struct ccase *c;

  taint_propagate (casereader_get_taint (reader),
                   casewriter_get_taint (writer));
  while ((c = casereader_read (reader)) != NULL)
    casewriter_write (writer, c);
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

   PROTO must be the prototype for the cases that may be read
   from the casereader.  The caller retains its reference to
   PROTO.

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
                              const struct caseproto *proto,
                              casenumber case_cnt,
                              const struct casereader_class *class, void *aux)
{
  struct casereader *reader = xmalloc (sizeof *reader);
  reader->taint = taint != NULL ? taint_clone (taint) : taint_create ();
  reader->proto = caseproto_ref (proto);
  reader->case_cnt = case_cnt;
  reader->class = class;
  reader->aux = aux;
  return reader;
}

/* If READER is a casereader of the given CLASS, returns its
   associated auxiliary data; otherwise, returns a null pointer.

   This function is intended for use from casereader
   implementations, not by casereader users.  Even within
   casereader implementations, its usefulness is quite limited,
   for at least two reasons.  First, every casereader member
   function already receives a pointer to the casereader's
   auxiliary data.  Second, a casereader's class can change
   (through a call to casereader_swap) and this is in practice
   quite common (e.g. any call to casereader_clone on a
   casereader that does not directly support clone will cause the
   casereader to be replaced by a shim caseader). */
void *
casereader_dynamic_cast (struct casereader *reader,
                         const struct casereader_class *class)
{
  return reader->class == class ? reader->aux : NULL;
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

static const struct casereader_class random_reader_casereader_class;

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

   PROTO must be the prototype for the cases that may be read
   from the casereader.  The caller retains its reference to
   PROTO.

   CASE_CNT is an upper limit on the number of cases that
   casereader_read will return from the casereader in successive
   calls.  Ordinarily, this is the actual number of cases in the
   data source or CASENUMBER_MAX if the number of cases cannot be
   predicted in advance.

   CLASS and AUX are a set of casereader implementation-specific
   member functions and auxiliary data to pass to those member
   functions, respectively. */
struct casereader *
casereader_create_random (const struct caseproto *proto, casenumber case_cnt,
                          const struct casereader_random_class *class,
                          void *aux)
{
  struct random_reader_shared *shared = xmalloc (sizeof *shared);
  shared->readers = heap_create (compare_random_readers_by_offset, NULL);
  shared->class = class;
  shared->aux = aux;
  shared->min_offset = 0;
  return casereader_create_sequential (NULL, proto, case_cnt,
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
static struct ccase *
random_reader_read (struct casereader *reader, void *br_)
{
  struct random_reader *br = br_;
  struct random_reader_shared *shared = br->shared;
  struct ccase *c = shared->class->read (reader, shared->aux,
                                         br->offset - shared->min_offset);
  if (c != NULL)
    {
      br->offset++;
      heap_changed (shared->readers, &br->heap_node);
      advance_random_reader (reader, shared);
    }
  return c;
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
                                       reader->proto,
                                       casereader_get_case_cnt (reader),
                                       &random_reader_casereader_class,
                                       make_random_reader (shared,
                                                           br->offset));
}

/* struct casereader_class "peek" function for random reader. */
static struct ccase *
random_reader_peek (struct casereader *reader, void *br_, casenumber idx)
{
  struct random_reader *br = br_;
  struct random_reader_shared *shared = br->shared;

  return shared->class->read (reader, shared->aux,
                              br->offset - shared->min_offset + idx);
}

/* Casereader class for random reader. */
static const struct casereader_class random_reader_casereader_class =
  {
    random_reader_read,
    random_reader_destroy,
    random_reader_clone,
    random_reader_peek,
  };


static const struct casereader_class casereader_null_class;

/* Returns a casereader with no cases.  The casereader has the prototype
   specified by PROTO.  PROTO may be specified as a null pointer, in which case
   the casereader has no variables. */
struct casereader *
casereader_create_empty (const struct caseproto *proto_)
{
  struct casereader *reader;
  struct caseproto *proto;

  proto = proto_ != NULL ? caseproto_ref (proto_) : caseproto_create ();
  reader = casereader_create_sequential (NULL, proto, 0,
                                         &casereader_null_class, NULL);
  caseproto_unref (proto);

  return reader;
}

static struct ccase *
casereader_null_read (struct casereader *reader UNUSED, void *aux UNUSED)
{
  return NULL;
}

static void
casereader_null_destroy (struct casereader *reader UNUSED, void *aux UNUSED)
{
  /* Nothing to do. */
}

static const struct casereader_class casereader_null_class =
  {
    casereader_null_read,
    casereader_null_destroy,
    NULL,                       /* clone */
    NULL,                       /* peek */
  };
