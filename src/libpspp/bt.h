/* PSPP - computes sample statistics.
   Copyright (C) 2007 Free Software Foundation, Inc.

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

#ifndef LIBPSPP_BT_H
#define LIBPSPP_BT_H 1

/* Balanced tree (BT) data structure.

   The client should not need to be aware of the form of
   balancing applied to the balanced tree, as its operation is
   fully encapsulated. */

#include <stddef.h>

/* Returns the data structure corresponding to the given NODE,
   assuming that NODE is embedded as the given MEMBER name in
   data type STRUCT. */
#define bt_data(NODE, STRUCT, MEMBER)                                  \
        ((STRUCT *) ((char *) (NODE) - offsetof (STRUCT, MEMBER)))

/* Node in a balanced binary tree. */
struct bt_node
  {
    struct bt_node *up;        /* Parent (NULL for root). */
    struct bt_node *down[2];   /* Left child, right child. */
  };

/* Compares nodes A and B, with the tree's AUX.
   Returns a strcmp-like result. */
typedef int bt_compare_func (const struct bt_node *a,
                             const struct bt_node *b,
                             const void *aux);

/* A balanced binary tree. */
struct bt
  {
    struct bt_node *root;       /* Tree's root, NULL if empty. */
    bt_compare_func *compare;   /* To compare nodes. */
    const void *aux;            /* Auxiliary data. */
    size_t size;                /* Current node count. */
    size_t max_size;            /* Max size since last complete rebalance. */
  };

void bt_init (struct bt *, bt_compare_func *, const void *aux);

struct bt_node *bt_insert (struct bt *, struct bt_node *);
void bt_delete (struct bt *, struct bt_node *);

struct bt_node *bt_find (const struct bt *, const struct bt_node *);
struct bt_node *bt_find_ge (const struct bt *, const struct bt_node *);
struct bt_node *bt_find_le (const struct bt *, const struct bt_node *);

struct bt_node *bt_first (const struct bt *);
struct bt_node *bt_last (const struct bt *);
struct bt_node *bt_find (const struct bt *, const struct bt_node *);
struct bt_node *bt_next (const struct bt *, const struct bt_node *);
struct bt_node *bt_prev (const struct bt *, const struct bt_node *);

struct bt_node *bt_changed (struct bt *, struct bt_node *);
void bt_moved (struct bt *, struct bt_node *);

/* Returns the number of nodes currently in BT. */
static inline size_t bt_count (const struct bt *bt)
{
  return bt->size;
}

#endif /* libpspp/bt.h */
