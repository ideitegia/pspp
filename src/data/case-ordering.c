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

#include <data/case-ordering.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <data/dictionary.h>
#include <data/variable.h>

#include "xalloc.h"

/* One key used for sorting. */
struct sort_key
  {
    const struct variable *var;       /* Variable. */
    enum sort_direction dir;    /* Sort direction. */
  };

/* A set of criteria for ordering cases. */
struct case_ordering
  {
    /* Sort keys. */
    struct sort_key *keys;
    size_t key_cnt;
  };

/* Creates and returns a new case ordering for comparing cases
   that represent dictionary DICT.  The case ordering initially
   contains no variables, so that all cases will compare as
   equal. */
struct case_ordering *
case_ordering_create (void)
{
  struct case_ordering *co = xmalloc (sizeof *co);
  co->keys = NULL;
  co->key_cnt = 0;
  return co;
}

/* Creates and returns a clone of case ordering ORIG. */
struct case_ordering *
case_ordering_clone (const struct case_ordering *orig)
{
  struct case_ordering *co = xmalloc (sizeof *co);
  co->keys = xmemdup (orig->keys, orig->key_cnt * sizeof *orig->keys);
  co->key_cnt = orig->key_cnt;
  return co;
}

/* Destroys case ordering CO. */
void
case_ordering_destroy (struct case_ordering *co)
{
  if (co != NULL)
    {
      free (co->keys);
      free (co);
    }
}

/* Compares cases A and B given case ordering CO and returns a
   strcmp()-type result. */
int
case_ordering_compare_cases (const struct ccase *a, const struct ccase *b,
                             const struct case_ordering *co)
{
  size_t i;

  for (i = 0; i < co->key_cnt; i++)
    {
      const struct sort_key *key = &co->keys[i];
      int width = var_get_width (key->var);
      int cmp;

      if (width == 0)
        {
          double af = case_num (a, key->var);
          double bf = case_num (b, key->var);
          if (af == bf)
            continue;
          cmp = af > bf ? 1 : -1;
        }
      else
        {
          const char *as = case_str (a, key->var);
          const char *bs = case_str (b, key->var);
          cmp = memcmp (as, bs, width);
          if (cmp == 0)
            continue;
        }

      return key->dir == SRT_ASCEND ? cmp : -cmp;
    }
  return 0;
}

/* Adds VAR to case ordering CO as an additional sort key in sort
   direction DIR.  Returns true if successful, false if VAR was
   already part of the ordering for CO. */
bool
case_ordering_add_var (struct case_ordering *co,
                       const struct variable *var, enum sort_direction dir)
{
  struct sort_key *key;
  size_t i;

  for (i = 0; i < co->key_cnt; i++)
    if (var_get_case_index (co->keys[i].var) == var_get_case_index (var))
      return false;

  co->keys = xnrealloc (co->keys, co->key_cnt + 1, sizeof *co->keys);
  key = &co->keys[co->key_cnt++];
  key->var = var;
  key->dir = dir;
  return true;
}

/* Returns the number of variables used for ordering within
   CO. */
size_t
case_ordering_get_var_cnt (const struct case_ordering *co)
{
  return co->key_cnt;
}

/* Returns sort variable IDX within CO.  An IDX of 0 returns the
   primary sort key (the one added first), an IDX of 1 returns
   the secondary sort key, and so on.  IDX must be less than the
   number of sort variables. */
const struct variable *
case_ordering_get_var (const struct case_ordering *co, size_t idx)
{
  assert (idx < co->key_cnt);
  return co->keys[idx].var;
}

/* Returns the sort direction for sort variable IDX within CO. */
enum sort_direction
case_ordering_get_direction (const struct case_ordering *co, size_t idx)
{
  assert (idx < co->key_cnt);
  return co->keys[idx].dir;
}

/* Stores an array listing all of the variables used for sorting
   within CO into *VARS and the number of variables into
   *VAR_CNT.  The caller is responsible for freeing *VARS when it
   is no longer needed. */
void
case_ordering_get_vars (const struct case_ordering *co,
                        const struct variable ***vars, size_t *var_cnt)
{
  size_t i;

  *var_cnt = co->key_cnt;
  *vars = xnmalloc (*var_cnt, sizeof **vars);
  for (i = 0; i < co->key_cnt; i++)
    (*vars)[i] = co->keys[i].var;
}

