/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011 Free Software Foundation, Inc.

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

#ifndef LIBPSPP_STRINGI_MAP_H
#define LIBPSPP_STRINGI_MAP_H

/* Map from a unique, case-insensitve string key to a string value.

   This is a convenient wrapper around a "struct hmap" for storing string
   key-value pairs. */

#include <stdbool.h>
#include "libpspp/hmap.h"

struct string_set;
struct stringi_set;

/* A node within a string map. */
struct stringi_map_node
  {
    struct hmap_node hmap_node;
    char *key;
    char *value;
  };

/* Returns the string key within NODE.  The caller must not modify or free the
   returned string. */
static inline const char *
stringi_map_node_get_key (const struct stringi_map_node *node)
{
  return node->key;
}

/* Returns the string key within NODE.  The caller must not free the returned
   string. */
static inline const char *
stringi_map_node_get_value (const struct stringi_map_node *node)
{
  return node->value;
}

/* Returns the string key within NODE.  The caller must not free the returned
   string. */
static inline char *
stringi_map_node_get_value_rw (struct stringi_map_node *node)
{
  return node->value;
}

char *stringi_map_node_swap_value (struct stringi_map_node *,
                                   const char *new_value);
char *stringi_map_node_swap_value_nocopy (struct stringi_map_node *,
                                          char *new_value);
void stringi_map_node_set_value (struct stringi_map_node *, const char *value);
void stringi_map_node_set_value_nocopy (struct stringi_map_node *, char *value);
void stringi_map_node_destroy (struct stringi_map_node *);

/* Unordered map from unique, case-insensitive string keys to string values. */
struct stringi_map
  {
    struct hmap hmap;
  };

/* Suitable for use as the initializer for a stringi_map named MAP.  Typical
   usage:
   struct stringi_map map = STRINGI_MAP_INITIALIZER (map);
   STRINGI_MAP_INITIALIZER is an alternative to calling stringi_map_init. */
#define STRINGI_MAP_INITIALIZER(MAP) { HMAP_INITIALIZER ((MAP).hmap) }

void stringi_map_init (struct stringi_map *);
void stringi_map_clone (struct stringi_map *, const struct stringi_map *);
void stringi_map_swap (struct stringi_map *, struct stringi_map *);
void stringi_map_destroy (struct stringi_map *);

static inline size_t stringi_map_count (const struct stringi_map *);
static inline bool stringi_map_is_empty (const struct stringi_map *);

bool stringi_map_contains (const struct stringi_map *, const char *);
const char *stringi_map_find (const struct stringi_map *, const char *);
struct stringi_map_node *stringi_map_find_node (const struct stringi_map *,
                                                const char *);
char *stringi_map_find_and_delete (struct stringi_map *, const char *key);

struct stringi_map_node *stringi_map_insert (struct stringi_map *,
                                             const char *key,
                                             const char *value);
struct stringi_map_node *stringi_map_insert_nocopy (struct stringi_map *,
                                                    char *key, char *value);
struct stringi_map_node *stringi_map_replace (struct stringi_map *,
                                              const char *key,
                                              const char *value);
struct stringi_map_node *stringi_map_replace_nocopy (struct stringi_map *,
                                                     char *key, char *value);
bool stringi_map_delete (struct stringi_map *, const char *);
void stringi_map_delete_node (struct stringi_map *, struct stringi_map_node *);
void stringi_map_delete_nofree (struct stringi_map *,
                                struct stringi_map_node *);

void stringi_map_clear (struct stringi_map *);
void stringi_map_insert_map (struct stringi_map *, const struct stringi_map *);
void stringi_map_replace_map (struct stringi_map *,
                              const struct stringi_map *);

void stringi_map_get_keys (const struct stringi_map *, struct stringi_set *);
void stringi_map_get_values (const struct stringi_map *, struct string_set *);

static inline struct stringi_map_node *stringi_map_first (
  const struct stringi_map *);
static inline struct stringi_map_node *stringi_map_next (
  const struct stringi_map *, const struct stringi_map_node *);

/* Macros for conveniently iterating through a stringi_map, e.g. to print all
   of the key-value pairs in "my_map":

   struct stringi_map_node *node;
   const char *key, *value;

   STRINGI_MAP_FOR_EACH_KEY_VALUE (key, value, node, &my_map)
   printf ("%s=%s\n", key, value);
*/
#define STRINGI_MAP_FOR_EACH_NODE(NODE, MAP)                    \
  for ((NODE) = stringi_map_first (MAP); (NODE) != NULL;        \
       (NODE) = stringi_map_next (MAP, NODE))
#define STRINGI_MAP_FOR_EACH_NODE_SAFE(NODE, NEXT, MAP) \
  for ((NODE) = stringi_map_first (MAP);                \
       ((NODE) != NULL                                  \
        && ((NEXT) = stringi_map_next (MAP, NODE), 1)); \
       (NODE) = (NEXT))
#define STRINGI_MAP_FOR_EACH_KEY(KEY, NODE, MAP)                \
  for ((NODE) = stringi_map_first (MAP);                        \
       ((NODE) != NULL                                          \
        && ((KEY) = stringi_map_node_get_key (NODE), 1));       \
       (NODE) = stringi_map_next (MAP, NODE))
#define STRINGI_MAP_FOR_EACH_KEY_SAFE(KEY, NODE, NEXT, MAP)     \
  for ((NODE) = stringi_map_first (MAP);                        \
       ((NODE) != NULL                                          \
        && ((KEY) = stringi_map_node_get_key (NODE), 1)         \
        && ((NEXT) = stringi_map_next (MAP, NODE), 1));         \
       (NODE) = (NEXT))
#define STRINGI_MAP_FOR_EACH_VALUE(VALUE, NODE, MAP)    \
  for ((NODE) = stringi_map_first (MAP);                \
       ((NODE) != NULL                                  \
        && ((VALUE) = (NODE)->value, 1));               \
       (NODE) = stringi_map_next (MAP, NODE))
#define STRINGI_MAP_FOR_EACH_VALUE_SAFE(VALUE, NODE, NEXT, MAP) \
  for ((NODE) = stringi_map_first (MAP);                        \
       ((NODE) != NULL                                          \
        && ((VALUE) = (NODE)->value, 1)                         \
        && ((NEXT) = stringi_map_next (MAP, NODE), 1));         \
       (NODE) = (NEXT))
#define STRINGI_MAP_FOR_EACH_KEY_VALUE(KEY, VALUE, NODE, MAP)   \
  for ((NODE) = stringi_map_first (MAP);                        \
       ((NODE) != NULL                                          \
        && ((KEY) = stringi_map_node_get_key (NODE), 1)         \
        && ((VALUE) = (NODE)->value, 1));                       \
       (NODE) = stringi_map_next (MAP, NODE))
#define STRINGI_MAP_FOR_EACH_KEY_VALUE_SAFE(KEY, VALUE, NODE, NEXT, MAP) \
  for ((NODE) = stringi_map_first (MAP);                                \
       ((NODE) != NULL                                                  \
        && ((KEY) = stringi_map_node_get_key (NODE), 1)                 \
        && ((VALUE) = (NODE)->value, 1)                                 \
        && ((NEXT) = stringi_map_next (MAP, NODE), 1));                 \
       (NODE) = (NEXT))

/* Returns the number of key-value pairs currently in MAP. */
static inline size_t
stringi_map_count (const struct stringi_map *map)
{
  return hmap_count (&map->hmap);
}

/* Returns true if MAP currently contains no key-value pairs, false
   otherwise. */
static inline bool
stringi_map_is_empty (const struct stringi_map *map)
{
  return hmap_is_empty (&map->hmap);
}

/* Returns the first node in MAP, or a null pointer if MAP is empty.  See the
   hmap_first function for information about complexity (O(1) amortized) and
   ordering (arbitrary).

   The STRINGI_MAP_FOR_EACH family of macros provide convenient ways to iterate
   over all the nodes in a string map. */
static inline struct stringi_map_node *
stringi_map_first (const struct stringi_map *map)
{
  return HMAP_FIRST (struct stringi_map_node, hmap_node, &map->hmap);
}

/* Returns the next node in MAP following NODE, or a null pointer if NODE is
   the last node in MAP.  See the hmap_next function for information about
   complexity (O(1) amortized) and ordering (arbitrary).

   The STRINGI_MAP_FOR_EACH family of macros provide convenient ways to iterate
   over all the nodes in a string map. */
static inline struct stringi_map_node *
stringi_map_next (const struct stringi_map *map,
                  const struct stringi_map_node *node)
{
  return HMAP_NEXT (node, struct stringi_map_node, hmap_node, &map->hmap);
}

#endif /* libpspp/string-map.h */
