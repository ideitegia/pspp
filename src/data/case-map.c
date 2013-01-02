/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2009, 2011, 2013 Free Software Foundation, Inc.

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

#include "data/case-map.h"

#include <stdio.h>
#include <stdlib.h>

#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/dictionary.h"
#include "data/variable.h"
#include "data/case.h"
#include "libpspp/assertion.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmap.h"

#include "gl/xalloc.h"

/* A case map. */
struct case_map
  {
    struct caseproto *proto;   /* Prototype for output cases. */
    int *map;                  /* For each destination index, the
                                  corresponding source index. */
  };

static struct ccase *translate_case (struct ccase *, void *map_);
static bool destroy_case_map (void *map_);

/* Creates and returns an empty map that outputs cases matching
   PROTO. */
static struct case_map *
create_case_map (const struct caseproto *proto)
{
  size_t n_values = caseproto_get_n_widths (proto);
  struct case_map *map;
  size_t i;

  map = xmalloc (sizeof *map);
  map->proto = caseproto_ref (proto);
  map->map = xnmalloc (n_values, sizeof *map->map);
  for (i = 0; i < n_values; i++)
    map->map[i] = -1;

  return map;
}

/* Inserts into MAP a mapping of the value at index FROM in the
   source case to the value at index TO in the destination
   case. */
static void
insert_mapping (struct case_map *map, size_t from, size_t to)
{
  assert (to < caseproto_get_n_widths (map->proto));
  assert (map->map[to] == -1);
  map->map[to] = from;
}

/* Destroys case map MAP. */
void
case_map_destroy (struct case_map *map)
{
  if (map != NULL)
    {
      caseproto_unref (map->proto);
      free (map->map);
      free (map);
    }
}

/* If MAP is nonnull, returns a new case that is the result of
   applying case map MAP to SRC, and unrefs SRC.

   If MAP is null, returns SRC unchanged. */
struct ccase *
case_map_execute (const struct case_map *map, struct ccase *src)
{
  if (map != NULL)
    {
      size_t n_values = caseproto_get_n_widths (map->proto);
      struct ccase *dst;
      size_t dst_idx;

      dst = case_create (map->proto);
      for (dst_idx = 0; dst_idx < n_values; dst_idx++)
        {
          int src_idx = map->map[dst_idx];
          if (src_idx != -1)
	    value_copy (case_data_rw_idx (dst, dst_idx), case_data_idx (src, src_idx), caseproto_get_width (map->proto, dst_idx));
        }
      case_unref (src);
      return dst;
    }
  else
    return src;
}

/* Returns the prototype for output cases created by MAP.  The
   caller must not unref the returned case prototype. */
const struct caseproto *
case_map_get_proto (const struct case_map *map)
{
  return map->proto;
}

/* Creates and returns a new casereader whose cases are produced
   by reading from SUBREADER and executing the actions of MAP.  
   The casereader will have as many `union value's as MAP.  When
   the new casereader is destroyed, MAP will be destroyed too.

   After this function is called, SUBREADER must not ever again
   be referenced directly.  It will be destroyed automatically
   when the returned casereader is destroyed. */
struct casereader *
case_map_create_input_translator (struct case_map *map,
                                  struct casereader *subreader) 
{
    return casereader_create_translator (subreader,
                                         case_map_get_proto (map),
                                         translate_case,
                                         destroy_case_map,
                                         map);
}

/* Creates and returns a new casewriter.  Cases written to the
   new casewriter will be passed through MAP and written to
   SUBWRITER.  The casewriter will have as many `union value's as
   MAP.  When the new casewriter is destroyed, MAP will be
   destroyed too.

   After this function is called, SUBWRITER must not ever again
   be referenced directly.  It will be destroyed automatically
   when the returned casewriter is destroyed. */
struct casewriter *
case_map_create_output_translator (struct case_map *map,
                                   struct casewriter *subwriter) 
{
    return casewriter_create_translator (subwriter,
                                         case_map_get_proto (map),
                                         translate_case,
                                         destroy_case_map,
                                         map);
}

/* Casereader/casewriter translation callback. */
static struct ccase *
translate_case (struct ccase *input, void *map_)
{
  struct case_map *map = map_;
  return case_map_execute (map, input);
}

/* Casereader/casewriter destruction callback. */
static bool
destroy_case_map (void *map_)
{
  struct case_map *map = map_;
  case_map_destroy (map);
  return true;
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
  size_t n_vars = dict_get_var_cnt (d);
  struct caseproto *proto;
  struct case_map *map;
  size_t n_values;
  size_t i;

  /* Create the case mapping. */
  proto = dict_get_compacted_proto (d, exclude_classes);
  map = create_case_map (proto);
  caseproto_unref (proto);

  /* Add the values to the case mapping. */
  n_values = 0;
  for (i = 0; i < n_vars; i++)
    {
      struct variable *v = dict_get_var (d, i);
      if (!(exclude_classes & (1u << var_get_dict_class (v))))
        insert_mapping (map, var_get_case_index (v), n_values++);
    }

  return map;
}

struct stage_var
  {
    struct hmap_node hmap_node; /* In struct case_map_stage's 'stage_vars'. */
    const struct variable *var;
    int case_index;
  };

struct case_map_stage
  {
    const struct dictionary *dict;
    struct hmap stage_vars;
  };

/* Prepares and returns a "struct case_map_stage" for producing a case map for
   DICT.  Afterward, the caller may delete, reorder, or rename variables within
   DICT at will before using case_map_stage_get_case_map() to produce the case
   map.

   The caller must *not* add new variables to DICT. */
struct case_map_stage *
case_map_stage_create (const struct dictionary *dict)
{
  size_t n_vars = dict_get_var_cnt (dict);
  struct case_map_stage *stage;
  size_t i;

  stage = xmalloc (sizeof *stage);
  stage->dict = dict;
  hmap_init (&stage->stage_vars);

  for (i = 0; i < n_vars; i++)
    {
      const struct variable *var = dict_get_var (dict, i);
      struct stage_var *stage_var;

      stage_var = xmalloc (sizeof *stage_var);
      stage_var->var = var;
      stage_var->case_index = var_get_case_index (var);
      hmap_insert (&stage->stage_vars, &stage_var->hmap_node,
                   hash_pointer (var, 0));
    }

  return stage;
}

/* Destroys STAGE, which was created by case_map_stage_create(). */
void
case_map_stage_destroy (struct case_map_stage *stage)
{
  if (stage != NULL)
    {
      struct stage_var *stage_var, *next_stage_var;

      HMAP_FOR_EACH_SAFE (stage_var, next_stage_var,
                          struct stage_var, hmap_node, &stage->stage_vars)
        {
          hmap_delete (&stage->stage_vars, &stage_var->hmap_node);
          free (stage_var);
        }
      hmap_destroy (&stage->stage_vars);
      free (stage);
    }
}

static const struct stage_var *
case_map_stage_find_var (const struct case_map_stage *stage,
                         const struct variable *var)
{
  const struct stage_var *stage_var;

  HMAP_FOR_EACH_IN_BUCKET (stage_var, struct stage_var, hmap_node,
                           hash_pointer (var, 0), &stage->stage_vars)
    if (stage_var->var == var)
      return stage_var;

  /* If the following assertion is reached, it indicates a bug in the
     case_map_stage client: the client allowed a new variable to be added to
     the dictionary.  This is not allowed, because of the risk that the new
     varaible might have the same address as an old variable that has been
     deleted. */
  NOT_REACHED ();
}

/* Produces a case map from STAGE, which must have been previously created with
   case_map_stage_create().  The case map maps from the original case index of
   the variables in STAGE's dictionary to their current case indexes.

   Returns the new case map, or a null pointer if no mapping is required (that
   is, no data has changed position). */
struct case_map *
case_map_stage_get_case_map (const struct case_map_stage *stage)
{
  struct case_map *map;
  size_t n_vars = dict_get_var_cnt (stage->dict);
  size_t n_values;
  size_t i;
  bool identity_map = true;

  map = create_case_map (dict_get_proto (stage->dict));
  for (i = 0; i < n_vars; i++)
    {
      const struct variable *var = dict_get_var (stage->dict, i);
      const struct stage_var *stage_var = case_map_stage_find_var (stage, var);

      if (var_get_case_index (var) != stage_var->case_index)
        identity_map = false;

      insert_mapping (map, stage_var->case_index, var_get_case_index (var));
    }

  if (identity_map)
    {
      case_map_destroy (map);
      return NULL;
    }

  n_values = caseproto_get_n_widths (map->proto);
  while (n_values > 0 && caseproto_get_width (map->proto, n_values - 1) == -1)
    map->proto = caseproto_remove_widths (map->proto, --n_values, 1);

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

  map = create_case_map (dict_get_proto (new));
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *nv = dict_get_var (new, i);
      struct variable *ov = dict_lookup_var_assert (old, var_get_name (nv));
      assert (var_get_width (nv) == var_get_width (ov));
      insert_mapping (map, var_get_case_index (ov), var_get_case_index (nv));
    }
  return map;
}

/* Prints the mapping represented by case map CM to stdout, for
   debugging purposes. */
void
case_map_dump (const struct case_map *cm)
{
  int i;
  for (i = 0 ; i < caseproto_get_n_widths (cm->proto); ++i )
    printf ("%d -> %d\n", i, cm->map[i]);
}
