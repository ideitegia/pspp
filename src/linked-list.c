/* PSPP - computes sample statistics.
   Copyright (C) 2005 Free Software Foundation, Inc.
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include <assert.h>
#include <stdlib.h>

#include "alloc.h"
#include "linked-list.h"

/* Iteration */

/* Return the first element in LL */
void *
ll_first (const struct linked_list *ll, struct ll_iterator *li)
{
  assert(ll); 

  li->p = ll->head;

  return ll->head->entry;
}

/* Return the next element in LL iterated by LI */
void *
ll_next (const struct linked_list *ll, struct ll_iterator *li)
{
  assert( ll ) ;

  li->p = li->p->next;

  if ( ! li->p ) 
    return 0;

  return li->p->entry;
}


/* Create a linked list.
   Elements will be freed using F and AUX
*/
struct linked_list *
ll_create( ll_free_func *f , void *aux)
{
  struct linked_list *ll = xmalloc ( sizeof(struct linked_list) ) ;

  ll->head = 0;
  ll->free = f;
  ll->aux  = aux;

  return ll;
}


/* Destroy a linked list */
void
ll_destroy(struct linked_list *ll)
{
  struct node *n = ll->head;

  while (n)
    {
      struct node *nn = n->next;
      if ( ll->free ) 
	ll->free(n->entry, ll->aux);
      free (n);
      n = nn;
    }

  free (ll);
}


/* Push a an element ENTRY onto the list LL */
void
ll_push_front(struct linked_list *ll, void *entry)
{
  struct node *n ; 
  assert (ll);

  n = xmalloc (sizeof(struct node) );
  n->next = ll->head;
  n->entry = entry;
  ll->head = n;
}

