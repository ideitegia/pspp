/* PSPP - computes sample statistics.
   Copyright (C) 2004 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include "workspace.h"
#include <assert.h>
#include <stdlib.h>
#include "alloc.h"
#include "settings.h"

static size_t workspace_used;

/* Returns a block SIZE bytes in size, charging it against the
   workspace limit.  Returns a null pointer if the workspace
   limit is reached. */
void *
workspace_malloc (size_t size) 
{
  if (workspace_used + size > get_max_workspace ())
    return NULL;

  workspace_used += size;
  return xmalloc (size);
}

/* Frees BLOCK, which is SIZE bytes long, and credits it toward
   the workspace limit. */
void
workspace_free (void *block, size_t size) 
{
  if (block != NULL) 
    {
      assert (workspace_used >= size);
      free (block);
      workspace_used -= size;
    }
}
