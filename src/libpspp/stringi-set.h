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

#ifndef LIBPSPP_STRINGI_SET_H
#define LIBPSPP_STRINGI_SET_H

/* Set of unique, case-insensitive strings.

   This is a convenient wrapper around a "struct hmap" for storing strings. */

#include <stdbool.h>
#include "libpspp/hmap.h"

/* A node in the string set. */
struct stringi_set_node
  {
    struct hmap_node hmap_node;
    char *string;
  };

/* Returns the string within NODE.  The caller must not modify or free the
   returned string. */
static inline const char *
stringi_set_node_get_string (const struct stringi_set_node *node)
{
  return node->string;
}

/* An unordered set of unique strings. */
struct stringi_set
  {
    struct hmap hmap;
  };

/* Suitable for use as the initializer for a stringi_set named SET.  Typical
   usage:
       struct stringi_set set = STRINGI_SET_INITIALIZER (set);
   STRINGI_SET_INITIALIZER is an alternative to calling stringi_set_init. */
#define STRINGI_SET_INITIALIZER(SET) { HMAP_INITIALIZER ((SET).hmap) }

void stringi_set_init (struct stringi_set *);
void stringi_set_clone (struct stringi_set *, const struct stringi_set *);
void stringi_set_swap (struct stringi_set *, struct stringi_set *);
void stringi_set_destroy (struct stringi_set *);

static inline size_t stringi_set_count (const struct stringi_set *);
static inline bool stringi_set_is_empty (const struct stringi_set *);

bool stringi_set_contains (const struct stringi_set *, const char *);
struct stringi_set_node *stringi_set_find_node (const struct stringi_set *,
                                              const char *);

bool stringi_set_insert (struct stringi_set *, const char *);
bool stringi_set_insert_nocopy (struct stringi_set *, char *);
bool stringi_set_delete (struct stringi_set *, const char *);
void stringi_set_delete_node (struct stringi_set *, struct stringi_set_node *);
char *stringi_set_delete_nofree (struct stringi_set *,
                                 struct stringi_set_node *);

void stringi_set_clear (struct stringi_set *);
void stringi_set_union (struct stringi_set *, const struct stringi_set *);
void stringi_set_union_and_intersection (struct stringi_set *,
                                        struct stringi_set *);
void stringi_set_intersect (struct stringi_set *, const struct stringi_set *);
void stringi_set_subtract (struct stringi_set *, const struct stringi_set *);

char **stringi_set_get_array (const struct stringi_set *);
char **stringi_set_get_sorted_array (const struct stringi_set *);

static inline const struct stringi_set_node *stringi_set_first (
  const struct stringi_set *);
static inline const struct stringi_set_node *stringi_set_next (
  const struct stringi_set *, const struct stringi_set_node *);

/* Macros for conveniently iterating through a stringi_set, e.g. to print all
   of the strings in "my_set":

   struct stringi_set_node *node;
   const char *string;

   STRINGI_SET_FOR_EACH (string, node, &my_set)
     puts (string);
   */
#define STRINGI_SET_FOR_EACH(STRING, NODE, SET)                 \
        for ((NODE) = stringi_set_first (SET);                  \
             ((NODE) != NULL                                    \
              ? ((STRING) = stringi_set_node_get_string (NODE), \
                 1)                                             \
              : 0);                                             \
             (NODE) = stringi_set_next (SET, NODE))
#define STRINGI_SET_FOR_EACH_SAFE(STRING, NODE, NEXT, SET)      \
        for ((NODE) = stringi_set_first (SET);                  \
             ((NODE) != NULL                                    \
              ? ((STRING) = stringi_set_node_get_string (NODE), \
                 (NEXT) = stringi_set_next (SET, NODE),         \
                 1)                                             \
              : 0);                                             \
             (NODE) = (NEXT))

/* Returns the number of strings currently in SET. */
static inline size_t
stringi_set_count (const struct stringi_set *set)
{
  return hmap_count (&set->hmap);
}

/* Returns true if SET currently contains no strings, false otherwise. */
static inline bool
stringi_set_is_empty (const struct stringi_set *set)
{
  return hmap_is_empty (&set->hmap);
}

/* Returns the first node in SET, or a null pointer if SET is empty.  See the
   hmap_first function for information about complexity (O(1) amortized) and
   ordering (arbitrary).

   The STRINGI_SET_FOR_EACH and STRINGI_SET_FOR_EACH_SAFE macros provide
   convenient ways to iterate over all the nodes in a string set. */
static inline const struct stringi_set_node *
stringi_set_first (const struct stringi_set *set)
{
  return HMAP_FIRST (struct stringi_set_node, hmap_node, &set->hmap);
}

/* Returns the next node in SET following NODE, or a null pointer if NODE is
   the last node in SET.  See the hmap_next function for information about
   complexity (O(1) amortized) and ordering (arbitrary).

   The STRINGI_SET_FOR_EACH and STRINGI_SET_FOR_EACH_SAFE macros provide
   convenient ways to iterate over all the nodes in a string set. */
static inline const struct stringi_set_node *
stringi_set_next (const struct stringi_set *set,
                 const struct stringi_set_node *node)
{
  return HMAP_NEXT (node, struct stringi_set_node, hmap_node, &set->hmap);
}

#endif /* libpspp/string-set.h */
