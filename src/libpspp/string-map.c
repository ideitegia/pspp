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

#include <config.h>

#include "libpspp/string-map.h"

#include <stdlib.h>
#include <string.h>

#include "libpspp/hash-functions.h"
#include "libpspp/string-set.h"

#include "gl/xalloc.h"

static struct string_map_node *string_map_find_node__ (
  const struct string_map *, const char *key, unsigned int hash);
static bool string_map_delete__ (struct string_map *, const char *key,
                                 unsigned int hash);
static struct string_map_node *string_map_insert__ (struct string_map *,
                                                    char *key, char *value,
                                                    unsigned int hash);

/* Sets NODE's value to a copy of NEW_VALUE and returns the node's previous
   value.  The caller is responsible for freeing the returned string (with
   free()). */
char *
string_map_node_swap_value (struct string_map_node *node,
                            const char *new_value)
{
  return string_map_node_swap_value_nocopy (node, xstrdup (new_value));
}

/* Sets NODE's value to NEW_VALUE, which must be a malloc()'d string,
   transferring ownership of NEW_VALUE to the node.  Returns the node's
   previous value, which the caller is responsible for freeing (with
   free()). */
char *
string_map_node_swap_value_nocopy (struct string_map_node *node,
                                   char *new_value)
{
  char *old_value = node->value;
  node->value = new_value;
  return old_value;
}

/* Replaces NODE's value by a copy of VALUE. */
void
string_map_node_set_value (struct string_map_node *node, const char *value)
{
  string_map_node_set_value_nocopy (node, xstrdup (value));
}

/* Replaces NODE's value by VALUE, which must be a malloc()'d string,
   transferring ownership of VALUE to the node.. */
void
string_map_node_set_value_nocopy (struct string_map_node *node, char *value)
{
  free (node->value);
  node->value = value;
}

/* Frees NODE and and its key and value.  Ordinarily nodes are owned by
   string_maps, but this function should only be used by a caller that owns
   NODE, such as one that has called string_map_delete_nofree() for the
   node. */
void
string_map_node_destroy (struct string_map_node *node)
{
  free (node->key);
  free (node->value);
  free (node);
}

/* Initializes MAP as an initially empty string map. */
void
string_map_init (struct string_map *map)
{
  hmap_init (&map->hmap);
}

/* Initializes MAP as a new string map that initially contains the same pairs
   as OLD. */
void
string_map_clone (struct string_map *map, const struct string_map *old)
{
  const struct string_map_node *node;
  const char *key, *value;

  string_map_init (map);
  hmap_reserve (&map->hmap, string_map_count (old));
  STRING_MAP_FOR_EACH_KEY_VALUE (key, value, node, old)
    string_map_insert__ (map, xstrdup (key), xstrdup (value),
                         node->hmap_node.hash);
}

/* Exchanges the contents of string maps A and B. */
void
string_map_swap (struct string_map *a, struct string_map *b)
{
  hmap_swap (&a->hmap, &b->hmap);
}

/* Frees MAP and its nodes and key-value pairs. */
void
string_map_destroy (struct string_map *map)
{
  if (map != NULL)
    {
      string_map_clear (map);
      hmap_destroy (&map->hmap);
    }
}

/* Returns true if MAP contains KEY as a key, otherwise false. */
bool
string_map_contains (const struct string_map *map, const char *key)
{
  return string_map_find_node (map, key) != NULL;
}

/* If MAP contains KEY as a key, returns the corresponding value.  Otherwise,
   returns a null pointer. */
const char *
string_map_find (const struct string_map *map, const char *key)
{
  const struct string_map_node *node = string_map_find_node (map, key);
  return node != NULL ? node->value : NULL;
}

/* If MAP contains KEY as a key, returns the corresponding node.  Otherwise,
   returns a null pointer. */
struct string_map_node *
string_map_find_node (const struct string_map *map, const char *key)
{
  return string_map_find_node__ (map, key, hash_string (key, 0));
}

/* If MAP contains KEY as a key, deletes that key's node and returns its value,
   which the caller is responsible for freeing (using free()).  Otherwise,
   returns a null pointer. */
char *
string_map_find_and_delete (struct string_map *map, const char *key)
{
  struct string_map_node *node = string_map_find_node (map, key);
  char *value = NULL;
  if (node != NULL)
    {
      value = node->value;
      node->value = NULL;
      string_map_delete_node (map, node);
    }
  return value;
}

/* If MAP does not contain KEY as a key, inserts a new node containing copies
   of KEY and VALUE and returns the new node.  Otherwise, returns the existing
   node that contains KEY. */
struct string_map_node *
string_map_insert (struct string_map *map, const char *key, const char *value)
{
  unsigned int hash = hash_string (key, 0);
  struct string_map_node *node = string_map_find_node__ (map, key, hash);
  if (node == NULL)
    node = string_map_insert__ (map, xstrdup (key), xstrdup (value), hash);
  return node;
}

/* If MAP does not contain KEY as a key, inserts a new node containing KEY and
   VALUE and returns the new node.  Otherwise, returns the existing node that
   contains KEY.  Either way, ownership of KEY and VALUE is transferred to
   MAP. */
struct string_map_node *
string_map_insert_nocopy (struct string_map *map, char *key, char *value)
{
  unsigned int hash = hash_string (key, 0);
  struct string_map_node *node = string_map_find_node__ (map, key, hash);
  if (node == NULL)
    node = string_map_insert__ (map, key, value, hash);
  else
    {
      free (key);
      free (value);
    }
  return node;
}

/* If MAP does not contain KEY as a key, inserts a new node containing copies
   of KEY and VALUE.  Otherwise, replaces the existing node's value by a copy
   of VALUE.  Returns the node. */
struct string_map_node *
string_map_replace (struct string_map *map, const char *key, const char *value)
{
  unsigned int hash = hash_string (key, 0);
  struct string_map_node *node = string_map_find_node__ (map, key, hash);
  if (node == NULL)
    node = string_map_insert__ (map, xstrdup (key), xstrdup (value), hash);
  else
    string_map_node_set_value (node, value);
  return node;
}

/* If MAP does not contain KEY as a key, inserts a new node containing KEY and
   VALUE.  Otherwise, replaces the existing node's value by VALUE.  Either way,
   ownership of KEY and VALUE is transferred to MAP.  Returns the node. */
struct string_map_node *
string_map_replace_nocopy (struct string_map *map, char *key, char *value)
{
  unsigned int hash = hash_string (key, 0);
  struct string_map_node *node = string_map_find_node__ (map, key, hash);
  if (node == NULL)
    node = string_map_insert__ (map, key, value, hash);
  else
    {
      free (key);
      string_map_node_set_value_nocopy (node, value);
    }
  return node;
}

/* Searches MAP for a node with KEY as its key.  If found, deletes the node
   and its key and value and returns true.  Otherwise, returns false without
   modifying MAP. */
bool
string_map_delete (struct string_map *map, const char *key)
{
  return string_map_delete__ (map, key, hash_string (key, 0));
}

/* Deletes NODE from MAP and destroys the node and its key and value. */
void
string_map_delete_node (struct string_map *map, struct string_map_node *node)
{
  string_map_delete_nofree (map, node);
  string_map_node_destroy (node);
}

/* Deletes NODE from MAP.  Transfers ownership of NODE to the caller, which
   becomes responsible for destroying it. */
void
string_map_delete_nofree (struct string_map *map, struct string_map_node *node)
{
  hmap_delete (&map->hmap, &node->hmap_node);
}

/* Removes all nodes from MAP and frees them and their keys and values. */
void
string_map_clear (struct string_map *map)
{
  struct string_map_node *node, *next;

  STRING_MAP_FOR_EACH_NODE_SAFE (node, next, map)
    string_map_delete_node (map, node);
}

/* Inserts a copy of each of the nodes in SRC into DST.  When SRC and DST both
   have a particular key, the value in DST's node is left unchanged. */
void
string_map_insert_map (struct string_map *dst, const struct string_map *src)
{
  const struct string_map_node *node;

  STRING_MAP_FOR_EACH_NODE (node, src)
    {
      if (!string_map_find_node__ (dst, node->key, node->hmap_node.hash))
        string_map_insert__ (dst, xstrdup (node->key), xstrdup (node->value),
                             node->hmap_node.hash);
    }
}

/* Inserts a copy of each of the nodes in SRC into DST.  When SRC and DST both
   have a particular key, the value in DST's node is replaced by a copy of the
   value in SRC's node. */
void
string_map_replace_map (struct string_map *dst, const struct string_map *src)
{
  const struct string_map_node *snode;

  STRING_MAP_FOR_EACH_NODE (snode, src)
    {
      struct string_map_node *dnode;
      dnode = string_map_find_node__ (dst, snode->key, snode->hmap_node.hash);
      if (dnode != NULL)
        string_map_node_set_value (dnode, snode->value);
      else
        string_map_insert__ (dst, xstrdup (snode->key), xstrdup (snode->value),
                             snode->hmap_node.hash);
    }
}

/* Inserts each of the keys in MAP into KEYS.  KEYS must already have been
   initialized (using string_set_init()). */
void
string_map_get_keys (const struct string_map *map, struct string_set *keys)
{
  const struct string_map_node *node;
  const char *key;

  STRING_MAP_FOR_EACH_KEY (key, node, map)
    string_set_insert (keys, key);
}

/* Inserts each of the values in MAP into VALUES.  VALUES must already have
   been initialized (using string_set_init()). */
void
string_map_get_values (const struct string_map *map, struct string_set *values)
{
  const struct string_map_node *node;
  const char *value;

  STRING_MAP_FOR_EACH_VALUE (value, node, map)
    string_set_insert (values, value);
}

static struct string_map_node *
string_map_find_node__ (const struct string_map *map, const char *key,
                        unsigned int hash)
{
  struct string_map_node *node;

  HMAP_FOR_EACH_WITH_HASH (node, struct string_map_node, hmap_node,
                           hash, &map->hmap)
    if (!strcmp (key, node->key))
      return node;

  return NULL;
}

static bool
string_map_delete__ (struct string_map *map, const char *key,
                     unsigned int hash)
{
  struct string_map_node *node = string_map_find_node__ (map, key, hash);
  if (node != NULL)
    {
      string_map_delete_node (map, node);
      return true;
    }
  else
    return false;
}

static struct string_map_node *
string_map_insert__ (struct string_map *map, char *key, char *value,
                     unsigned int hash)
{
  struct string_map_node *node = xmalloc (sizeof *node);
  node->key = key;
  node->value = value;
  hmap_insert (&map->hmap, &node->hmap_node, hash);
  return node;
}

