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

#include "libpspp/intern.h"

#include <assert.h>
#include <string.h>

#include "libpspp/assertion.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmap.h"

#include "gl/xalloc.h"

/* A single interned string. */
struct interned_string
  {
    struct hmap_node node;      /* Node in hash table. */
    size_t ref_cnt;             /* Reference count. */
    char string[1];             /* Null-terminated string. */
  };

/* All interned strings. */
static struct hmap interns = HMAP_INITIALIZER (interns);

/* Searches the table of interned string for  */
static struct interned_string *
intern_lookup__ (const char *s, size_t length, unsigned int hash)
{
  struct interned_string *is;

  HMAP_FOR_EACH_WITH_HASH (is, struct interned_string, node, hash, &interns)
    if (!memcmp (s, is->string, length + 1))
      return is;

  return NULL;
}

/* Returns an interned version of string S.  Pass the returned string to
   intern_unref() to release it. */
const char *
intern_new (const char *s)
{
  size_t length = strlen (s);
  unsigned int hash = hash_bytes (s, length, 0);
  struct interned_string *is;

  is = intern_lookup__ (s, length, hash);
  if (is != NULL)
    is->ref_cnt++;
  else
    {
      is = xmalloc (length + sizeof *is);
      hmap_insert (&interns, &is->node, hash);
      is->ref_cnt = 1;
      memcpy (is->string, s, length + 1);
    }
  return is->string;
}

static struct interned_string *
interned_string_from_string (const char *s)
{
  const size_t ofs = offsetof (struct interned_string, string);
  struct interned_string *is = (struct interned_string *) (s - ofs);
  assert (is->ref_cnt > 0);
  return is;
}

/* Increases the reference count on S, which must be an interned string
   returned by intern_new(). */
const char *
intern_ref (const char *s)
{
  struct interned_string *is = interned_string_from_string (s);
  is->ref_cnt++;
  return s;
}

/* Decreases the reference count on S, which must be an interned string
   returned by intern_new().  If the reference count reaches 0, frees the
   interned string. */
void
intern_unref (const char *s)
{
  struct interned_string *is = interned_string_from_string (s);
  if (--is->ref_cnt == 0)
    {
      hmap_delete (&interns, &is->node);
      free (is);
    }
}

/* Given null-terminated string S, returns true if S is an interned string
   returned by intern_string_new(), false otherwise.

   This is appropriate for use in debug assertions, e.g.:
       assert (is_interned_string (s));
*/
bool
is_interned_string (const char *s)
{
  size_t length = strlen (s);
  unsigned int hash = hash_bytes (s, length, 0);
  return intern_lookup__ (s, length, hash) != NULL;
}
