/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010 Free Software Foundation, Inc.

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

#include "data/casereader-shim.h"

#include <stdlib.h>

#include "data/casereader.h"
#include "data/casereader-provider.h"
#include "data/casewindow.h"
#include "data/settings.h"
#include "libpspp/taint.h"

#include "gl/xalloc.h"

/* A buffering shim casereader. */
struct casereader_shim
  {
    struct casewindow *window;          /* Window of buffered cases. */
    struct casereader *subreader;       /* Subordinate casereader. */
  };

static const struct casereader_random_class shim_class;

static bool buffer_case (struct casereader_shim *s);

/* Interposes a buffering shim on READER.

   Returns the new shim.  The only legitimate use of the returned
   casereader_shim is for calling casereader_shim_slurp().  If READER has no
   clones already (which the caller should ensure, if it plans to use the
   return value), then the returned casreader_shim is valid for that purpose
   until, and only until, the READER's 'destroy' function is called. */
struct casereader_shim *
casereader_shim_insert (struct casereader *reader)
{
  const struct caseproto *proto = casereader_get_proto (reader);
  casenumber case_cnt = casereader_get_case_cnt (reader);
  struct casereader_shim *s = xmalloc (sizeof *s);
  s->window = casewindow_create (proto, settings_get_workspace_cases (proto));
  s->subreader = casereader_create_random (proto, case_cnt, &shim_class, s);
  casereader_swap (reader, s->subreader);
  taint_propagate (casewindow_get_taint (s->window),
                   casereader_get_taint (reader));
  taint_propagate (casereader_get_taint (s->subreader),
                   casereader_get_taint (reader));
  return s;
}

/* Reads all of the cases from S's subreader into S's buffer and destroys S's
   subreader.  (This is a no-op if the subreader has already been
   destroyed.)

   Refer to the comment on casereader_shim_insert() for information on when
   this function may be used. */
void
casereader_shim_slurp (struct casereader_shim *s)
{
  while (buffer_case (s))
    continue;
}

/* Reads a case from S's subreader and appends it to S's window.  Returns true
   if successful, false at the end of S's subreader or upon an I/O error. */
static bool
buffer_case (struct casereader_shim *s)
{
  struct ccase *tmp;

  if (s->subreader == NULL)
    return false;

  tmp = casereader_read (s->subreader);
  if (tmp == NULL)
    {
      casereader_destroy (s->subreader);
      s->subreader = NULL;
      return false;
    }

  casewindow_push_head (s->window, tmp);
  return true;
}

/* Reads the case at the given 0-based OFFSET from the front of the window into
   C.  Returns the case if successful, or a null pointer if OFFSET is beyond
   the end of file or upon I/O error.  The caller must call case_unref() on the
   returned case when it is no longer needed. */
static struct ccase *
casereader_shim_read (struct casereader *reader UNUSED, void *s_,
                      casenumber offset)
{
  struct casereader_shim *s = s_;

  while (casewindow_get_case_cnt (s->window) <= offset)
    if (!buffer_case (s))
      return false;

  return casewindow_get_case (s->window, offset);
}

/* Destroys S. */
static void
casereader_shim_destroy (struct casereader *reader UNUSED, void *s_)
{
  struct casereader_shim *s = s_;
  casewindow_destroy (s->window);
  casereader_destroy (s->subreader);
  free (s);
}

/* Discards CNT cases from the front of S's window. */
static void
casereader_shim_advance (struct casereader *reader UNUSED, void *s_,
                         casenumber case_cnt)
{
  struct casereader_shim *s = s_;
  casewindow_pop_tail (s->window, case_cnt);
}

/* Class for the buffered reader. */
static const struct casereader_random_class shim_class =
  {
    casereader_shim_read,
    casereader_shim_destroy,
    casereader_shim_advance,
  };
