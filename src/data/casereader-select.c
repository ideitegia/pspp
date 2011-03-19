/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "gl/xalloc.h"

struct casereader_select
  {
    casenumber by;
    casenumber i;
  };

static bool
casereader_select_include (const struct ccase *c UNUSED, void *cs_)
{
  struct casereader_select *cs = cs_;
  if (++cs->i >= cs->by)
    {
      cs->i = 0;
      return true;
    }
  else
    return false;
}

static bool
casereader_select_destroy (void *cs_)
{
  struct casereader_select *cs = cs_;
  free (cs);
  return true;
}

/* Returns a casereader that contains cases FIRST though LAST, exclusive, of
   those within SUBREADER.  (The first case in SUBREADER is number 0.)  If BY
   is greater than 1, then it specifies a step between cases, e.g.  a BY value
   of 2 causes the cases numbered FIRST + 1, FIRST + 3, FIRST + 5, and so on
   to be omitted.

   The caller gives up ownership of SUBREADER, transferring it into this
   function. */
struct casereader *
casereader_select (struct casereader *subreader,
                   casenumber first, casenumber last, casenumber by)
{
  if (by == 0)
    by = 1;

  casereader_advance (subreader, first);
  if (last >= first)
    casereader_truncate (subreader, (last - first) / by * by);

  if (by == 1)
    return casereader_rename (subreader);
  else
    {
      struct casereader_select *cs = xmalloc (sizeof *cs);
      cs->by = by;
      cs->i = by - 1;
      return casereader_create_filter_func (subreader,
                                            casereader_select_include,
                                            casereader_select_destroy,
                                            cs, NULL);
    }
}
