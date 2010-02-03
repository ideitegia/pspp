/* PSPP - a program for statistical analysis.
   Copyright (C) 2010 Free Software Foundation, Inc.

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

#ifndef LIBPSPP_STRING_ARRAY_H
#define LIBPSPP_STRING_ARRAY_H

#include <stdbool.h>
#include <stddef.h>

/* An unordered array of strings.

   Not opaque by any means. */
struct string_array
  {
    char **strings;
    size_t n;
    size_t allocated;
  };

/* Suitable for use as the initializer for a string_array named ARRAY.  Typical
   usage:
       struct string_array array = STRING_ARRAY_INITIALIZER (array);
   STRING_ARRAY_INITIALIZER is an alternative to calling string_array_init. */
#define STRING_ARRAY_INITIALIZER(ARRAY) { NULL, 0, 0 }

void string_array_init (struct string_array *);
void string_array_clone (struct string_array *, const struct string_array *);
void string_array_swap (struct string_array *, struct string_array *);
void string_array_destroy (struct string_array *);

static inline size_t string_array_count (const struct string_array *);
static inline bool string_array_is_empty (const struct string_array *);

bool string_array_contains (const struct string_array *, const char *);
size_t string_array_find (const struct string_array *, const char *);

void string_array_append (struct string_array *, const char *);
void string_array_append_nocopy (struct string_array *, char *);
void string_array_insert (struct string_array *, const char *, size_t before);
void string_array_insert_nocopy (struct string_array *, char *, size_t before);
void string_array_delete (struct string_array *, size_t idx);
char *string_array_delete_nofree (struct string_array *, size_t idx);

void string_array_clear (struct string_array *);

void string_array_terminate_null (struct string_array *);
void string_array_shrink (struct string_array *);

void string_array_sort (struct string_array *);

char *string_array_join (const struct string_array *, const char *separator);

/* Macros for conveniently iterating through a string_array, e.g. to print all
   of the strings in "my_array":

   const char *string;
   size_t idx;

   STRING_ARRAY_FOR_EACH (string, idx, &my_array)
     puts (string);
*/
#define STRING_ARRAY_FOR_EACH(STRING, IDX, ARRAY)                  \
  for ((IDX) = 0;                                                  \
       ((IDX) < (ARRAY)->n                                         \
        ? ((STRING) = (ARRAY)->strings[IDX], true)                 \
        : false);                                                  \
       (IDX)++)

/* Returns the number of strings currently in ARRAY. */
static inline size_t
string_array_count (const struct string_array *array)
{
  return array->n;
}

/* Returns true if ARRAY currently contains no strings, false otherwise. */
static inline bool
string_array_is_empty (const struct string_array *array)
{
  return array->n == 0;
}

#endif /* libpspp/string-array.h */
