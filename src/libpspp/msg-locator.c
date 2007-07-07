/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

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
#include <stdlib.h>
#include <libpspp/alloc.h>
#include "msg-locator.h"
#include <libpspp/message.h>
#include <libpspp/assertion.h>
#include "getl.h"

/* File locator stack. */
static const struct msg_locator **file_loc;

static int nfile_loc, mfile_loc;

void
msg_locator_done (void)
{
  free(file_loc);
  file_loc = NULL;
  nfile_loc = mfile_loc = 0;
}


/* File locator stack functions. */

/* Pushes F onto the stack of file locations. */
void
msg_push_msg_locator (const struct msg_locator *loc)
{
  if (nfile_loc >= mfile_loc)
    {
      if (mfile_loc == 0)
	mfile_loc = 8;
      else
	mfile_loc *= 2;

      file_loc = xnrealloc (file_loc, mfile_loc, sizeof *file_loc);
    }

  file_loc[nfile_loc++] = loc;
}

/* Pops F off the stack of file locations.
   Argument F is only used for verification that that is actually the
   item on top of the stack. */
void
msg_pop_msg_locator (const struct msg_locator *loc)
{
  assert (nfile_loc >= 0 && file_loc[nfile_loc - 1] == loc);
  nfile_loc--;
}

/* Puts the current file and line number into LOC, or NULL and -1 if
   none. */
void
get_msg_location (const struct source_stream *ss, struct msg_locator *loc)
{
  if (nfile_loc)
    {
      *loc = *file_loc[nfile_loc - 1];
    }
  else
    {
      loc->file_name = getl_source_name (ss);
      loc->line_number = getl_source_location (ss);
    }
}
