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

#include <stdlib.h>

#include "libpspp/taint.h"

#include "gl/xalloc.h"

struct casewriter_translator
  {
    struct casewriter *subwriter;

    struct ccase *(*translate) (struct ccase *, void *aux);
    bool (*destroy) (void *aux);
    void *aux;
  };

static const struct casewriter_class casewriter_translator_class;

/* Creates and returns a new casewriter whose cases are passed
   through TRANSLATE, based on INPUT and auxiliary data AUX.
   (TRANSLATE may also return a null pointer, in which case no
   case is written to the output.)  The translated cases are then
   written to SUBWRITER.

   The cases returned by TRANSLATE must match OUTPUT_PROTO.

   TRANSLATE takes ownership of each case passed to it.  Thus, it
   should either unref each case and return a new case, or
   (unshare and then) modify and return the same case.

   When the translating casewriter is destroyed, DESTROY will be
   called to allow any state maintained by TRANSLATE to be freed.

   After this function is called, SUBWRITER must not ever again
   be referenced directly.  It will be destroyed automatically
   when the translating casewriter is destroyed. */
struct casewriter *
casewriter_create_translator (struct casewriter *subwriter,
                              const struct caseproto *translated_proto,
                              struct ccase *(*translate) (struct ccase *,
                                                          void *aux),
                              bool (*destroy) (void *aux),
                              void *aux)
{
  struct casewriter_translator *ct = xmalloc (sizeof *ct);
  struct casewriter *writer;
  ct->subwriter = casewriter_rename (subwriter);
  ct->translate = translate;
  ct->destroy = destroy;
  ct->aux = aux;
  writer = casewriter_create (translated_proto,
                              &casewriter_translator_class, ct);
  taint_propagate (casewriter_get_taint (ct->subwriter),
                   casewriter_get_taint (writer));
  return writer;
}

static void
casewriter_translator_write (struct casewriter *writer UNUSED,
                             void *ct_, struct ccase *c)
{
  struct casewriter_translator *ct = ct_;
  c = ct->translate (c, ct->aux);
  if (c != NULL)
    casewriter_write (ct->subwriter, c);
}

static void
casewriter_translator_destroy (struct casewriter *writer UNUSED, void *ct_)
{
  struct casewriter_translator *ct = ct_;
  casewriter_destroy (ct->subwriter);
  ct->destroy (ct->aux);
  free (ct);
}

static struct casereader *
casewriter_translator_convert_to_reader (struct casewriter *writer UNUSED,
                                         void *ct_)
{
  struct casewriter_translator *ct = ct_;
  struct casereader *reader = casewriter_make_reader (ct->subwriter);
  free (ct);
  ct->destroy (ct->aux);
  return reader;
}

static const struct casewriter_class casewriter_translator_class =
  {
    casewriter_translator_write,
    casewriter_translator_destroy,
    casewriter_translator_convert_to_reader,
  };
