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

/* If you add routines in this file, please add a corresponding test to
   string-set-test.c. */

#include <config.h>

#include "libpspp/string-set.h"

#include <stdlib.h>
#include <string.h>

#include "libpspp/hash-functions.h"

#include "gl/xalloc.h"

static struct string_set_node *string_set_find_node__ (
  const struct string_set *, const char *, unsigned int hash);
static void string_set_insert__ (struct string_set *, char *,
                                 unsigned int hash);
static bool string_set_delete__ (struct string_set *, const char *,
                                 unsigned int hash);

/* Initializes SET as a new string set that is initially empty. */
void
string_set_init (struct string_set *set)
{
  hmap_init (&set->hmap);
}

/* Initializes SET as a new string set that initially contains the same strings
   as OLD. */
void
string_set_clone (struct string_set *set, const struct string_set *old)
{
  const struct string_set_node *node;
  const char *s;

  string_set_init (set);
  hmap_reserve (&set->hmap, string_set_count (old));
  STRING_SET_FOR_EACH (s, node, old)
    string_set_insert__ (set, xstrdup (s), node->hmap_node.hash);
}

/* Exchanges the contents of string sets A and B. */
void
string_set_swap (struct string_set *a, struct string_set *b)
{
  hmap_swap (&a->hmap, &b->hmap);
}

/* Frees SET and its nodes and strings. */
void
string_set_destroy (struct string_set *set)
{
  if (set != NULL)
    {
      string_set_clear (set);
      hmap_destroy (&set->hmap);
    }
}

/* Returns true if SET contains S, false otherwise. */
bool
string_set_contains (const struct string_set *set, const char *s)
{
  return string_set_find_node (set, s) != NULL;
}

/* Returns the node in SET that contains S, or a null pointer if SET does not
   contain S. */
struct string_set_node *
string_set_find_node (const struct string_set *set, const char *s)
{
  return string_set_find_node__ (set, s, hash_string (s, 0));
}

/* Inserts a copy of S into SET.  Returns true if successful, false if SET
   is unchanged because it already contained S. */
bool
string_set_insert (struct string_set *set, const char *s)
{
  unsigned int hash = hash_string (s, 0);
  if (!string_set_find_node__ (set, s, hash))
    {
      string_set_insert__ (set, xstrdup (s), hash);
      return true;
    }
  else
    return false;
}

/* Inserts S, which must be a malloc'd string, into SET, transferring ownership
   of S to SET.  Returns true if successful, false if SET is unchanged because
   it already contained a copy of S.  (In the latter case, S is freed.) */
bool
string_set_insert_nocopy (struct string_set *set, char *s)
{
  unsigned int hash = hash_string (s, 0);
  if (!string_set_find_node__ (set, s, hash))
    {
      string_set_insert__ (set, s, hash);
      return true;
    }
  else
    {
      free (s);
      return false;
    }
}

/* Deletes S from SET.  Returns true if successful, false if SET is unchanged
   because it did not contain a copy of S. */
bool
string_set_delete (struct string_set *set, const char *s)
{
  return string_set_delete__ (set, s, hash_string (s, 0));
}

/* Deletes NODE from SET, and frees NODE and its string. */
void
string_set_delete_node (struct string_set *set, struct string_set_node *node)
{
  free (string_set_delete_nofree (set, node));
}

/* Deletes NODE from SET and frees NODE.  Returns the string that NODE
   contained, transferring ownership to the caller. */
char *
string_set_delete_nofree (struct string_set *set, struct string_set_node *node)
{
  char *string = node->string;
  hmap_delete (&set->hmap, &node->hmap_node);
  free (node);
  return string;
}

/* Removes all nodes from SET. */
void
string_set_clear (struct string_set *set)
{
  struct string_set_node *node, *next;

  HMAP_FOR_EACH_SAFE (node, next, struct string_set_node, hmap_node,
                      &set->hmap)
    string_set_delete_node (set, node);
}

/* Calculates A = union(A, B).

   If B may be modified, string_set_union_and_intersection() is
   faster than this function. */
void
string_set_union (struct string_set *a, const struct string_set *b)
{
  struct string_set_node *node;
  HMAP_FOR_EACH (node, struct string_set_node, hmap_node, &b->hmap)
    if (!string_set_find_node__ (a, node->string, node->hmap_node.hash))
      string_set_insert__ (a, xstrdup (node->string), node->hmap_node.hash);
}

/* Calculates A = union(A, B) and B = intersect(A, B).

   If only the intersection is needed, string_set_intersect() is
   faster. */
void
string_set_union_and_intersection (struct string_set *a, struct string_set *b)
{
  struct string_set_node *node, *next;

  HMAP_FOR_EACH_SAFE (node, next, struct string_set_node, hmap_node,
                      &b->hmap)
    if (!string_set_find_node__ (a, node->string, node->hmap_node.hash))
      {
        hmap_delete (&b->hmap, &node->hmap_node);
        hmap_insert (&a->hmap, &node->hmap_node, node->hmap_node.hash);
      }
}

/* Calculates A = intersect(A, B). */
void
string_set_intersect (struct string_set *a, const struct string_set *b)
{
  struct string_set_node *node, *next;

  HMAP_FOR_EACH_SAFE (node, next, struct string_set_node, hmap_node,
                      &a->hmap)
    if (!string_set_find_node__ (b, node->string, node->hmap_node.hash))
      string_set_delete_node (a, node);
}

/* Removes from A all of the strings in B. */
void
string_set_subtract (struct string_set *a, const struct string_set *b)
{
  struct string_set_node *node, *next;

  if (string_set_count (a) < string_set_count (b))
    {
      HMAP_FOR_EACH_SAFE (node, next, struct string_set_node, hmap_node,
                          &a->hmap)
        if (string_set_find_node__ (b, node->string, node->hmap_node.hash))
          string_set_delete_node (a, node);
    }
  else
    {
      HMAP_FOR_EACH (node, struct string_set_node, hmap_node, &b->hmap)
        string_set_delete__ (a, node->string, node->hmap_node.hash);
    }
}

/* Internal functions. */

static struct string_set_node *
string_set_find_node__ (const struct string_set *set, const char *s,
                        unsigned int hash)
{
  struct string_set_node *node;

  HMAP_FOR_EACH_WITH_HASH (node, struct string_set_node, hmap_node,
                           hash, &set->hmap)
    if (!strcmp (s, node->string))
      return node;

  return NULL;
}

static void
string_set_insert__ (struct string_set *set, char *s, unsigned int hash)
{
  struct string_set_node *node = xmalloc (sizeof *node);
  node->string = s;
  hmap_insert (&set->hmap, &node->hmap_node, hash);
}

static bool
string_set_delete__ (struct string_set *set, const char *s, unsigned int hash)
{
  struct string_set_node *node = string_set_find_node__ (set, s, hash);
  if (node != NULL)
    {
      string_set_delete_node (set, node);
      return true;
    }
  else
    return false;
}
