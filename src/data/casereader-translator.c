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

#include <stdlib.h>

#include <data/casereader-provider.h>
#include <libpspp/taint.h>

#include "xalloc.h"

/* Casereader that applies a user-supplied function to translate
   each case into another in an arbitrary fashion. */

/* A translating casereader. */
struct casereader_translator
  {
    struct casereader *subreader; /* Source of input cases. */

    void (*translate) (struct ccase *input, struct ccase *output, void *aux);
    bool (*destroy) (void *aux);
    void *aux;
  };

static const struct casereader_class casereader_translator_class;

/* Creates and returns a new casereader whose cases are produced
   by reading from SUBREADER and passing through TRANSLATE, which
   must create case OUTPUT, with OUTPUT_VALUE_CNT values, and
   populate it based on INPUT and auxiliary data AUX.  TRANSLATE
   must also destroy INPUT.

   When the translating casereader is destroyed, DESTROY will be
   called to allow any state maintained by TRANSLATE to be freed.

   After this function is called, SUBREADER must not ever again
   be referenced directly.  It will be destroyed automatically
   when the translating casereader is destroyed. */
struct casereader *
casereader_create_translator (struct casereader *subreader,
                              size_t output_value_cnt,
                              void (*translate) (struct ccase *input,
                                                 struct ccase *output,
                                                 void *aux),
                              bool (*destroy) (void *aux),
                              void *aux)
{
  struct casereader_translator *ct = xmalloc (sizeof *ct);
  struct casereader *reader;
  ct->subreader = casereader_rename (subreader);
  ct->translate = translate;
  ct->destroy = destroy;
  ct->aux = aux;
  reader = casereader_create_sequential (
    NULL, output_value_cnt, casereader_get_case_cnt (ct->subreader),
    &casereader_translator_class, ct);
  taint_propagate (casereader_get_taint (ct->subreader),
                   casereader_get_taint (reader));
  return reader;
}

/* Internal read function for translating casereader. */
static bool
casereader_translator_read (struct casereader *reader UNUSED,
                            void *ct_, struct ccase *c)
{
  struct casereader_translator *ct = ct_;
  struct ccase tmp;

  if (casereader_read (ct->subreader, &tmp))
    {
      ct->translate (&tmp, c, ct->aux);
      return true;
    }
  else
    return false;
}

/* Internal destroy function for translating casereader. */
static void
casereader_translator_destroy (struct casereader *reader UNUSED, void *ct_)
{
  struct casereader_translator *ct = ct_;
  casereader_destroy (ct->subreader);
  ct->destroy (ct->aux);
  free (ct);
}

/* Casereader class for translating casereader. */
static const struct casereader_class casereader_translator_class =
  {
    casereader_translator_read,
    casereader_translator_destroy,
    NULL,
    NULL,
  };
