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

#include <config.h>

#include "libpspp/string-array.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libpspp/array.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

/* Initializes SA as an initially empty array of strings. */
void
string_array_init (struct string_array *sa)
{
  sa->strings = NULL;
  sa->n = 0;
  sa->allocated = 0;
}

/* Initializes DST as an array of strings whose contents are initially copies
   of the strings in SRC. */
void
string_array_clone (struct string_array *dst, const struct string_array *src)
{
  size_t i;

  dst->strings = xmalloc (sizeof *dst->strings * src->n);
  for (i = 0; i < src->n; i++)
    dst->strings[i] = xstrdup (src->strings[i]);
  dst->n = src->n;
  dst->allocated = src->n;
}

/* Exchanges the contents of A and B. */
void
string_array_swap (struct string_array *a, struct string_array *b)
{
  struct string_array tmp = *a;
  *a = *b;
  *b = tmp;
}

/* Frees the strings in SA.  SA must be reinitialized (with
   string_array_init()) before it is used again. */
void
string_array_destroy (struct string_array *sa)
{
  if (sa != NULL)
    {
      string_array_clear (sa);
      free (sa->strings);
    }
}

/* Returns true if SA contains at least one copy of STRING, otherwise false.

   This function runs in O(n) time in the number of strings in SA. */
bool
string_array_contains (const struct string_array *sa, const char *string)
{
  return string_array_find (sa, string) != SIZE_MAX;
}

/* If SA contains at least one copy of STRING, returns the smallest index of
   any of those copies.  If SA does not contain STRING, returns SIZE_MAX.

   This function runs in O(n) time in the number of strings in SA. */
size_t
string_array_find (const struct string_array *sa, const char *string)
{
  size_t i;

  for (i = 0; i < sa->n; i++)
    if (!strcmp (sa->strings[i], string))
      return i;
  return SIZE_MAX;
}

/* Appends a copy of STRING to SA.  The caller retains ownership of STRING. */
void
string_array_append (struct string_array *sa, const char *string)
{
  string_array_insert (sa, string, sa->n);
}

/* Appends STRING to SA.  Ownership of STRING transfers to SA. */
void
string_array_append_nocopy (struct string_array *sa, char *string)
{
  string_array_insert_nocopy (sa, string, sa->n);
}

/* Inserts a copy of STRING in SA just before the string with index BEFORE,
   which must be less than or equal to the number of strings in SA.  The caller
   retains ownership of STRING.

   In general, this function runs in O(n) time in the number of strings that
   must be shifted to higher indexes; if BEFORE is the number of strings in SA,
   it runs in amortized constant time. */
void
string_array_insert (struct string_array *sa,
                     const char *string, size_t before)
{
  string_array_insert_nocopy (sa, xstrdup (string), before);
}

static void
string_array_expand__ (struct string_array *sa)
{
  if (sa->n >= sa->allocated)
    sa->strings = x2nrealloc (sa->strings, &sa->allocated,
                              sizeof *sa->strings);
}

/* Inserts STRING in SA just before the string with index BEFORE, which must be
   less than or equal to the number of strings in SA.  Ownership of STRING
   transfers to SA.

   In general, this function runs in O(n) time in the number of strings that
   must be shifted to higher indexes; if BEFORE is the number of strings in SA,
   it runs in amortized constant time. */
void
string_array_insert_nocopy (struct string_array *sa, char *string,
                            size_t before)
{
  string_array_expand__ (sa);
  if (before < sa->n)
    insert_element (sa->strings, sa->n, sizeof *sa->strings, before);

  sa->strings[before] = string;
  sa->n++;
}

/* Deletes from SA the string with index IDX, which must be less than the
   number of strings in SA, and shifts down the strings with higher indexes.
   Frees the string.

   In general, this function runs in O(n) time in the number of strings that
   must be shifted to lower indexes.  If IDX is the last string in SA, it runs
   in amortized constant time. */
void
string_array_delete (struct string_array *sa, size_t idx)
{
  free (string_array_delete_nofree (sa, idx));
}

/* Deletes from SA the string with index IDX, which must be less than the
   number of strings in SA.  Returns the string, which the caller is
   responsible for freeing with free().

   In general, this function runs in O(n) time in the number of strings that
   must be shifted to lower indexes.  If IDX is the last string in SA, it runs
   in amortized constant time. */
char *
string_array_delete_nofree (struct string_array *sa, size_t idx)
{
  char *s = sa->strings[idx];
  if (idx != sa->n - 1)
    remove_element (sa->strings, sa->n, sizeof *sa->strings, idx);
  sa->n--;
  return s;
}

/* Deletes all of the strings from SA and frees them. */
void
string_array_clear (struct string_array *sa)
{
  size_t i;

  for (i = 0; i < sa->n; i++)
    free (sa->strings[i]);
  sa->n = 0;
}

/* Ensures that 'sa->strings[sa->n]' is a null pointer (until SA is modified
   further). */
void
string_array_terminate_null (struct string_array *sa)
{
  string_array_expand__ (sa);
  sa->strings[sa->n] = NULL;
}

/* Reduces the amount of memory allocated for SA's strings to the minimum
   necessary. */
void
string_array_shrink (struct string_array *sa)
{
  if (sa->allocated > sa->n)
    {
      if (sa->n > 0)
        sa->strings = xrealloc (sa->strings, sa->n * sizeof *sa->strings);
      else
        {
          free (sa->strings);
          sa->strings = NULL;
        }
      sa->allocated = sa->n;
    }
}

static int
compare_strings (const void *a_, const void *b_)
{
  const void *const *a = a_;
  const void *const *b = b_;

  return strcmp (*a, *b);
}

/* Sorts the strings in SA into order according to strcmp(). */
void
string_array_sort (struct string_array *sa)
{
  qsort (sa->strings, sa->n, sizeof *sa->strings, compare_strings);
}

/* Returns a single string that consists of each of the strings in SA
   concatenated, separated from each other with SEPARATOR.

   The caller is responsible for freeing the returned string with free(). */
char *
string_array_join (const struct string_array *sa, const char *separator)
{
  struct string dst;
  const char *s;
  size_t i;

  ds_init_empty (&dst);
  STRING_ARRAY_FOR_EACH (s, i, sa)
    {
      if (i > 0)
        ds_put_cstr (&dst, separator);
      ds_put_cstr (&dst, s);
    }
  return ds_steal_cstr (&dst);
}
