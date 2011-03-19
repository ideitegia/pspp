/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2011 Free Software Foundation, Inc.

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

#ifndef LIBPSPP_RANGE_MAP_H
#define LIBPSPP_RANGE_MAP_H

/* Range map data structure, implemented as a balanced binary
   tree.

   This is a dictionary data structure that maps from contiguous
   ranges of "unsigned long int" keys to arbitrary data
   values.

   The implementation is not robust against ranges that include
   ULONG_MAX in their ranges.  Such ranges are difficult to deal
   with in C anyhow, because a range that includes 0 through
   ULONG_MAX inclusive has a width of ULONG_MAX + 1, which equals
   0. */

#include <stdbool.h>

#include "libpspp/bt.h"
#include "libpspp/cast.h"

/* Returns the data structure corresponding to the given NODE,
   assuming that NODE is embedded as the given MEMBER name in
   data type STRUCT. */
#define range_map_data(NODE, STRUCT, MEMBER)                            \
        (CHECK_POINTER_HAS_TYPE (NODE, struct range_map_node *),        \
         UP_CAST (NODE, STRUCT, MEMBER))

/* A range map node, to be embedded in the data value. */
struct range_map_node
  {
    struct bt_node bt_node;     /* Balanced tree node. */
    unsigned long int start;    /* Start of range. */
    unsigned long int end;      /* End of range, plus one. */
  };

/* Returns the start of the range in the given NODE. */
static inline unsigned long int
range_map_node_get_start (const struct range_map_node *node)
{
  return node->start;
}

/* Returns the end of the range in the given NODE, plus one. */
static inline unsigned long int
range_map_node_get_end (const struct range_map_node *node)
{
  return node->end;
}

/* Returns the width of the range in the given NODE. */
static inline unsigned long int
range_map_node_get_width (const struct range_map_node *node)
{
  return node->end - node->start;
}

/* Range map. */
struct range_map
  {
    struct bt bt;
  };

void range_map_init (struct range_map *);

bool range_map_is_empty (const struct range_map *);

void range_map_insert (struct range_map *,
                       unsigned long int start, unsigned long int width,
                       struct range_map_node *new);
void range_map_delete (struct range_map *, struct range_map_node *);

struct range_map_node *range_map_lookup (const struct range_map *,
                                         unsigned long int position);
struct range_map_node *range_map_first (const struct range_map *);
struct range_map_node *range_map_next (const struct range_map *,
                                       const struct range_map_node *);

#endif /* libpspp/range-map.h */
