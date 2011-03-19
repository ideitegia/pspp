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

#ifndef LIBPSPP_STRING_SET_H
#define LIBPSPP_STRING_SET_H

/* Set of unique strings.

   This is a convenient wrapper around a "struct hmap" for storing strings. */

#include <stdbool.h>
#include "libpspp/hmap.h"

/* A node in the string set. */
struct string_set_node
  {
    struct hmap_node hmap_node;
    char *string;
  };

/* Returns the string within NODE.  The caller must not modify or free the
   returned string. */
static inline const char *
string_set_node_get_string (const struct string_set_node *node)
{
  return node->string;
}

/* An unordered set of unique strings. */
struct string_set
  {
    struct hmap hmap;
  };

/* Suitable for use as the initializer for a string_set named SET.  Typical
   usage:
       struct string_set set = STRING_SET_INITIALIZER (set);
   STRING_SET_INITIALIZER is an alternative to calling string_set_init. */
#define STRING_SET_INITIALIZER(SET) { HMAP_INITIALIZER ((SET).hmap) }

void string_set_init (struct string_set *);
void string_set_clone (struct string_set *, const struct string_set *);
void string_set_swap (struct string_set *, struct string_set *);
void string_set_destroy (struct string_set *);

static inline size_t string_set_count (const struct string_set *);
static inline bool string_set_is_empty (const struct string_set *);

bool string_set_contains (const struct string_set *, const char *);
struct string_set_node *string_set_find_node (const struct string_set *,
                                              const char *);

bool string_set_insert (struct string_set *, const char *);
bool string_set_insert_nocopy (struct string_set *, char *);
bool string_set_delete (struct string_set *, const char *);
void string_set_delete_node (struct string_set *, struct string_set_node *);
char *string_set_delete_nofree (struct string_set *, struct string_set_node *);

void string_set_clear (struct string_set *);
void string_set_union (struct string_set *, const struct string_set *);
void string_set_union_and_intersection (struct string_set *,
                                        struct string_set *);
void string_set_intersect (struct string_set *, const struct string_set *);
void string_set_subtract (struct string_set *, const struct string_set *);

static inline const struct string_set_node *string_set_first (
  const struct string_set *);
static inline const struct string_set_node *string_set_next (
  const struct string_set *, const struct string_set_node *);

/* Macros for conveniently iterating through a string_set, e.g. to print all of
   the strings in "my_set":

   struct string_set_node *node;
   const char *string;

   STRING_SET_FOR_EACH (string, node, &my_set)
     puts (string);
   */
#define STRING_SET_FOR_EACH(STRING, NODE, SET)                  \
        for ((NODE) = string_set_first (SET);                   \
             ((NODE) != NULL                                    \
              ? ((STRING) = string_set_node_get_string (NODE),  \
                 1)                                             \
              : 0);                                             \
             (NODE) = string_set_next (SET, NODE))
#define STRING_SET_FOR_EACH_SAFE(STRING, NODE, NEXT, SET)       \
        for ((NODE) = string_set_first (SET);                   \
             ((NODE) != NULL                                    \
              ? ((STRING) = string_set_node_get_string (NODE),  \
                 (NEXT) = string_set_next (SET, NODE),          \
                 1)                                             \
              : 0);                                             \
             (NODE) = (NEXT))

/* Returns the number of strings currently in SET. */
static inline size_t
string_set_count (const struct string_set *set)
{
  return hmap_count (&set->hmap);
}

/* Returns true if SET currently contains no strings, false otherwise. */
static inline bool
string_set_is_empty (const struct string_set *set)
{
  return hmap_is_empty (&set->hmap);
}

/* Returns the first node in SET, or a null pointer if SET is empty.  See the
   hmap_first function for information about complexity (O(1) amortized) and
   ordering (arbitrary).

   The STRING_SET_FOR_EACH and STRING_SET_FOR_EACH_SAFE macros provide
   convenient ways to iterate over all the nodes in a string set. */
static inline const struct string_set_node *
string_set_first (const struct string_set *set)
{
  return HMAP_FIRST (struct string_set_node, hmap_node, &set->hmap);
}

/* Returns the next node in SET following NODE, or a null pointer if NODE is
   the last node in SET.  See the hmap_next function for information about
   complexity (O(1) amortized) and ordering (arbitrary).

   The STRING_SET_FOR_EACH and STRING_SET_FOR_EACH_SAFE macros provide
   convenient ways to iterate over all the nodes in a string set. */
static inline const struct string_set_node *
string_set_next (const struct string_set *set,
                 const struct string_set_node *node)
{
  return HMAP_NEXT (node, struct string_set_node, hmap_node, &set->hmap);
}

#endif /* libpspp/string-set.h */
