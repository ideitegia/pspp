/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006, 2007 Free Software Foundation, Inc.

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

#include <language/data-io/case-map.h>

#include <stdlib.h>

#include <data/case.h>
#include <data/dictionary.h>
#include <data/variable.h>
#include <libpspp/assertion.h>

#include "xalloc.h"

/* A case map. */
struct case_map
  {
    size_t value_cnt;   /* Number of values in map. */
    int *map;           /* For each destination index, the
                           corresponding source index. */
  };

/* Prepares dictionary D for producing a case map.  Afterward,
   the caller may delete, reorder, or rename variables within D
   at will before using finish_case_map() to produce the case
   map.

   Uses D's aux members, which must otherwise not be in use. */
void
case_map_prepare (struct dictionary *d) 
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
   previously prepared with start_case_map().

   Does not retain any reference to D, and clears the aux members
   set up by start_case_map().

   Returns the new case map, or a null pointer if no mapping is
   required (that is, no data has changed position). */
struct case_map *
case_map_finish (struct dictionary *d) 
{
  struct case_map *map;
  size_t var_cnt = dict_get_var_cnt (d);
  size_t i;
  int identity_map;

  map = xmalloc (sizeof *map);
  map->value_cnt = dict_get_next_value_idx (d);
  map->map = xnmalloc (map->value_cnt, sizeof *map->map);
  for (i = 0; i < map->value_cnt; i++)
    map->map[i] = -1;

  identity_map = 1;
  for (i = 0; i < var_cnt; i++) 
    {
      struct variable *v = dict_get_var (d, i);
      size_t value_cnt = var_get_value_cnt (v);
      int *src_fv = var_detach_aux (v);
      size_t idx;

      if (var_get_case_index (v) != *src_fv)
        identity_map = 0;
      
      for (idx = 0; idx < value_cnt; idx++)
        {
          int src_idx = *src_fv + idx;
          int dst_idx = var_get_case_index (v) + idx;
          
          assert (map->map[dst_idx] == -1);
          map->map[dst_idx] = src_idx;
        }
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

/* Maps from SRC to DST, applying case map MAP. */
void
case_map_execute (const struct case_map *map,
                  const struct ccase *src, struct ccase *dst) 
{
  size_t dst_idx;

  for (dst_idx = 0; dst_idx < map->value_cnt; dst_idx++)
    {
      int src_idx = map->map[dst_idx];
      if (src_idx != -1)
        *case_data_rw_idx (dst, dst_idx) = *case_data_idx (src, src_idx);
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
