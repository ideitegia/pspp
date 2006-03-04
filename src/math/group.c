/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by John Darrington <john@darrington.wattle.id.au>

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
#include <stdlib.h>
#include "alloc.h"
#include "compiler.h"
#include "hash.h"
#include "group.h"
#include "group-proc.h"
#include "str.h"
#include "variable.h"
#include "misc.h"


/* Return -1 if the id of a is less than b; +1 if greater than and 
   0 if equal */
int 
compare_group(const struct group_statistics *a, 
		 const struct group_statistics *b, 
		 int width)
{
  return compare_values(&a->id, &b->id, width);
}



unsigned 
hash_group(const struct group_statistics *g, int width)
{
  unsigned id_hash;

  id_hash = hash_value(&g->id, width);

  return id_hash;
}


void  
free_group(struct group_statistics *v, void *aux UNUSED)
{
  free(v);
}


struct group_proc *
group_proc_get (struct variable *v)
{
  /* This is not ideal, obviously. */
  if (v->aux == NULL) 
    var_attach_aux (v, xmalloc (sizeof (struct group_proc)), var_dtor_free);
  return v->aux;
}
