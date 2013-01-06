/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2012 Free Software Foundation, Inc.

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
   stringi-set-test.c. */

#include <config.h>

#include "libpspp/stringi-set.h"

#include <stdlib.h>
#include <string.h>

#include "libpspp/cast.h"
#include "libpspp/hash-functions.h"
#include "libpspp/i18n.h"

#include "gl/xalloc.h"

static struct stringi_set_node *stringi_set_find_node__ (
  const struct stringi_set *, const char *, unsigned int hash);
static void stringi_set_insert__ (struct stringi_set *, char *,
                                 unsigned int hash);
static bool stringi_set_delete__ (struct stringi_set *, const char *,
                                 unsigned int hash);

/* Initializes SET as a new string set that is initially empty. */
void
stringi_set_init (struct stringi_set *set)
{
  hmap_init (&set->hmap);
}

/* Initializes SET as a new string set that initially contains the same strings
   as OLD. */
void
stringi_set_clone (struct stringi_set *set, const struct stringi_set *old)
{
  const struct stringi_set_node *node;
  const char *s;

  stringi_set_init (set);
  hmap_reserve (&set->hmap, stringi_set_count (old));
  STRINGI_SET_FOR_EACH (s, node, old)
    stringi_set_insert__ (set, xstrdup (s), node->hmap_node.hash);
}

/* Exchanges the contents of string sets A and B. */
void
stringi_set_swap (struct stringi_set *a, struct stringi_set *b)
{
  hmap_swap (&a->hmap, &b->hmap);
}

/* Frees SET and its nodes and strings. */
void
stringi_set_destroy (struct stringi_set *set)
{
  if (set != NULL)
    {
      stringi_set_clear (set);
      hmap_destroy (&set->hmap);
    }
}

/* Returns true if SET contains S (or a similar string with different case),
   false otherwise. */
bool
stringi_set_contains (const struct stringi_set *set, const char *s)
{
  return stringi_set_find_node (set, s) != NULL;
}

/* Returns the node in SET that contains S, or a null pointer if SET does not
   contain S. */
struct stringi_set_node *
stringi_set_find_node (const struct stringi_set *set, const char *s)
{
  return stringi_set_find_node__ (set, s, utf8_hash_case_string (s, 0));
}

/* Inserts a copy of S into SET.  Returns true if successful, false if SET
   is unchanged because it already contained S. */
bool
stringi_set_insert (struct stringi_set *set, const char *s)
{
  unsigned int hash = utf8_hash_case_string (s, 0);
  if (!stringi_set_find_node__ (set, s, hash))
    {
      stringi_set_insert__ (set, xstrdup (s), hash);
      return true;
    }
  else
    return false;
}

/* Inserts S, which must be a malloc'd string, into SET, transferring ownership
   of S to SET.  Returns true if successful, false if SET is unchanged because
   it already contained a copy of S.  (In the latter case, S is freed.) */
bool
stringi_set_insert_nocopy (struct stringi_set *set, char *s)
{
  unsigned int hash = utf8_hash_case_string (s, 0);
  if (!stringi_set_find_node__ (set, s, hash))
    {
      stringi_set_insert__ (set, s, hash);
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
stringi_set_delete (struct stringi_set *set, const char *s)
{
  return stringi_set_delete__ (set, s, utf8_hash_case_string (s, 0));
}

/* Deletes NODE from SET, and frees NODE and its string. */
void
stringi_set_delete_node (struct stringi_set *set,
                         struct stringi_set_node *node)
{
  free (stringi_set_delete_nofree (set, node));
}

/* Deletes NODE from SET and frees NODE.  Returns the string that NODE
   contained, transferring ownership to the caller. */
char *
stringi_set_delete_nofree (struct stringi_set *set,
                           struct stringi_set_node *node)
{
  char *string = node->string;
  hmap_delete (&set->hmap, &node->hmap_node);
  free (node);
  return string;
}

/* Removes all nodes from SET. */
void
stringi_set_clear (struct stringi_set *set)
{
  struct stringi_set_node *node, *next;

  HMAP_FOR_EACH_SAFE (node, next, struct stringi_set_node, hmap_node,
                      &set->hmap)
    stringi_set_delete_node (set, node);
}

/* Calculates A = union(A, B).

   If B may be modified, stringi_set_union_and_intersection() is
   faster than this function. */
void
stringi_set_union (struct stringi_set *a, const struct stringi_set *b)
{
  struct stringi_set_node *node;
  HMAP_FOR_EACH (node, struct stringi_set_node, hmap_node, &b->hmap)
    if (!stringi_set_find_node__ (a, node->string, node->hmap_node.hash))
      stringi_set_insert__ (a, xstrdup (node->string), node->hmap_node.hash);
}

/* Calculates A = union(A, B) and B = intersect(A, B).

   If only the intersection is needed, stringi_set_intersect() is
   faster. */
void
stringi_set_union_and_intersection (struct stringi_set *a,
                                    struct stringi_set *b)
{
  struct stringi_set_node *node, *next;

  HMAP_FOR_EACH_SAFE (node, next, struct stringi_set_node, hmap_node,
                      &b->hmap)
    if (!stringi_set_find_node__ (a, node->string, node->hmap_node.hash))
      {
        hmap_delete (&b->hmap, &node->hmap_node);
        hmap_insert (&a->hmap, &node->hmap_node, node->hmap_node.hash);
      }
}

/* Calculates A = intersect(A, B). */
void
stringi_set_intersect (struct stringi_set *a, const struct stringi_set *b)
{
  struct stringi_set_node *node, *next;

  HMAP_FOR_EACH_SAFE (node, next, struct stringi_set_node, hmap_node,
                      &a->hmap)
    if (!stringi_set_find_node__ (b, node->string, node->hmap_node.hash))
      stringi_set_delete_node (a, node);
}

/* Removes from A all of the strings in B. */
void
stringi_set_subtract (struct stringi_set *a, const struct stringi_set *b)
{
  struct stringi_set_node *node, *next;

  if (stringi_set_count (a) < stringi_set_count (b))
    {
      HMAP_FOR_EACH_SAFE (node, next, struct stringi_set_node, hmap_node,
                          &a->hmap)
        if (stringi_set_find_node__ (b, node->string, node->hmap_node.hash))
          stringi_set_delete_node (a, node);
    }
  else
    {
      HMAP_FOR_EACH (node, struct stringi_set_node, hmap_node, &b->hmap)
        stringi_set_delete__ (a, node->string, node->hmap_node.hash);
    }
}

/* Allocates and returns an array that points to each of the strings in SET.
   The caller must not free or modify any of the strings.  Removing a string
   from SET invalidates the corresponding element of the returned array.  The
   caller it is responsible for freeing the returned array itself (with
   free()).

   The returned array is in the same order as observed by stringi_set_first()
   and stringi_set_next(), that is, no particular order. */
char **
stringi_set_get_array (const struct stringi_set *set)
{
  const struct stringi_set_node *node;
  const char *s;
  char **array;
  size_t i;

  array = xnmalloc (stringi_set_count (set), sizeof *array);

  i = 0;
  STRINGI_SET_FOR_EACH (s, node, set)
    array[i++] = CONST_CAST (char *, s);

  return array;
}

static int
compare_strings (const void *a_, const void *b_)
{
  const char *const *a = a_;
  const char *const *b = b_;
  return utf8_strcasecmp (*a, *b);
}

/* Allocates and returns an array that points to each of the strings in SET.
   The caller must not free or modify any of the strings.  Removing a string
   from SET invalidates the corresponding element of the returned array.  The
   caller it is responsible for freeing the returned array itself (with
   free()).

   The returned array is ordered according to utf8_strcasecmp(). */
char **
stringi_set_get_sorted_array (const struct stringi_set *set)
{
  char **array = stringi_set_get_array (set);
  qsort (array, stringi_set_count (set), sizeof *array, compare_strings);
  return array;
}

/* Internal functions. */

static struct stringi_set_node *
stringi_set_find_node__ (const struct stringi_set *set, const char *s,
                        unsigned int hash)
{
  struct stringi_set_node *node;

  HMAP_FOR_EACH_WITH_HASH (node, struct stringi_set_node, hmap_node,
                           hash, &set->hmap)
    if (!utf8_strcasecmp (s, node->string))
      return node;

  return NULL;
}

static void
stringi_set_insert__ (struct stringi_set *set, char *s, unsigned int hash)
{
  struct stringi_set_node *node = xmalloc (sizeof *node);
  node->string = s;
  hmap_insert (&set->hmap, &node->hmap_node, hash);
}

static bool
stringi_set_delete__ (struct stringi_set *set, const char *s,
                      unsigned int hash)
{
  struct stringi_set_node *node = stringi_set_find_node__ (set, s, hash);
  if (node != NULL)
    {
      stringi_set_delete_node (set, node);
      return true;
    }
  else
    return false;
}
