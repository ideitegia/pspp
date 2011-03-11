/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2011 Free Software Foundation, Inc.

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
#include <libpspp/compiler.h>
#include <libpspp/hash.h>
#include "group.h"
#include "group-proc.h"
#include <libpspp/str.h>
#include <data/variable.h>
#include <libpspp/misc.h>

#include "xalloc.h"

void
free_group (struct group_statistics *v, void *aux UNUSED)
{
  free(v);
}

static void
group_proc_dtor (struct variable *var)
{
  struct group_proc *group = var_detach_aux (var);

  hsh_destroy (group->group_hash);
  free (group);
}

struct group_proc *
group_proc_get (const struct variable *v)
{
  /* This is not ideal, obviously. */
  struct group_proc *group = var_get_aux (v);
  if (group == NULL)
    {
      group = xzalloc (sizeof *group);
      var_attach_aux (v, group, group_proc_dtor);
    }
  return group;
}
