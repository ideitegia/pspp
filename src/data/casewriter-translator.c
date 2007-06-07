/* PSPP - computes sample statistics.
   Copyright (C) 2007 Free Software Foundation, Inc.

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

#include <data/casewriter.h>
#include <data/casewriter-provider.h>

#include <stdlib.h>

#include <libpspp/taint.h>

#include "xalloc.h"

struct casewriter_translator
  {
    struct casewriter *subwriter;

    void (*translate) (const struct ccase *input, struct ccase *output,
                       void *aux);
    bool (*destroy) (void *aux);
    void *aux;
  };

static struct casewriter_class casewriter_translator_class;

struct casewriter *
casewriter_create_translator (struct casewriter *subwriter,
                              void (*translate) (const struct ccase *input,
                                                 struct ccase *output,
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
  writer = casewriter_create (&casewriter_translator_class, ct);
  taint_propagate (casewriter_get_taint (ct->subwriter),
                   casewriter_get_taint (writer));
  return writer;
}

static void
casewriter_translator_write (struct casewriter *writer UNUSED,
                             void *ct_, struct ccase *c) 
{
  struct casewriter_translator *ct = ct_;
  struct ccase tmp;

  ct->translate (c, &tmp, ct->aux);
  casewriter_write (ct->subwriter, &tmp);
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

static struct casewriter_class casewriter_translator_class = 
  {
    casewriter_translator_write,
    casewriter_translator_destroy,
    casewriter_translator_convert_to_reader,
  };
