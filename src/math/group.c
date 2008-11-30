/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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

/* Return -1 if the id of a is less than b; +1 if greater than and
   0 if equal */
int
compare_group (const void *a_,
		 const void *b_,
		 const void *var)
{
  const struct group_statistics *a = a_;
  const struct group_statistics *b = b_;
  return compare_values_short (&a->id, &b->id, var);
}



unsigned int
hash_group (const void *g_, const void *var)
{
  unsigned id_hash;
  const struct group_statistics *g = g_;;

  id_hash = hash_value_short (&g->id, var);

  return id_hash;
}


void
free_group (struct group_statistics *v, void *aux UNUSED)
{
  free(v);
}


struct group_proc *
group_proc_get (const struct variable *v)
{
  /* This is not ideal, obviously. */
  struct group_proc *group = var_get_aux (v);
  if (group == NULL)
    {
      group = xmalloc (sizeof (struct group_proc));
      var_attach_aux (v, group, var_dtor_free);
    }
  return group;
}
