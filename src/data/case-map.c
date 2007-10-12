/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007 Free Software Foundation, Inc.

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

#include <data/case-map.h>

#include <stdio.h>
#include <stdlib.h>

#include <data/dictionary.h>
#include <data/variable.h>
#include <data/case.h>
#include <libpspp/assertion.h>

#include "xalloc.h"

/* A case map. */
struct case_map
  {
    size_t value_cnt;   /* Number of values in map. */
    int *map;           /* For each destination index, the
                           corresponding source index. */
  };

/* Creates and returns an empty map. */
static struct case_map *
create_case_map (size_t n)
{
  struct case_map *map;
  size_t i;

  map = xmalloc (sizeof *map);
  map->value_cnt = n;
  map->map = xnmalloc (n, sizeof *map->map);
  for (i = 0; i < map->value_cnt; i++)
    map->map[i] = -1;

  return map;
}

/* Inserts into MAP a mapping of the CNT values starting at FROM
   to the CNT values starting at TO. */
static void
insert_mapping (struct case_map *map, size_t from, size_t to, size_t cnt)
{
  size_t i;

  assert (to + cnt <= map->value_cnt);
  for (i = 0; i < cnt; i++)
    {
      assert (map->map[to + i] == -1);
      map->map[to + i] = from + i;
    }
}

/* Destroys case map MAP. */
void
case_map_destroy (struct case_map *map)
{
  if (map != NULL)
    {
      free (map->map);
      free (map);
    }
}

/* Maps from SRC to DST, applying case map MAP. */
void
case_map_execute (const struct case_map *map,
                  const struct ccase *src, struct ccase *dst)
{
  size_t dst_idx;

  case_create (dst, map->value_cnt);
  for (dst_idx = 0; dst_idx < map->value_cnt; dst_idx++)
    {
      int src_idx = map->map[dst_idx];
      if (src_idx != -1)
        *case_data_rw_idx (dst, dst_idx) = *case_data_idx (src, src_idx);
    }
}

/* Returns the number of `union value's in cases created by
   MAP. */
size_t
case_map_get_value_cnt (const struct case_map *map)
{
  return map->value_cnt;
}

/* Creates and returns a case_map that can be used to compact
   cases for dictionary D.

   Compacting a case eliminates "holes" between values and after
   the last value.  (Holes are created by deleting variables.)

   All variables are compacted if EXCLUDE_CLASSES is 0, or it may
   contain one or more of (1u << DC_ORDINARY), (1u << DC_SYSTEM),
   or (1u << DC_SCRATCH) to cause the corresponding type of
   variable to be deleted during compaction. */
struct case_map *
case_map_to_compact_dict (const struct dictionary *d,
                          unsigned int exclude_classes)
{
  size_t var_cnt;
  struct case_map *map;
  size_t value_idx;
  size_t i;

  assert ((exclude_classes & ~((1u << DC_ORDINARY)
                               | (1u << DC_SYSTEM)
                               | (1u << DC_SCRATCH))) == 0);

  map = create_case_map (dict_count_values (d, exclude_classes));
  var_cnt = dict_get_var_cnt (d);
  value_idx = 0;
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *v = dict_get_var (d, i);
      enum dict_class class = dict_class_from_id (var_get_name (v));

      if (!(exclude_classes & (1u << class)))
        {
          size_t value_cnt = var_get_value_cnt (v);
          insert_mapping (map, var_get_case_index (v), value_idx, value_cnt);
          value_idx += value_cnt;
        }
    }
  assert (value_idx == map->value_cnt);

  return map;
}

/* Prepares dictionary D for producing a case map.  Afterward,
   the caller may delete, reorder, or rename variables within D
   at will before using case_map_from_dict() to produce the case
   map.

   Uses D's aux members, which must otherwise not be in use. */
void
case_map_prepare_dict (const struct dictionary *d)
{
  size_t var_cnt = dict_get_var_cnt (d);
  size_t i;

  for (i = 0; i < var_cnt; i++)
    {
      struct variable *v = dict_get_var (d, i);
      int *src_fv = xmalloc (sizeof *src_fv);
      *src_fv = var_get_case_index (v);
      var_attach_aux (v, src_fv, var_dtor_free);
    }
}

/* Produces a case map from dictionary D, which must have been
   previously prepared with case_map_prepare_dict().

   Does not retain any reference to D, and clears the aux members
   set up by case_map_prepare_dict().

   Returns the new case map, or a null pointer if no mapping is
   required (that is, no data has changed position). */
struct case_map *
case_map_from_dict (const struct dictionary *d)
{
  struct case_map *map;
  size_t var_cnt = dict_get_var_cnt (d);
  size_t i;
  bool identity_map = true;

  map = create_case_map (dict_get_next_value_idx (d));
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *v = dict_get_var (d, i);
      size_t value_cnt = var_get_value_cnt (v);
      int *src_fv = (int *) var_detach_aux (v);

      if (var_get_case_index (v) != *src_fv)
        identity_map = false;

      insert_mapping (map, *src_fv, var_get_case_index (v), value_cnt);

      free (src_fv);
    }

  if (identity_map)
    {
      case_map_destroy (map);
      return NULL;
    }

  while (map->value_cnt > 0 && map->map[map->value_cnt - 1] == -1)
    map->value_cnt--;

  return map;
}

/* Creates and returns a case map for mapping variables in OLD to
   variables in NEW based on their name.  For every variable in
   NEW, there must be a variable in OLD with the same name, type,
   and width. */
struct case_map *
case_map_by_name (const struct dictionary *old,
                  const struct dictionary *new)
{
  struct case_map *map;
  size_t var_cnt = dict_get_var_cnt (new);
  size_t i;

  map = create_case_map (dict_get_next_value_idx (new));
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *nv = dict_get_var (new, i);
      struct variable *ov = dict_lookup_var_assert (old, var_get_name (nv));
      assert (var_get_width (nv) == var_get_width (ov));
      insert_mapping (map, var_get_case_index (ov), var_get_case_index (nv),
                      var_get_value_cnt (ov));
    }
  return map;
}

/* Prints the mapping represented by case map CM to stdout, for
   debugging purposes. */
void
case_map_dump (const struct case_map *cm)
{
  int i;
  for (i = 0 ; i < cm->value_cnt; ++i )
    printf ("%d -> %d\n", i, cm->map[i]);
}
