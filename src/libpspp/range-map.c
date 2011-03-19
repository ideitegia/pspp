/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2011 Free Software Foundation, Inc.

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

#include "libpspp/range-map.h"

#include "libpspp/assertion.h"
#include "libpspp/compiler.h"

static struct range_map_node *bt_to_range_map_node (const struct bt_node *);
static int compare_range_map_nodes (const struct bt_node *,
                                    const struct bt_node *,
                                    const void *aux);
static struct range_map_node *first_node (const struct range_map *);
static struct range_map_node *next_node (const struct range_map *,
                                         const struct range_map_node *);
static struct range_map_node *prev_node (const struct range_map *,
                                         const struct range_map_node *) UNUSED;

/* Initializes RM as an empty range map. */
void
range_map_init (struct range_map *rm)
{
  bt_init (&rm->bt, compare_range_map_nodes, NULL);
}

/* Returns true if RM contains no mappings,
   false if it contains at least one. */
bool
range_map_is_empty (const struct range_map *rm)
{
  return bt_count (&rm->bt) == 0;
}

/* Inserts node NEW into RM, covering the range beginning at
   START and ending at START + WIDTH (exclusive).  WIDTH must be
   at least 1.  The new range must not overlap any existing range
   already in RM. */
void
range_map_insert (struct range_map *rm,
                  unsigned long int start, unsigned long int width,
                  struct range_map_node *new)
{
  unsigned long int end = start + width;
  struct range_map_node *dup;

  assert (width > 0);
  assert (end - 1 >= start);

  new->start = start;
  new->end = end;
  dup = bt_to_range_map_node (bt_insert (&rm->bt, &new->bt_node));

  /* Make sure NEW doesn't overlap any other node. */
  assert (dup == NULL);
  assert (prev_node (rm, new) == NULL || start >= prev_node (rm, new)->end);
  assert (next_node (rm, new) == NULL || next_node (rm, new)->start >= end);
}

/* Deletes NODE from RM. */
void
range_map_delete (struct range_map *rm, struct range_map_node *node)
{
  bt_delete (&rm->bt, &node->bt_node);
}

/* Returns the node in RM that contains the given POSITION, or a
   null pointer if no node contains POSITION. */
struct range_map_node *
range_map_lookup (const struct range_map *rm,
                  unsigned long int position)
{
  struct range_map_node tmp, *node;

  tmp.start = position;
  node = bt_to_range_map_node (bt_find_le (&rm->bt, &tmp.bt_node));
  return node != NULL && position < node->end ? node : NULL;
}

/* Returns the first node in RM, or a null pointer if RM is
   empty. */
struct range_map_node *
range_map_first (const struct range_map *rm)
{
  return first_node (rm);
}

/* If NODE is nonnull, returns the node in RM following NODE, or
   a null pointer if NODE is the last node in RM.
   If NODE is null, behaves like range_map_first. */
struct range_map_node *
range_map_next (const struct range_map *rm,
                const struct range_map_node *node)
{
  return node != NULL ? next_node (rm, node) : first_node (rm);
}

/* Returns the range_map_node containing BT_NODE. */
static struct range_map_node *
bt_to_range_map_node (const struct bt_node *bt_node)
{
  return (bt_node != NULL
          ? bt_data (bt_node, struct range_map_node, bt_node)
          : NULL);
}

/* Compares range map nodes A and B and returns a strcmp()-type
   result. */
static int
compare_range_map_nodes (const struct bt_node *a_,
                         const struct bt_node *b_,
                         const void *aux UNUSED)
{
  const struct range_map_node *a = bt_to_range_map_node (a_);
  const struct range_map_node *b = bt_to_range_map_node (b_);
  return a->start < b->start ? -1 : a->start > b->start;
}

/* Returns the first range map node in RM, or a null pointer if
   RM is empty. */
static struct range_map_node *
first_node (const struct range_map *rm)
{
  return bt_to_range_map_node (bt_first (&rm->bt));
}

/* Returns the next range map node in RM following NODE, or a
   null pointer if NODE is the last node in RM. */
static struct range_map_node *
next_node (const struct range_map *rm, const struct range_map_node *node)
{
  return bt_to_range_map_node (bt_next (&rm->bt, &node->bt_node));
}

/* Returns the previous range map node in RM preceding NODE, or a
   null pointer if NODE is the first node in RM. */
static struct range_map_node *
prev_node (const struct range_map *rm, const struct range_map_node *node)
{
  return bt_to_range_map_node (bt_prev (&rm->bt, &node->bt_node));
}

