/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2011 Free Software Foundation, Inc.

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

#include "data/casereader-provider.h"
#include "data/subcase.h"

#include "gl/xalloc.h"

static bool
projection_is_no_op (const struct casereader *reader, const struct subcase *sc)
{
  size_t n = subcase_get_n_fields (sc);
  size_t i;

  if (n != caseproto_get_n_widths (casereader_get_proto (reader)))
    return false;

  for (i = 0; i < n; i++)
    if (subcase_get_case_index (sc, i) != i)
      return false;

  return true;
}

struct casereader_project
  {
    struct subcase old_sc;
    struct subcase new_sc;
  };

static struct ccase *
project_case (struct ccase *old, casenumber idx UNUSED, const void *project_)
{
  const struct casereader_project *project = project_;
  struct ccase *new = case_create (subcase_get_proto (&project->new_sc));
  subcase_copy (&project->old_sc, old, &project->new_sc, new);
  case_unref (old);
  return new;
}

static bool
destroy_projection (void *project_)
{
  struct casereader_project *project = project_;
  subcase_destroy (&project->old_sc);
  subcase_destroy (&project->new_sc);
  free (project);
  return true;
}

/* Returns a casereader in which each row is obtained by extracting the subcase
   SC from the corresponding row of SUBREADER. */
struct casereader *
casereader_project (struct casereader *subreader, const struct subcase *sc)
{
  if (projection_is_no_op (subreader, sc))
    return casereader_rename (subreader);
  else
    {
      struct casereader_project *project = xmalloc (sizeof *project);
      const struct caseproto *proto;

      subcase_clone (&project->old_sc, sc);
      proto = subcase_get_proto (&project->old_sc);

      subcase_init_empty (&project->new_sc);
      subcase_add_proto_always (&project->new_sc, proto);

      return casereader_translate_stateless (subreader, proto,
                                             project_case, destroy_projection,
                                             project);
    }
}

/* Returns a casereader in which each row is obtained by extracting the value
   with index COLUMN from the corresponding row of SUBREADER. */
struct casereader *
casereader_project_1 (struct casereader *subreader, int column)
{
  const struct caseproto *subproto = casereader_get_proto (subreader);
  struct casereader *reader;
  struct subcase sc;

  subcase_init (&sc, column, caseproto_get_width (subproto, column),
                SC_ASCEND);
  reader = casereader_project (subreader, &sc);
  subcase_destroy (&sc);

  return reader;
}

