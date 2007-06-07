/* PSPP - computes sample statistics.
   Copyright (C) 2007 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>

#include <data/casegrouper.h>

#include <stdlib.h>

#include <data/case-ordering.h>
#include <data/casereader.h>
#include <data/casewriter.h>
#include <data/dictionary.h>
#include <libpspp/taint.h>

#include "xalloc.h"

struct casegrouper 
  {
    struct casereader *reader;
    struct taint *taint;
    
    bool (*same_group) (const struct ccase *, const struct ccase *, void *aux);
    void (*destroy) (void *aux);
    void *aux;
  };

struct casegrouper *
casegrouper_create_func (struct casereader *reader,
                         bool (*same_group) (const struct ccase *,
                                             const struct ccase *,
                                             void *aux),
                         void (*destroy) (void *aux),
                         void *aux) 
{
  struct casegrouper *grouper = xmalloc (sizeof *grouper);
  grouper->reader = casereader_rename (reader);
  grouper->taint = taint_clone (casereader_get_taint (grouper->reader));
  grouper->same_group = same_group;
  grouper->destroy = destroy;
  grouper->aux = aux;
  return grouper;
}

/* FIXME: we really shouldn't need a temporary casewriter for the
   common case where we read an entire group's data before going
   on to the next. */
bool
casegrouper_get_next_group (struct casegrouper *grouper,
                            struct casereader **reader)
{
  if (grouper->same_group != NULL) 
    {
      struct casewriter *writer;
      struct ccase group_case, tmp;

      if (!casereader_read (grouper->reader, &group_case)) 
        {
          *reader = NULL;
          return false; 
        }

      writer = autopaging_writer_create (casereader_get_value_cnt (grouper->reader));
      case_clone (&tmp, &group_case);
      casewriter_write (writer, &tmp);

      while (casereader_peek (grouper->reader, 0, &tmp)
             && grouper->same_group (&group_case, &tmp, grouper->aux))
        {
          struct ccase discard;
          casereader_read (grouper->reader, &discard);
          case_destroy (&discard);
          casewriter_write (writer, &tmp); 
        }
      case_destroy (&tmp);
      case_destroy (&group_case);

      *reader = casewriter_make_reader (writer);
      return true;
    }
  else 
    {
      if (grouper->reader != NULL) 
        {
          *reader = grouper->reader;
          grouper->reader = NULL;
          return true;
        }
      else
        return false;
    } 
}

bool
casegrouper_destroy (struct casegrouper *grouper) 
{
  if (grouper != NULL) 
    {
      struct taint *taint = grouper->taint;
      bool ok;

      casereader_destroy (grouper->reader);
      if (grouper->destroy != NULL)
        grouper->destroy (grouper->aux);
      free (grouper);

      ok = !taint_has_tainted_successor (taint);
      taint_destroy (taint);
      return ok;
    }
  else
    return true;
}

struct casegrouper_vars 
  {
    struct variable **vars;
    size_t var_cnt;
  };

static bool
casegrouper_vars_same_group (const struct ccase *a, const struct ccase *b,
                             void *cv_) 
{
  struct casegrouper_vars *cv = cv_;
  return case_compare (a, b, cv->vars, cv->var_cnt) == 0;
}

static void
casegrouper_vars_destroy (void *cv_) 
{
  struct casegrouper_vars *cv = cv_;
  free (cv->vars);
  free (cv);
}

struct casegrouper *
casegrouper_create_vars (struct casereader *reader,
                         struct variable *const *vars,
                         size_t var_cnt) 
{
  if (var_cnt > 0) 
    {
      struct casegrouper_vars *cv = xmalloc (sizeof *cv);
      cv->vars = xmemdup (vars, sizeof *vars * var_cnt);
      cv->var_cnt = var_cnt;
      return casegrouper_create_func (reader,
                                      casegrouper_vars_same_group,
                                      casegrouper_vars_destroy,
                                      cv);
    }
  else
    return casegrouper_create_func (reader, NULL, NULL, NULL);
}

struct casegrouper *
casegrouper_create_splits (struct casereader *reader,
                           const struct dictionary *dict) 
{
  return casegrouper_create_vars (reader,
                                  dict_get_split_vars (dict),
                                  dict_get_split_cnt (dict));
}

struct casegrouper *
casegrouper_create_case_ordering (struct casereader *reader,
                                  const struct case_ordering *co) 
{
  struct variable **vars;
  size_t var_cnt;
  struct casegrouper *grouper;

  case_ordering_get_vars (co, &vars, &var_cnt);
  grouper = casegrouper_create_vars (reader, vars, var_cnt);
  free (vars);

  return grouper;
}
