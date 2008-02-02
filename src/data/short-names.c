/* PSPP - a program for statistical analysis.
   Copyright (C) 2007 Free Software Foundation, Inc.

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

#include <data/short-names.h>

#include <data/dictionary.h>
#include <data/sys-file-private.h>
#include <data/variable.h>
#include <libpspp/assertion.h>
#include <libpspp/compiler.h>
#include <libpspp/hash.h>
#include <libpspp/message.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Compares two strings. */
static int
compare_strings (const void *a, const void *b, const void *aux UNUSED)
{
  return strcmp (a, b);
}

/* Hashes a string. */
static unsigned
hash_string (const void *s, const void *aux UNUSED)
{
  return hsh_hash_string (s);
}

/* Sets V's short name to BASE, followed by a suffix of the form
   _A, _B, _C, ..., _AA, _AB, etc. according to the value of
   SUFFIX_NUMBER.  Truncates BASE as necessary to fit. */
static void
set_var_short_name_suffix (struct variable *v, size_t i,
                           const char *base, int suffix_number)
{
  char suffix[SHORT_NAME_LEN + 1];
  char short_name[SHORT_NAME_LEN + 1];
  int len, ofs;

  assert (suffix_number >= 0);

  /* Set base name. */
  var_set_short_name (v, i, base);

  /* Compose suffix. */
  suffix[0] = '_';
  if (!str_format_26adic (suffix_number, &suffix[1], sizeof suffix - 1))
    msg (SE, _("Variable suffix too large."));
  len = strlen (suffix);

  /* Append suffix to V's short name. */
  str_copy_trunc (short_name, sizeof short_name, base);
  if (strlen (short_name) + len > SHORT_NAME_LEN)
    ofs = SHORT_NAME_LEN - len;
  else
    ofs = strlen (short_name);
  strcpy (short_name + ofs, suffix);

  /* Set name. */
  var_set_short_name (v, i, short_name);
}

static void
claim_short_name (struct variable *v, size_t i, struct hsh_table *short_names)
{
  const char *short_name = var_get_short_name (v, i);
  if (short_name != NULL
      && hsh_insert (short_names, (char *) short_name) != NULL)
    var_set_short_name (v, i, NULL);
}

/* Form initial short_name from the variable name, then try _A,
   _B, ... _AA, _AB, etc., if needed. */
static void
assign_short_name (struct variable *v, size_t i, struct hsh_table *short_names)
{
  int trial;

  if (var_get_short_name (v, i) != NULL)
    return;

  for (trial = 0; ; trial++)
    {
      if (trial == 0)
        var_set_short_name (v, i, var_get_name (v));
      else
        set_var_short_name_suffix (v, i, var_get_name (v), trial);

      if (hsh_insert (short_names, (char *) var_get_short_name (v, i)) == NULL)
        break;
    }
}

/* Assigns a valid, unique short_name[] to each variable in D.
   Each variable whose actual name is short has highest priority
   for that short name.  Otherwise, variables with an existing
   short_name[] have the next highest priority for a given short
   name; if it is already taken, then the variable is treated as
   if short_name[] had been empty.  Otherwise, long names are
   truncated to form short names.  If that causes conflicts,
   variables are renamed as PREFIX_A, PREFIX_B, and so on. */
void
short_names_assign (struct dictionary *d)
{
  size_t var_cnt = dict_get_var_cnt (d);
  struct hsh_table *short_names;
  size_t i, j;

  /* Create hash used for detecting conflicts.  The entries in
     the hash table point to strings owned by dictionary
     variables, not by us, so we don't need to provide a free
     function. */
  short_names = hsh_create (var_cnt, compare_strings, hash_string,
                            NULL, NULL);

  /* Clear short names that conflict with a variable name. */
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *v = dict_get_var (d, i);
      int segment_cnt = sfm_width_to_segments (var_get_width (v));
      for (j = 0; j < segment_cnt; j++)
        {
          const char *name = var_get_short_name (v, j);
          if (name != NULL)
            {
              struct variable *ov = dict_lookup_var (d, name);
              if (ov != NULL && (ov != v || j > 0))
                var_set_short_name (v, j, NULL);
            }
        }
    }

  /* Give variables whose names are short the corresponding short
     name. */
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *v = dict_get_var (d, i);
      if (strlen (var_get_name (v)) <= SHORT_NAME_LEN)
        var_set_short_name (v, 0, var_get_name (v));
    }

  /* Each variable with an assigned short name for its first
     segment now gets it unless there is a conflict.  In case of
     conflict, the claimant earlier in dictionary order wins.
     Then similarly for additional segments of very long
     strings. */
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *v = dict_get_var (d, i);
      claim_short_name (v, 0, short_names);
    }
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *v = dict_get_var (d, i);
      int segment_cnt = sfm_width_to_segments (var_get_width (v));
      for (j = 1; j < segment_cnt; j++)
        claim_short_name (v, j, short_names);
    }

  /* Assign short names to first segment of remaining variables,
     then similarly for additional segments. */
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *v = dict_get_var (d, i);
      assign_short_name (v, 0, short_names);
    }
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *v = dict_get_var (d, i);
      int segment_cnt = sfm_width_to_segments (var_get_width (v));
      for (j = 1; j < segment_cnt; j++)
        assign_short_name (v, j, short_names);
    }

  /* Get rid of hash table. */
  hsh_destroy (short_names);
}
