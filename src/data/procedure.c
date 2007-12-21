/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007 Free Software Foundation, Inc.

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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <data/case.h>
#include <data/case-map.h>
#include <data/caseinit.h>
#include <data/casereader.h>
#include <data/casereader-provider.h>
#include <data/casewriter.h>
#include <data/dictionary.h>
#include <data/file-handle-def.h>
#include <data/procedure.h>
#include <data/transformations.h>
#include <data/variable.h>
#include <libpspp/deque.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>
#include <libpspp/taint.h>

#include "xalloc.h"

struct dataset {
  /* Cases are read from source,
     their transformation variables are initialized,
     pass through permanent_trns_chain (which transforms them into
     the format described by permanent_dict),
     are written to sink,
     pass through temporary_trns_chain (which transforms them into
     the format described by dict),
     and are finally passed to the procedure. */
  struct casereader *source;
  struct caseinit *caseinit;
  struct trns_chain *permanent_trns_chain;
  struct dictionary *permanent_dict;
  struct casewriter *sink;
  struct trns_chain *temporary_trns_chain;
  struct dictionary *dict;

  /* Callback which occurs whenever the transformation chain(s) have
     been modified */
  transformation_change_callback_func *xform_callback;
  void *xform_callback_aux;

  /* If true, cases are discarded instead of being written to
     sink. */
  bool discard_output;

  /* The transformation chain that the next transformation will be
     added to. */
  struct trns_chain *cur_trns_chain;

  /* The case map used to compact a case, if necessary;
     otherwise a null pointer. */
  struct case_map *compactor;

  /* Time at which proc was last invoked. */
  time_t last_proc_invocation;

  /* Cases just before ("lagging") the current one. */
  int n_lag;			/* Number of cases to lag. */
  struct deque lag;             /* Deque of lagged cases. */
  struct ccase *lag_cases;      /* Lagged cases managed by deque. */

  /* Procedure data. */
  enum
    {
      PROC_COMMITTED,           /* No procedure in progress. */
      PROC_OPEN,                /* proc_open called, casereader still open. */
      PROC_CLOSED               /* casereader from proc_open destroyed,
                                   but proc_commit not yet called. */
    }
  proc_state;
  casenumber cases_written;       /* Cases output so far. */
  bool ok;                    /* Error status. */
}; /* struct dataset */


static void add_case_limit_trns (struct dataset *ds);
static void add_filter_trns (struct dataset *ds);

static void update_last_proc_invocation (struct dataset *ds);

/* Public functions. */

/* Returns the last time the data was read. */
time_t
time_of_last_procedure (struct dataset *ds)
{
  if (ds->last_proc_invocation == 0)
    update_last_proc_invocation (ds);
  return ds->last_proc_invocation;
}

/* Regular procedure. */

/* Executes any pending transformations, if necessary.
   This is not identical to the EXECUTE command in that it won't
   always read the source data.  This can be important when the
   source data is given inline within BEGIN DATA...END FILE. */
bool
proc_execute (struct dataset *ds)
{
  bool ok;

  if ((ds->temporary_trns_chain == NULL
       || trns_chain_is_empty (ds->temporary_trns_chain))
      && trns_chain_is_empty (ds->permanent_trns_chain))
    {
      ds->n_lag = 0;
      ds->discard_output = false;
      dict_set_case_limit (ds->dict, 0);
      dict_clear_vectors (ds->dict);
      return true;
    }

  ok = casereader_destroy (proc_open (ds));
  return proc_commit (ds) && ok;
}

static const struct casereader_class proc_casereader_class;

/* Opens dataset DS for reading cases with proc_read.
   proc_commit must be called when done. */
struct casereader *
proc_open (struct dataset *ds)
{
  assert (ds->source != NULL);
  assert (ds->proc_state == PROC_COMMITTED);

  update_last_proc_invocation (ds);

  caseinit_mark_for_init (ds->caseinit, ds->dict);

  /* Finish up the collection of transformations. */
  add_case_limit_trns (ds);
  add_filter_trns (ds);
  trns_chain_finalize (ds->cur_trns_chain);

  /* Make permanent_dict refer to the dictionary right before
     data reaches the sink. */
  if (ds->permanent_dict == NULL)
    ds->permanent_dict = ds->dict;

  /* Prepare sink. */
  if (!ds->discard_output)
    {
      struct dictionary *pd = ds->permanent_dict;
      size_t compacted_value_cnt = dict_count_values (pd, 1u << DC_SCRATCH);
      bool should_compact = compacted_value_cnt < dict_get_next_value_idx (pd);
      ds->compactor = (should_compact
                       ? case_map_to_compact_dict (pd, 1u << DC_SCRATCH)
                       : NULL);
      ds->sink = autopaging_writer_create (compacted_value_cnt);
    }
  else
    {
      ds->compactor = NULL;
      ds->sink = NULL;
    }

  /* Allocate memory for lagged cases. */
  ds->lag_cases = deque_init (&ds->lag, ds->n_lag, sizeof *ds->lag_cases);

  ds->proc_state = PROC_OPEN;
  ds->cases_written = 0;
  ds->ok = true;

  /* FIXME: use taint in dataset in place of `ok'? */
  /* FIXME: for trivial cases we can just return a clone of
     ds->source? */
  return casereader_create_sequential (NULL,
                                       dict_get_next_value_idx (ds->dict),
                                       CASENUMBER_MAX,
                                       &proc_casereader_class, ds);
}

/* Returns true if a procedure is in progress, that is, if
   proc_open has been called but proc_commit has not. */
bool
proc_is_open (const struct dataset *ds)
{
  return ds->proc_state != PROC_COMMITTED;
}

/* "read" function for procedure casereader. */
static bool
proc_casereader_read (struct casereader *reader UNUSED, void *ds_,
                      struct ccase *c)
{
  struct dataset *ds = ds_;
  enum trns_result retval = TRNS_DROP_CASE;

  assert (ds->proc_state == PROC_OPEN);
  for (;;)
    {
      casenumber case_nr;

      assert (retval == TRNS_DROP_CASE || retval == TRNS_ERROR);
      if (retval == TRNS_ERROR)
        ds->ok = false;
      if (!ds->ok)
        return false;

      /* Read a case from source. */
      if (!casereader_read (ds->source, c))
        return false;
      case_resize (c, dict_get_next_value_idx (ds->dict));
      caseinit_init_vars (ds->caseinit, c);

      /* Execute permanent transformations.  */
      case_nr = ds->cases_written + 1;
      retval = trns_chain_execute (ds->permanent_trns_chain, TRNS_CONTINUE,
                                   c, case_nr);
      caseinit_update_left_vars (ds->caseinit, c);
      if (retval != TRNS_CONTINUE)
        {
          case_destroy (c);
          continue;
        }

      /* Write case to collection of lagged cases. */
      if (ds->n_lag > 0)
        {
          while (deque_count (&ds->lag) >= ds->n_lag)
            case_destroy (&ds->lag_cases[deque_pop_back (&ds->lag)]);
          case_clone (&ds->lag_cases[deque_push_front (&ds->lag)], c);
        }

      /* Write case to replacement active file. */
      ds->cases_written++;
      if (ds->sink != NULL)
        {
          struct ccase tmp;
          if (ds->compactor != NULL)
            case_map_execute (ds->compactor, c, &tmp);
          else
            case_clone (&tmp, c);
          casewriter_write (ds->sink, &tmp);
        }

      /* Execute temporary transformations. */
      if (ds->temporary_trns_chain != NULL)
        {
          retval = trns_chain_execute (ds->temporary_trns_chain, TRNS_CONTINUE,
                                       c, ds->cases_written);
          if (retval != TRNS_CONTINUE)
            {
              case_destroy (c);
              continue;
            }
        }

      return true;
    }
}

/* "destroy" function for procedure casereader. */
static void
proc_casereader_destroy (struct casereader *reader, void *ds_)
{
  struct dataset *ds = ds_;
  struct ccase c;

  /* Make sure transformations happen for every input case, in
     case they have side effects, and ensure that the replacement
     active file gets all the cases it should. */
  while (casereader_read (reader, &c))
    case_destroy (&c);

  ds->proc_state = PROC_CLOSED;
  ds->ok = casereader_destroy (ds->source) && ds->ok;
  ds->source = NULL;
  proc_set_active_file_data (ds, NULL);
}

/* Must return false if the source casereader, a transformation,
   or the sink casewriter signaled an error.  (If a temporary
   transformation signals an error, then the return value is
   false, but the replacement active file may still be
   untainted.) */
bool
proc_commit (struct dataset *ds)
{
  assert (ds->proc_state == PROC_CLOSED);
  ds->proc_state = PROC_COMMITTED;

  /* Free memory for lagged cases. */
  while (!deque_is_empty (&ds->lag))
    case_destroy (&ds->lag_cases[deque_pop_back (&ds->lag)]);
  free (ds->lag_cases);

  /* Dictionary from before TEMPORARY becomes permanent. */
  proc_cancel_temporary_transformations (ds);

  if (!ds->discard_output)
    {
      /* Finish compacting. */
      if (ds->compactor != NULL)
        {
          case_map_destroy (ds->compactor);
          ds->compactor = NULL;

          dict_delete_scratch_vars (ds->dict);
          dict_compact_values (ds->dict);
        }

      /* Old data sink becomes new data source. */
      if (ds->sink != NULL)
        ds->source = casewriter_make_reader (ds->sink);
    }
  else
    {
      ds->source = NULL;
      ds->discard_output = false;
    }
  ds->sink = NULL;

  caseinit_clear (ds->caseinit);
  caseinit_mark_as_preinited (ds->caseinit, ds->dict);

  dict_clear_vectors (ds->dict);
  ds->permanent_dict = NULL;
  return proc_cancel_all_transformations (ds) && ds->ok;
}

/* Casereader class for procedure execution. */
static const struct casereader_class proc_casereader_class =
  {
    proc_casereader_read,
    proc_casereader_destroy,
    NULL,
    NULL,
  };

/* Updates last_proc_invocation. */
static void
update_last_proc_invocation (struct dataset *ds)
{
  ds->last_proc_invocation = time (NULL);
}

/* Returns a pointer to the lagged case from N_BEFORE cases before the
   current one, or NULL if there haven't been that many cases yet. */
struct ccase *
lagged_case (const struct dataset *ds, int n_before)
{
  assert (n_before >= 1);
  assert (n_before <= ds->n_lag);

  if (n_before <= deque_count (&ds->lag))
    return &ds->lag_cases[deque_front (&ds->lag, n_before - 1)];
  else
    return NULL;
}

/* Returns the current set of permanent transformations,
   and clears the permanent transformations.
   For use by INPUT PROGRAM. */
struct trns_chain *
proc_capture_transformations (struct dataset *ds)
{
  struct trns_chain *chain;

  assert (ds->temporary_trns_chain == NULL);
  chain = ds->permanent_trns_chain;
  ds->cur_trns_chain = ds->permanent_trns_chain = trns_chain_create ();

  if ( ds->xform_callback)
    ds->xform_callback (false, ds->xform_callback_aux);

  return chain;
}

/* Adds a transformation that processes a case with PROC and
   frees itself with FREE to the current set of transformations.
   The functions are passed AUX as auxiliary data. */
void
add_transformation (struct dataset *ds, trns_proc_func *proc, trns_free_func *free, void *aux)
{
  trns_chain_append (ds->cur_trns_chain, NULL, proc, free, aux);
  if ( ds->xform_callback)
    ds->xform_callback (true, ds->xform_callback_aux);
}

/* Adds a transformation that processes a case with PROC and
   frees itself with FREE to the current set of transformations.
   When parsing of the block of transformations is complete,
   FINALIZE will be called.
   The functions are passed AUX as auxiliary data. */
void
add_transformation_with_finalizer (struct dataset *ds,
				   trns_finalize_func *finalize,
                                   trns_proc_func *proc,
                                   trns_free_func *free, void *aux)
{
  trns_chain_append (ds->cur_trns_chain, finalize, proc, free, aux);

  if ( ds->xform_callback)
    ds->xform_callback (true, ds->xform_callback_aux);
}

/* Returns the index of the next transformation.
   This value can be returned by a transformation procedure
   function to indicate a "jump" to that transformation. */
size_t
next_transformation (const struct dataset *ds)
{
  return trns_chain_next (ds->cur_trns_chain);
}

/* Returns true if the next call to add_transformation() will add
   a temporary transformation, false if it will add a permanent
   transformation. */
bool
proc_in_temporary_transformations (const struct dataset *ds)
{
  return ds->temporary_trns_chain != NULL;
}

/* Marks the start of temporary transformations.
   Further calls to add_transformation() will add temporary
   transformations. */
void
proc_start_temporary_transformations (struct dataset *ds)
{
  if (!proc_in_temporary_transformations (ds))
    {
      add_case_limit_trns (ds);

      ds->permanent_dict = dict_clone (ds->dict);

      trns_chain_finalize (ds->permanent_trns_chain);
      ds->temporary_trns_chain = ds->cur_trns_chain = trns_chain_create ();

      if ( ds->xform_callback)
	ds->xform_callback (true, ds->xform_callback_aux);
    }
}

/* Converts all the temporary transformations, if any, to
   permanent transformations.  Further transformations will be
   permanent.
   Returns true if anything changed, false otherwise. */
bool
proc_make_temporary_transformations_permanent (struct dataset *ds)
{
  if (proc_in_temporary_transformations (ds))
    {
      trns_chain_finalize (ds->temporary_trns_chain);
      trns_chain_splice (ds->permanent_trns_chain, ds->temporary_trns_chain);
      ds->temporary_trns_chain = NULL;

      dict_destroy (ds->permanent_dict);
      ds->permanent_dict = NULL;

      return true;
    }
  else
    return false;
}

/* Cancels all temporary transformations, if any.  Further
   transformations will be permanent.
   Returns true if anything changed, false otherwise. */
bool
proc_cancel_temporary_transformations (struct dataset *ds)
{
  if (proc_in_temporary_transformations (ds))
    {
      dict_destroy (ds->dict);
      ds->dict = ds->permanent_dict;
      ds->permanent_dict = NULL;

      trns_chain_destroy (ds->temporary_trns_chain);
      ds->temporary_trns_chain = NULL;

      if ( ds->xform_callback)
	ds->xform_callback (!trns_chain_is_empty (ds->permanent_trns_chain),
			    ds->xform_callback_aux);

      return true;
    }
  else
    return false;
}

/* Cancels all transformations, if any.
   Returns true if successful, false on I/O error. */
bool
proc_cancel_all_transformations (struct dataset *ds)
{
  bool ok;
  assert (ds->proc_state == PROC_COMMITTED);
  ok = trns_chain_destroy (ds->permanent_trns_chain);
  ok = trns_chain_destroy (ds->temporary_trns_chain) && ok;
  ds->permanent_trns_chain = ds->cur_trns_chain = trns_chain_create ();
  ds->temporary_trns_chain = NULL;
  if ( ds->xform_callback)
    ds->xform_callback (false, ds->xform_callback_aux);

  return ok;
}

/* Initializes procedure handling. */
struct dataset *
create_dataset (void)
{
  struct dataset *ds = xzalloc (sizeof(*ds));
  ds->dict = dict_create ();
  ds->caseinit = caseinit_create ();
  proc_cancel_all_transformations (ds);
  return ds;
}


void
dataset_add_transform_change_callback (struct dataset *ds,
				       transformation_change_callback_func *cb,
				       void *aux)
{
  ds->xform_callback = cb;
  ds->xform_callback_aux = aux;
}

/* Finishes up procedure handling. */
void
destroy_dataset (struct dataset *ds)
{
  proc_discard_active_file (ds);
  dict_destroy (ds->dict);
  caseinit_destroy (ds->caseinit);
  trns_chain_destroy (ds->permanent_trns_chain);

  if ( ds->xform_callback)
    ds->xform_callback (false, ds->xform_callback_aux);
  free (ds);
}

/* Causes output from the next procedure to be discarded, instead
   of being preserved for use as input for the next procedure. */
void
proc_discard_output (struct dataset *ds)
{
  ds->discard_output = true;
}

/* Discards the active file dictionary, data, and
   transformations. */
void
proc_discard_active_file (struct dataset *ds)
{
  assert (ds->proc_state == PROC_COMMITTED);

  dict_clear (ds->dict);
  fh_set_default_handle (NULL);

  ds->n_lag = 0;

  casereader_destroy (ds->source);
  ds->source = NULL;

  proc_cancel_all_transformations (ds);
}

/* Sets SOURCE as the source for procedure input for the next
   procedure. */
void
proc_set_active_file (struct dataset *ds,
                      struct casereader *source,
                      struct dictionary *dict)
{
  assert (ds->proc_state == PROC_COMMITTED);
  assert (ds->dict != dict);

  proc_discard_active_file (ds);

  dict_destroy (ds->dict);
  ds->dict = dict;

  proc_set_active_file_data (ds, source);
}

/* Replaces the active file's data by READER without replacing
   the associated dictionary. */
bool
proc_set_active_file_data (struct dataset *ds, struct casereader *reader)
{
  casereader_destroy (ds->source);
  ds->source = reader;

  caseinit_clear (ds->caseinit);
  caseinit_mark_as_preinited (ds->caseinit, ds->dict);

  return reader == NULL || !casereader_error (reader);
}

/* Returns true if an active file data source is available, false
   otherwise. */
bool
proc_has_active_file (const struct dataset *ds)
{
  return ds->source != NULL;
}

/* Returns the active file data source from DS, or a null pointer
   if DS has no data source, and removes it from DS. */
struct casereader *
proc_extract_active_file_data (struct dataset *ds)
{
  struct casereader *reader = ds->source;
  ds->source = NULL;

  return reader;
}

/* Checks whether DS has a corrupted active file.  If so,
   discards it and returns false.  If not, returns true without
   doing anything. */
bool
dataset_end_of_command (struct dataset *ds)
{
  if (ds->source != NULL)
    {
      if (casereader_error (ds->source))
        {
          proc_discard_active_file (ds);
          return false;
        }
      else
        {
          const struct taint *taint = casereader_get_taint (ds->source);
          taint_reset_successor_taint ((struct taint *) taint);
          assert (!taint_has_tainted_successor (taint));
        }
    }
  return true;
}

static trns_proc_func case_limit_trns_proc;
static trns_free_func case_limit_trns_free;

/* Adds a transformation that limits the number of cases that may
   pass through, if DS->DICT has a case limit. */
static void
add_case_limit_trns (struct dataset *ds)
{
  casenumber case_limit = dict_get_case_limit (ds->dict);
  if (case_limit != 0)
    {
      casenumber *cases_remaining = xmalloc (sizeof *cases_remaining);
      *cases_remaining = case_limit;
      add_transformation (ds, case_limit_trns_proc, case_limit_trns_free,
                          cases_remaining);
      dict_set_case_limit (ds->dict, 0);
    }
}

/* Limits the maximum number of cases processed to
   *CASES_REMAINING. */
static int
case_limit_trns_proc (void *cases_remaining_,
                      struct ccase *c UNUSED, casenumber case_nr UNUSED)
{
  size_t *cases_remaining = cases_remaining_;
  if (*cases_remaining > 0)
    {
      (*cases_remaining)--;
      return TRNS_CONTINUE;
    }
  else
    return TRNS_DROP_CASE;
}

/* Frees the data associated with a case limit transformation. */
static bool
case_limit_trns_free (void *cases_remaining_)
{
  size_t *cases_remaining = cases_remaining_;
  free (cases_remaining);
  return true;
}

static trns_proc_func filter_trns_proc;

/* Adds a temporary transformation to filter data according to
   the variable specified on FILTER, if any. */
static void
add_filter_trns (struct dataset *ds)
{
  struct variable *filter_var = dict_get_filter (ds->dict);
  if (filter_var != NULL)
    {
      proc_start_temporary_transformations (ds);
      add_transformation (ds, filter_trns_proc, NULL, filter_var);
    }
}

/* FILTER transformation. */
static int
filter_trns_proc (void *filter_var_,
                  struct ccase *c UNUSED, casenumber case_nr UNUSED)

{
  struct variable *filter_var = filter_var_;
  double f = case_num (c, filter_var);
  return (f != 0.0 && !var_is_num_missing (filter_var, f, MV_ANY)
          ? TRNS_CONTINUE : TRNS_DROP_CASE);
}


struct dictionary *
dataset_dict (const struct dataset *ds)
{
  return ds->dict;
}

const struct casereader *
dataset_source (const struct dataset *ds)
{
  return ds->source;
}

void
dataset_need_lag (struct dataset *ds, int n_before)
{
  ds->n_lag = MAX (ds->n_lag, n_before);
}
