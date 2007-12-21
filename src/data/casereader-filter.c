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

#include <data/casereader.h>

#include <stdlib.h>

#include <data/casereader-provider.h>
#include <data/casewriter.h>
#include <data/variable.h>
#include <data/dictionary.h>
#include <libpspp/taint.h>
#include <libpspp/message.h>

#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* A casereader that filters data coming from another
   casereader. */
struct casereader_filter
  {
    struct casereader *subreader; /* The reader to filter. */
    bool (*include) (const struct ccase *, void *aux);
    bool (*destroy) (void *aux);
    void *aux;
    struct casewriter *exclude; /* Writer that gets filtered cases, or NULL. */
  };

static const struct casereader_class casereader_filter_class;

/* Creates and returns a casereader whose content is a filtered
   version of the data in SUBREADER.  Only the cases for which
   INCLUDE returns true will appear in the returned casereader,
   in the original order.

   If EXCLUDE is non-null, then cases for which INCLUDE returns
   false are written to EXCLUDE.  These cases will not
   necessarily be fully written to EXCLUDE until the filtering casereader's
   cases have been fully read or, if that never occurs, until the
   filtering casereader is destroyed.

   When the filtering casereader is destroyed, DESTROY will be
   called to allow any state maintained by INCLUDE to be freed.

   After this function is called, SUBREADER must not ever again
   be referenced directly.  It will be destroyed automatically
   when the filtering casereader is destroyed. */
struct casereader *
casereader_create_filter_func (struct casereader *subreader,
                               bool (*include) (const struct ccase *,
                                                void *aux),
                               bool (*destroy) (void *aux),
                               void *aux,
                               struct casewriter *exclude)
{
  struct casereader_filter *filter = xmalloc (sizeof *filter);
  struct casereader *reader;
  filter->subreader = casereader_rename (subreader);
  filter->include = include;
  filter->destroy = destroy;
  filter->aux = aux;
  filter->exclude = exclude;
  reader = casereader_create_sequential (
    NULL, casereader_get_value_cnt (filter->subreader), CASENUMBER_MAX,
    &casereader_filter_class, filter);
  taint_propagate (casereader_get_taint (filter->subreader),
                   casereader_get_taint (reader));
  return reader;
}

/* Internal read function for filtering casereader. */
static bool
casereader_filter_read (struct casereader *reader UNUSED, void *filter_,
                        struct ccase *c)

{
  struct casereader_filter *filter = filter_;
  for (;;)
    {
      if (!casereader_read (filter->subreader, c))
        return false;
      else if (filter->include (c, filter->aux))
        return true;
      else if (filter->exclude != NULL)
        casewriter_write (filter->exclude, c);
      else
        case_destroy (c);
    }
}

/* Internal destruction function for filtering casereader. */
static void
casereader_filter_destroy (struct casereader *reader, void *filter_)
{
  struct casereader_filter *filter = filter_;

  /* Make sure we've written everything to the excluded cases
     casewriter, if there is one. */
  if (filter->exclude != NULL)
    {
      struct ccase c;
      while (casereader_read (filter->subreader, &c))
        if (filter->include (&c, filter->aux))
          case_destroy (&c);
        else
          casewriter_write (filter->exclude, &c);
    }

  casereader_destroy (filter->subreader);
  if (filter->destroy != NULL && !filter->destroy (filter->aux))
    casereader_force_error (reader);
  free (filter);
}

/* Filtering casereader class. */
static const struct casereader_class casereader_filter_class =
  {
    casereader_filter_read,
    casereader_filter_destroy,

    /* We could in fact delegate clone to the subreader, if the
       filter function is required to have no memory and if we
       added reference counting.  But it might be useful to have
       filter functions with memory and in any case this would
       require a little extra work. */
    NULL,
    NULL,
  };


/* Casereader for filtering valid weights. */

/* Weight-filtering data. */
struct casereader_filter_weight
  {
    const struct variable *weight_var; /* Weight variable. */
    bool *warn_on_invalid;      /* Have we already issued an error? */
    bool local_warn_on_invalid; /* warn_on_invalid might point here. */
  };

static bool casereader_filter_weight_include (const struct ccase *, void *);
static bool casereader_filter_weight_destroy (void *);

/* Creates and returns a casereader that filters cases from
   READER by valid weights, that is, any cases with user- or
   system-missing, zero, or negative weights are dropped.  The
   weight variable's information is taken from DICT.  If DICT
   does not have a weight variable, then no cases are filtered
   out.

   When a case with an invalid weight is encountered,
   *WARN_ON_INVALID is checked.  If it is true, then an error
   message is issued and *WARN_ON_INVALID is set false.  If
   WARN_ON_INVALID is a null pointer, then an internal bool that
   is initially true is used instead of a caller-supplied bool.

   If EXCLUDE is non-null, then dropped cases are written to
   EXCLUDE.  These cases will not necessarily be fully written to
   EXCLUDE until the filtering casereader's cases have been fully
   read or, if that never occurs, until the filtering casereader
   is destroyed.

   After this function is called, READER must not ever again be
   referenced directly.  It will be destroyed automatically when
   the filtering casereader is destroyed. */
struct casereader *
casereader_create_filter_weight (struct casereader *reader,
                                 const struct dictionary *dict,
                                 bool *warn_on_invalid,
                                 struct casewriter *exclude)
{
  struct variable *weight_var = dict_get_weight (dict);
  if (weight_var != NULL)
    {
      struct casereader_filter_weight *cfw = xmalloc (sizeof *cfw);
      cfw->weight_var = weight_var;
      cfw->warn_on_invalid = (warn_on_invalid
                               ? warn_on_invalid
                               : &cfw->local_warn_on_invalid);
      cfw->local_warn_on_invalid = true;
      reader = casereader_create_filter_func (reader,
                                              casereader_filter_weight_include,
                                              casereader_filter_weight_destroy,
                                              cfw, exclude);
    }
  else
    reader = casereader_rename (reader);
  return reader;
}

/* Internal "include" function for weight-filtering
   casereader. */
static bool
casereader_filter_weight_include (const struct ccase *c, void *cfw_)
{
  struct casereader_filter_weight *cfw = cfw_;
  double value = case_num (c, cfw->weight_var);
  if (value >= 0.0 && !var_is_num_missing (cfw->weight_var, value, MV_ANY))
    return true;
  else
    {
      if (*cfw->warn_on_invalid)
        {
	  msg (SW, _("At least one case in the data read had a weight value "
		     "that was user-missing, system-missing, zero, or "
		     "negative.  These case(s) were ignored."));
          *cfw->warn_on_invalid = false;
        }
      return false;
    }
}

/* Internal "destroy" function for weight-filtering
   casereader. */
static bool
casereader_filter_weight_destroy (void *cfw_)
{
  struct casereader_filter_weight *cfw = cfw_;
  free (cfw);
  return true;
}

/* Casereader for filtering missing values. */

/* Missing-value filtering data. */
struct casereader_filter_missing
  {
    struct variable **vars;     /* Variables whose values to filter. */
    size_t var_cnt;             /* Number of variables. */
    enum mv_class class;        /* Types of missing values to filter. */
  };

static bool casereader_filter_missing_include (const struct ccase *, void *);
static bool casereader_filter_missing_destroy (void *);

/* Creates and returns a casereader that filters out cases from
   READER that have a missing value in the given CLASS for any of
   the VAR_CNT variables in VARS.  Only cases that have
   non-missing values for all of these variables are passed
   through.

   Ownership of VARS is retained by the caller.

   If EXCLUDE is non-null, then dropped cases are written to
   EXCLUDE.  These cases will not necessarily be fully written to
   EXCLUDE until the filtering casereader's cases have been fully
   read or, if that never occurs, until the filtering casereader
   is destroyed.

   After this function is called, READER must not ever again
   be referenced directly.  It will be destroyed automatically
   when the filtering casereader is destroyed. */
struct casereader *
casereader_create_filter_missing (struct casereader *reader,
                                  const struct variable **vars, size_t var_cnt,
                                  enum mv_class class,
                                  struct casewriter *exclude)
{
  if (var_cnt > 0 && class != MV_NEVER)
    {
      struct casereader_filter_missing *cfm = xmalloc (sizeof *cfm);
      cfm->vars = xmemdup (vars, sizeof *vars * var_cnt);
      cfm->var_cnt = var_cnt;
      cfm->class = class;
      return casereader_create_filter_func (reader,
                                            casereader_filter_missing_include,
                                            casereader_filter_missing_destroy,
                                            cfm,
                                            exclude);
    }
  else
    return casereader_rename (reader);
}

/* Internal "include" function for missing value-filtering
   casereader. */
static bool
casereader_filter_missing_include (const struct ccase *c, void *cfm_)
{
  const struct casereader_filter_missing *cfm = cfm_;
  size_t i;

  for (i = 0; i < cfm->var_cnt; i++)
    {
      struct variable *var = cfm->vars[i];
      const union value *value = case_data (c, var);
      if (var_is_value_missing (var, value, cfm->class))
        return false;
    }
  return true;
}

/* Internal "destroy" function for missing value-filtering
   casereader. */
static bool
casereader_filter_missing_destroy (void *cfm_)
{
  struct casereader_filter_missing *cfm = cfm_;
  free (cfm->vars);
  free (cfm);
  return true;
}

/* Case-counting casereader. */

static bool casereader_counter_include (const struct ccase *, void *);

/* Creates and returns a new casereader that counts the number of
   cases that have been read from it.  *COUNTER is initially set
   to INITIAL_VALUE, then incremented by 1 each time a case is read.

   Counting casereaders must be used very cautiously: if a
   counting casereader is cloned or if the casereader_peek
   function is used on it, then the counter's value can be higher
   than expected because of the buffering that goes on behind the
   scenes.

   The counter is only incremented as cases are actually read
   from the casereader.  In particular, if the casereader is
   destroyed before all cases have been read from the casereader,
   cases never read will not be included in the count.

   After this function is called, READER must not ever again
   be referenced directly.  It will be destroyed automatically
   when the filtering casereader is destroyed. */
struct casereader *
casereader_create_counter (struct casereader *reader, casenumber *counter,
                           casenumber initial_value)
{
  *counter = initial_value;
  return casereader_create_filter_func (reader, casereader_counter_include,
                                        NULL, counter, NULL);
}

/* Internal "include" function for counting casereader. */
static bool
casereader_counter_include (const struct ccase *c UNUSED, void *counter_)
{
  casenumber *counter = counter_;
  ++*counter;
  return true;
}
