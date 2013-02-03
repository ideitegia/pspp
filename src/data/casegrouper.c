/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2011, 2013 Free Software Foundation, Inc.

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

#include "data/casegrouper.h"

#include <stdlib.h>

#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/dictionary.h"
#include "data/subcase.h"
#include "libpspp/taint.h"

#include "gl/xalloc.h"

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

   Takes ownerhip of READER.

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
      struct ccase *group_case, *tmp;

      group_case = casereader_read (grouper->reader);
      if (group_case == NULL)
        {
          *reader = NULL;
          return false;
        }

      writer = autopaging_writer_create (
        casereader_get_proto (grouper->reader));

      casewriter_write (writer, case_ref (group_case));

      while ((tmp = casereader_peek (grouper->reader, 0)) != NULL
             && grouper->same_group (group_case, tmp, grouper->aux))
        {
          case_unref (casereader_read (grouper->reader));
          casewriter_write (writer, tmp);
        }
      case_unref (tmp);
      case_unref (group_case);

      *reader = casewriter_make_reader (writer);
      return true;
    }
  else
    {
      if (grouper->reader != NULL)
        {
          if (!casereader_is_empty (grouper->reader))
            {
              *reader = grouper->reader;
              grouper->reader = NULL;
              return true;
            }
          else
            {
              casereader_destroy (grouper->reader);
              grouper->reader = NULL;
              return false;
            }
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

static bool casegrouper_vars_same_group (const struct ccase *,
                                         const struct ccase *,
                                         void *);
static void casegrouper_vars_destroy (void *);

/* Creates and returns a casegrouper that reads data from READER
   and breaks it into contiguous groups of cases that have equal
   values for the VAR_CNT variables in VARS.  If VAR_CNT is 0,
   then all the cases will be put in a single group.

   Takes ownerhip of READER. */
struct casegrouper *
casegrouper_create_vars (struct casereader *reader,
                         const struct variable *const *vars,
                         size_t var_cnt)
{
  if (var_cnt > 0) 
    {
      struct subcase *sc = xmalloc (sizeof *sc);
      subcase_init_vars (sc, vars, var_cnt);
      return casegrouper_create_func (reader, casegrouper_vars_same_group,
                                      casegrouper_vars_destroy, sc); 
    }
  else
    return casegrouper_create_func (reader, NULL, NULL, NULL);
}

/* Creates and returns a casegrouper that reads data from READER
   and breaks it into contiguous groups of cases that have equal
   values for the SPLIT FILE variables in DICT.  If DICT has no
   SPLIT FILE variables, then all the cases will be put into a
   single group.

   Takes ownerhip of READER. */
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
   values for the variables used for sorting in SC.  If SC is
   empty (contains no fields), then all the cases will be put
   into a single group.

   Takes ownerhip of READER. */
struct casegrouper *
casegrouper_create_subcase (struct casereader *reader,
                            const struct subcase *sc)
{
  if (subcase_get_n_fields (sc) > 0) 
    {
      struct subcase *sc_copy = xmalloc (sizeof *sc);
      subcase_clone (sc_copy, sc);
      return casegrouper_create_func (reader, casegrouper_vars_same_group,
                                      casegrouper_vars_destroy, sc_copy); 
    }
  else
    return casegrouper_create_func (reader, NULL, NULL, NULL);
}

/* "same_group" function for an equal-variables casegrouper. */
static bool
casegrouper_vars_same_group (const struct ccase *a, const struct ccase *b,
                             void *sc_)
{
  struct subcase *sc = sc_;
  return subcase_equal (sc, a, sc, b);
}

/* "destroy" for an equal-variables casegrouper. */
static void
casegrouper_vars_destroy (void *sc_)
{
  struct subcase *sc = sc_;
  if (sc != NULL) 
    {
      subcase_destroy (sc);
      free (sc); 
    }
}
