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

/* A casegrouper. */
struct casegrouper
  {
    struct casereader *reader;  /* Source of input cases. */
    struct taint *taint;        /* Error status for casegrouper. */

    /* Functions for grouping cases. */
    bool (*same_group) (const struct ccase *, const struct ccase *, void *aux);
    void (*destroy) (void *aux);
    void *aux;
  };

/* Creates and returns a new casegrouper that takes its input
   from READER.  SAME_GROUP is used to decide which cases are in
   a group: it returns true if the pair of cases provided are in
   the same group, false otherwise.  DESTROY will be called when
   the casegrouper is destroyed and should free any storage
   needed by SAME_GROUP.

   SAME_GROUP may be a null pointer.  If so, READER's entire
   contents is considered to be a single group. */
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

/* Obtains the next group of cases from GROUPER.  Returns true if
   successful, false if no groups remain.  If successful, *READER
   is set to the casereader for the new group; otherwise, it is
   set to NULL. */
bool
casegrouper_get_next_group (struct casegrouper *grouper,
                            struct casereader **reader)
{
  /* FIXME: we really shouldn't need a temporary casewriter for
     the common case where we read an entire group's data before
     going on to the next. */
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
        {
          *reader = NULL;
          return false;
        }
    }
}

/* Destroys GROUPER.  Returns false if GROUPER's input casereader
   or any state derived from it had become tainted, which means
   that an I/O error or other serious error occurred in
   processing data derived from GROUPER; otherwise, return true. */
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

/* Casegrouper based on equal values of variables from case to
   case. */

/* Casegrouper based on equal variables. */
struct casegrouper_vars
  {
    const struct variable **vars; /* Variables to compare. */
    size_t var_cnt;               /* Number of variables. */
  };

static bool casegrouper_vars_same_group (const struct ccase *,
                                         const struct ccase *,
                                         void *);
static void casegrouper_vars_destroy (void *);

/* Creates and returns a casegrouper that reads data from READER
   and breaks it into contiguous groups of cases that have equal
   values for the VAR_CNT variables in VARS.  If VAR_CNT is 0,
   then all the cases will be put in a single group. */
struct casegrouper *
casegrouper_create_vars (struct casereader *reader,
                         const struct variable *const *vars,
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

/* Creates and returns a casegrouper that reads data from READER
   and breaks it into contiguous groups of cases that have equal
   values for the SPLIT FILE variables in DICT.  If DICT has no
   SPLIT FILE variables, then all the cases will be put into a
   single group. */
struct casegrouper *
casegrouper_create_splits (struct casereader *reader,
                           const struct dictionary *dict)
{
  return casegrouper_create_vars (reader,
                                  dict_get_split_vars (dict),
                                  dict_get_split_cnt (dict));
}

/* Creates and returns a casegrouper that reads data from READER
   and breaks it into contiguous groups of cases that have equal
   values for the variables used for sorting in CO.  If CO is
   empty (contains no sort keys), then all the cases will be put
   into a single group. */
struct casegrouper *
casegrouper_create_case_ordering (struct casereader *reader,
                                  const struct case_ordering *co)
{
  const struct variable **vars;
  size_t var_cnt;
  struct casegrouper *grouper;

  case_ordering_get_vars (co, &vars, &var_cnt);
  grouper = casegrouper_create_vars (reader, vars, var_cnt);
  free (vars);

  return grouper;
}

/* "same_group" function for an equal-variables casegrouper. */
static bool
casegrouper_vars_same_group (const struct ccase *a, const struct ccase *b,
                             void *cv_)
{
  struct casegrouper_vars *cv = cv_;
  return case_compare (a, b, cv->vars, cv->var_cnt) == 0;
}

/* "destroy" for an equal-variables casegrouper. */
static void
casegrouper_vars_destroy (void *cv_)
{
  struct casegrouper_vars *cv = cv_;
  free (cv->vars);
  free (cv);
}

