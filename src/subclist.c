/* subclist - lists for PSPP subcommands

Copyright (C) 2004 Free Software Foundation, Inc.

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


#include "subclist.h"
#include <stdlib.h>

/* I call these objects `lists' but they are in fact simple dynamic arrays */

#define CHUNKSIZE 16

/* Create a  list */
void
subc_list_double_create(subc_list_double *l)
{
  l->data = (double *) malloc(CHUNKSIZE * sizeof (double));
  l->sz = CHUNKSIZE;
  l->n_data = 0;
}

/* Push a value onto the list */
void
subc_list_double_push(subc_list_double *l, double d)
{
  l->data[l->n_data++] = d;

  if (l->n_data >= l->sz ) 
    {
      l->sz += CHUNKSIZE;
      l->data = realloc(l->data, l->sz * sizeof(double));
    }

}

/* Return the number of items in the list */
int 
subc_list_double_count(const subc_list_double *l)
{
  return l->n_data;
}


/* Index into the list (array) */
double
subc_list_double_at(const subc_list_double *l, int idx)
{
  return l->data[idx];
}

/* Free up the list */
void
subc_list_double_destroy(subc_list_double *l)
{
  free(l->data);
}
