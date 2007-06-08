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

struct casereader_filter 
  {
    struct casereader *subreader;
    bool (*include) (const struct ccase *, void *aux);
    bool (*destroy) (void *aux);
    void *aux;
    struct casewriter *exclude;
  };

static struct casereader_class casereader_filter_class;

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

static void
casereader_filter_destroy (struct casereader *reader, void *filter_) 
{
  struct casereader_filter *filter = filter_;
  casereader_destroy (filter->subreader);
  if (filter->destroy != NULL && !filter->destroy (filter->aux))
    casereader_force_error (reader);
  free (filter);
}

static struct casereader_class casereader_filter_class = 
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

struct casereader_filter_weight 
  {
    const struct variable *weight_var;
    bool *warn_on_invalid;
    bool local_warn_on_invalid;
  };

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

static bool
casereader_filter_weight_destroy (void *cfw_) 
{
  struct casereader_filter_weight *cfw = cfw_;
  free (cfw);
  return true;
}

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

struct casereader_filter_missing 
  {
    struct variable **vars;
    size_t var_cnt;
    enum mv_class class;
  };

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

static bool
casereader_filter_missing_destroy (void *cfm_) 
{
  struct casereader_filter_missing *cfm = cfm_;
  free (cfm->vars);
  free (cfm);
  return true;
}

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


static bool
casereader_counter_include (const struct ccase *c UNUSED, void *counter_) 
{
  casenumber *counter = counter_;
  ++*counter;
  return true;
}

struct casereader *
casereader_create_counter (struct casereader *reader, casenumber *counter,
                           casenumber initial_value) 
{
  *counter = initial_value;
  return casereader_create_filter_func (reader, casereader_counter_include,
                                        NULL, counter, NULL);
}
