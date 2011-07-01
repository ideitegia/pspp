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

#include "data/lazy-casereader.h"

#include <stdlib.h>

#include "data/case.h"
#include "data/casereader.h"
#include "data/casereader-provider.h"
#include "libpspp/assertion.h"

#include "gl/xalloc.h"

/* A lazy casereader's auxiliary data. */
struct lazy_casereader
  {
    unsigned long int serial;
    struct casereader *(*callback) (void *aux);
    void *aux;
  };

static const struct casereader_class lazy_casereader_class;

/* Creates and returns a new lazy casereader that will
   instantiate its underlying casereader, if necessary, by
   calling CALLBACK, passing AUX as its argument.  *SERIAL is set
   to a "serial number" that uniquely identifies the new lazy
   casereader, for use with lazy_casereader_destroy.

   PROTO must be the format of the cases to be read from the
   casereader.

   CASE_CNT is an upper limit on the number of cases that
   casereader_read will return from the casereader in successive
   calls.  Ordinarily, this is the actual number of cases in the
   data source or CASENUMBER_MAX if the number of cases cannot be
   predicted in advance. */
struct casereader *
lazy_casereader_create (const struct caseproto *proto, casenumber case_cnt,
                        struct casereader *(*callback) (void *aux), void *aux,
                        unsigned long int *serial)
{
  static unsigned long int next_serial = 0;
  struct lazy_casereader *lc;
  assert (callback != NULL);
  lc = xmalloc (sizeof *lc);
  *serial = lc->serial = next_serial++;
  lc->callback = callback;
  lc->aux = aux;
  return casereader_create_sequential (NULL, proto, case_cnt,
                                       &lazy_casereader_class, lc);
}

/* If READER is the lazy casereader that was returned by
   lazy_casereader_create along with SERIAL, and READER was never
   instantiated by any use of a casereader function, then this
   function destroys READER without instantiating it, and returns
   true.  Returns false in any other case; that is, if READER is
   not a lazy casereader, or if READER is a lazy casereader with
   a serial number different from SERIAL, or if READER is a lazy
   casereader that was instantiated.

   When this function returns true, it necessarily indicates
   that the lazy casereader was never cloned and never
   destroyed. */
bool
lazy_casereader_destroy (struct casereader *reader, unsigned long int serial)
{
  struct lazy_casereader *lc;

  if (reader == NULL)
    return false;

  lc = casereader_dynamic_cast (reader, &lazy_casereader_class);
  if (lc == NULL || lc->serial != serial)
    return false;

  lc->callback = NULL;
  casereader_destroy (reader);
  return true;
}

/* Instantiates lazy casereader READER, which is associated with
   LC. */
static void
instantiate_lazy_casereader (struct casereader *reader,
                             struct lazy_casereader *lc)
{
  struct casereader *subreader;

  /* Call the client-provided callback to obtain the real
     casereader, then swap READER with that casereader. */
  subreader = lc->callback (lc->aux);
  casereader_swap (reader, subreader);

  /* Now destroy the lazy casereader, which is no longer needed
     since we already swapped it out.  Set the callback to null
     to prevent lazy_casereader_do_destroy from trying to
     instantiate it again.  */
  lc->callback = NULL;
  casereader_destroy (subreader);
}

static struct ccase *
lazy_casereader_read (struct casereader *reader, void *lc_)
{
  struct lazy_casereader *lc = lc_;
  instantiate_lazy_casereader (reader, lc);
  return casereader_read (reader);
}

static void
lazy_casereader_do_destroy (struct casereader *reader UNUSED, void *lc_)
{
  struct lazy_casereader *lc = lc_;
  if (lc->callback != NULL)
    casereader_destroy (lc->callback (lc->aux));
  free (lc);
}

static struct casereader *
lazy_casereader_clone (struct casereader *reader, void *lc_)
{
  struct lazy_casereader *lc = lc_;
  instantiate_lazy_casereader (reader, lc);
  return casereader_clone (reader);
}

static struct ccase *
lazy_casereader_peek (struct casereader *reader, void *lc_, casenumber idx)
{
  struct lazy_casereader *lc = lc_;
  instantiate_lazy_casereader (reader, lc);
  return casereader_peek (reader, idx);
}

static const struct casereader_class lazy_casereader_class =
  {
    lazy_casereader_read,
    lazy_casereader_do_destroy,
    lazy_casereader_clone,
    lazy_casereader_peek,
  };
