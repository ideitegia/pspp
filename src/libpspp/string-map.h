/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2011 Free Software Foundation, Inc.

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

#ifndef LIBPSPP_STRING_MAP_H
#define LIBPSPP_STRING_MAP_H

/* Map from a unique string key to a string value.

   This is a convenient wrapper around a "struct hmap" for storing string
   key-value pairs. */

#include <stdbool.h>
#include "libpspp/hmap.h"

struct string_set;

/* A node within a string map. */
struct string_map_node
  {
    struct hmap_node hmap_node;
    char *key;
    char *value;
  };

/* Returns the string key within NODE.  The caller must not modify or free the
   returned string. */
static inline const char *
string_map_node_get_key (const struct string_map_node *node)
{
  return node->key;
}

/* Returns the string key within NODE.  The caller must not free the returned
   string. */
static inline const char *
string_map_node_get_value (const struct string_map_node *node)
{
  return node->value;
}

/* Returns the string key within NODE.  The caller must not free the returned
   string. */
static inline char *
string_map_node_get_value_rw (struct string_map_node *node)
{
  return node->value;
}

char *string_map_node_swap_value (struct string_map_node *,
                                  const char *new_value);
char *string_map_node_swap_value_nocopy (struct string_map_node *,
                                         char *new_value);
void string_map_node_set_value (struct string_map_node *, const char *value);
void string_map_node_set_value_nocopy (struct string_map_node *, char *value);
void string_map_node_destroy (struct string_map_node *);

/* Unordered map from unique string keys to string values. */
struct string_map
  {
    struct hmap hmap;
  };

/* Suitable for use as the initializer for a string_map named MAP.  Typical
   usage:
       struct string_map map = STRING_MAP_INITIALIZER (map);
   STRING_MAP_INITIALIZER is an alternative to calling string_map_init. */
#define STRING_MAP_INITIALIZER(MAP) { HMAP_INITIALIZER ((MAP).hmap) }

void string_map_init (struct string_map *);
void string_map_clone (struct string_map *, const struct string_map *);
void string_map_swap (struct string_map *, struct string_map *);
void string_map_destroy (struct string_map *);

static inline size_t string_map_count (const struct string_map *);
static inline bool string_map_is_empty (const struct string_map *);

bool string_map_contains (const struct string_map *, const char *);
const char *string_map_find (const struct string_map *, const char *);
struct string_map_node *string_map_find_node (const struct string_map *,
                                              const char *);
char *string_map_find_and_delete (struct string_map *, const char *key);

struct string_map_node *string_map_insert (struct string_map *,
                                           const char *key, const char *value);
struct string_map_node *string_map_insert_nocopy (struct string_map *,
                                                  char *key, char *value);
struct string_map_node *string_map_replace (struct string_map *,
                                           const char *key, const char *value);
struct string_map_node *string_map_replace_nocopy (struct string_map *,
                                                   char *key, char *value);
bool string_map_delete (struct string_map *, const char *);
void string_map_delete_node (struct string_map *, struct string_map_node *);
void string_map_delete_nofree (struct string_map *, struct string_map_node *);

void string_map_clear (struct string_map *);
void string_map_insert_map (struct string_map *, const struct string_map *);
void string_map_replace_map (struct string_map *, const struct string_map *);

void string_map_get_keys (const struct string_map *, struct string_set *);
void string_map_get_values (const struct string_map *, struct string_set *);

static inline struct string_map_node *string_map_first (
  const struct string_map *);
static inline struct string_map_node *string_map_next (
  const struct string_map *, const struct string_map_node *);

/* Macros for conveniently iterating through a string_map, e.g. to print all of
   the key-value pairs in "my_map":

   struct string_map_node *node;
   const char *key, *value;

   STRING_MAP_FOR_EACH_KEY_VALUE (key, value, node, &my_map)
     printf ("%s=%s\n", key, value);
   */
#define STRING_MAP_FOR_EACH_NODE(NODE, MAP)                     \
        for ((NODE) = string_map_first (MAP); (NODE) != NULL;   \
             (NODE) = string_map_next (MAP, NODE))
#define STRING_MAP_FOR_EACH_NODE_SAFE(NODE, NEXT, MAP)          \
        for ((NODE) = string_map_first (MAP);                   \
             ((NODE) != NULL                                    \
              && ((NEXT) = string_map_next (MAP, NODE), 1));    \
             (NODE) = (NEXT))
#define STRING_MAP_FOR_EACH_KEY(KEY, NODE, MAP)                 \
        for ((NODE) = string_map_first (MAP);                   \
             ((NODE) != NULL                                    \
              && ((KEY) = string_map_node_get_key (NODE), 1));  \
             (NODE) = string_map_next (MAP, NODE))
#define STRING_MAP_FOR_EACH_KEY_SAFE(KEY, NODE, NEXT, MAP)      \
        for ((NODE) = string_map_first (MAP);                   \
             ((NODE) != NULL                                    \
              && ((KEY) = string_map_node_get_key (NODE), 1)    \
              && ((NEXT) = string_map_next (MAP, NODE), 1));    \
             (NODE) = (NEXT))
#define STRING_MAP_FOR_EACH_VALUE(VALUE, NODE, MAP)     \
        for ((NODE) = string_map_first (MAP);           \
             ((NODE) != NULL                            \
              && ((VALUE) = (NODE)->value, 1));         \
             (NODE) = string_map_next (MAP, NODE))
#define STRING_MAP_FOR_EACH_VALUE_SAFE(VALUE, NODE, NEXT, MAP)  \
        for ((NODE) = string_map_first (MAP);                   \
             ((NODE) != NULL                                    \
              && ((VALUE) = (NODE)->value, 1)                   \
              && ((NEXT) = string_map_next (MAP, NODE), 1));    \
             (NODE) = (NEXT))
#define STRING_MAP_FOR_EACH_KEY_VALUE(KEY, VALUE, NODE, MAP)    \
        for ((NODE) = string_map_first (MAP);                   \
             ((NODE) != NULL                                    \
              && ((KEY) = string_map_node_get_key (NODE), 1)    \
              && ((VALUE) = (NODE)->value, 1));                 \
             (NODE) = string_map_next (MAP, NODE))
#define STRING_MAP_FOR_EACH_KEY_VALUE_SAFE(KEY, VALUE, NODE, NEXT, MAP) \
        for ((NODE) = string_map_first (MAP);                           \
             ((NODE) != NULL                                            \
              && ((KEY) = string_map_node_get_key (NODE), 1)            \
              && ((VALUE) = (NODE)->value, 1)                           \
              && ((NEXT) = string_map_next (MAP, NODE), 1));            \
             (NODE) = (NEXT))

/* Returns the number of key-value pairs currently in MAP. */
static inline size_t
string_map_count (const struct string_map *map)
{
  return hmap_count (&map->hmap);
}

/* Returns true if MAP currently contains no key-value pairs, false
   otherwise. */
static inline bool
string_map_is_empty (const struct string_map *map)
{
  return hmap_is_empty (&map->hmap);
}

/* Returns the first node in MAP, or a null pointer if MAP is empty.  See the
   hmap_first function for information about complexity (O(1) amortized) and
   ordering (arbitrary).

   The STRING_MAP_FOR_EACH family of macros provide convenient ways to iterate
   over all the nodes in a string map. */
static inline struct string_map_node *
string_map_first (const struct string_map *map)
{
  return HMAP_FIRST (struct string_map_node, hmap_node, &map->hmap);
}

/* Returns the next node in MAP following NODE, or a null pointer if NODE is
   the last node in MAP.  See the hmap_next function for information about
   complexity (O(1) amortized) and ordering (arbitrary).

   The STRING_MAP_FOR_EACH family of macros provide convenient ways to iterate
   over all the nodes in a string map. */
static inline struct string_map_node *
string_map_next (const struct string_map *map,
                 const struct string_map_node *node)
{
  return HMAP_NEXT (node, struct string_map_node, hmap_node, &map->hmap);
}

#endif /* libpspp/string-map.h */
