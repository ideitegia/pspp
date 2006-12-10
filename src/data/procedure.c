/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <data/case-source.h>
#include <data/case-sink.h>
#include <data/case.h>
#include <data/casefile.h>
#include <data/fastfile.h>
#include <data/dictionary.h>
#include <data/file-handle-def.h>
#include <data/procedure.h>
#include <data/storage-stream.h>
#include <data/transformations.h>
#include <data/variable.h>
#include <libpspp/alloc.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>

/* Procedure execution data. */
struct write_case_data
  {
    /* Function to call for each case. */
    case_func *proc;
    void *aux;

    struct dataset *dataset;    /* The dataset concerned */
    struct ccase trns_case;     /* Case used for transformations. */
    struct ccase sink_case;     /* Case written to sink, if
                                   compacting is necessary. */
    size_t cases_written;       /* Cases output so far. */
  };

struct dataset {
  /* Cases are read from proc_source,
     pass through permanent_trns_chain (which transforms them into
     the format described by permanent_dict),
     are written to proc_sink,
     pass through temporary_trns_chain (which transforms them into
     the format described by dict),
     and are finally passed to the procedure. */
  struct case_source *proc_source;
  struct trns_chain *permanent_trns_chain;
  struct dictionary *permanent_dict;
  struct case_sink *proc_sink;
  struct trns_chain *temporary_trns_chain;
  struct dictionary *dict;

  /* The transformation chain that the next transformation will be
     added to. */
  struct trns_chain *cur_trns_chain;

  /* The compactor used to compact a case, if necessary;
     otherwise a null pointer. */
  struct dict_compactor *compactor;

  /* Time at which proc was last invoked. */
  time_t last_proc_invocation;

  /* Lag queue. */
  int n_lag;			/* Number of cases to lag. */
  int lag_count;		/* Number of cases in lag_queue so far. */
  int lag_head;		/* Index where next case will be added. */
  struct ccase *lag_queue; /* Array of n_lag ccase * elements. */

}; /* struct dataset */


static void add_case_limit_trns (struct dataset *ds);
static void add_filter_trns (struct dataset *ds);

static bool internal_procedure (struct dataset *ds, case_func *,
                                end_func *,
                                void *aux);
static void update_last_proc_invocation (struct dataset *ds);
static void create_trns_case (struct ccase *, struct dictionary *);
static void open_active_file (struct dataset *ds);
static bool write_case (struct write_case_data *wc_data);
static void lag_case (struct dataset *ds, const struct ccase *c);
static void clear_case (const struct dataset *ds, struct ccase *c);
static bool close_active_file (struct dataset *ds);

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



/* Reads the data from the input program and writes it to a new
   active file.  For each case we read from the input program, we
   do the following:

   1. Execute permanent transformations.  If these drop the case,
      start the next case from step 1.

   2. Write case to replacement active file.
   
   3. Execute temporary transformations.  If these drop the case,
      start the next case from step 1.
      
   4. Pass case to PROC_FUNC, passing AUX as auxiliary data.

   Returns true if successful, false if an I/O error occurred. */
bool
procedure (struct dataset *ds, case_func *cf, void *aux)
{
  return internal_procedure (ds, cf, NULL, aux);
}

/* Multipass procedure. */

struct multipass_aux_data 
  {
    struct casefile *casefile;
    
    bool (*proc_func) (const struct casefile *, void *aux);
    void *aux;
  };

/* Case processing function for multipass_procedure(). */
static bool
multipass_case_func (const struct ccase *c, void *aux_data_, const struct dataset *ds UNUSED) 
{
  struct multipass_aux_data *aux_data = aux_data_;
  return casefile_append (aux_data->casefile, c);
}

/* End-of-file function for multipass_procedure(). */
static bool
multipass_end_func (void *aux_data_, const struct dataset *ds UNUSED) 
{
  struct multipass_aux_data *aux_data = aux_data_;
  return (aux_data->proc_func == NULL
          || aux_data->proc_func (aux_data->casefile, aux_data->aux));
}

/* Procedure that allows multiple passes over the input data.
   The entire active file is passed to PROC_FUNC, with the given
   AUX as auxiliary data, as a unit. */
bool
multipass_procedure (struct dataset *ds, casefile_func *proc_func,  void *aux) 
{
  struct multipass_aux_data aux_data;
  bool ok;

  aux_data.casefile = fastfile_create (dict_get_next_value_idx (ds->dict));
  aux_data.proc_func = proc_func;
  aux_data.aux = aux;

  ok = internal_procedure (ds, multipass_case_func, multipass_end_func, &aux_data);
  ok = !casefile_error (aux_data.casefile) && ok;

  casefile_destroy (aux_data.casefile);

  return ok;
}

/* Procedure implementation. */


/* Executes a procedure.
   Passes each case to CASE_FUNC.
   Calls END_FUNC after the last case.
   Returns true if successful, false if an I/O error occurred (or
   if CASE_FUNC or END_FUNC ever returned false). */
static bool
internal_procedure (struct dataset *ds, case_func *proc,
		    end_func *end,
                    void *aux) 
{
  struct write_case_data wc_data;
  bool ok = true;

  assert (ds->proc_source != NULL);

  update_last_proc_invocation (ds);

  /* Optimize the trivial case where we're not going to do
     anything with the data, by not reading the data at all. */
  if (proc == NULL && end == NULL
      && case_source_is_class (ds->proc_source, &storage_source_class)
      && ds->proc_sink == NULL
      && (ds->temporary_trns_chain == NULL
          || trns_chain_is_empty (ds->temporary_trns_chain))
      && trns_chain_is_empty (ds->permanent_trns_chain))
    {
      ds->n_lag = 0;
      dict_set_case_limit (ds->dict, 0);
      dict_clear_vectors (ds->dict);
      return true;
    }
  
  open_active_file (ds);
  
  wc_data.proc = proc;
  wc_data.aux = aux;
  wc_data.dataset = ds;
  create_trns_case (&wc_data.trns_case, ds->dict);
  case_create (&wc_data.sink_case,
               dict_get_compacted_value_cnt (ds->dict));
  wc_data.cases_written = 0;

  ok = ds->proc_source->class->read (ds->proc_source,
                                 &wc_data.trns_case,
                                 write_case, &wc_data) && ok;
  if (end != NULL)
    ok = end (aux, ds) && ok;

  case_destroy (&wc_data.sink_case);
  case_destroy (&wc_data.trns_case);

  ok = close_active_file (ds) && ok;

  return ok;
}

/* Updates last_proc_invocation. */
static void
update_last_proc_invocation (struct dataset *ds) 
{
  ds->last_proc_invocation = time (NULL);
}

/* Creates and returns a case, initializing it from the vectors
   that say which `value's need to be initialized just once, and
   which ones need to be re-initialized before every case. */
static void
create_trns_case (struct ccase *trns_case, struct dictionary *dict)
{
  size_t var_cnt = dict_get_var_cnt (dict);
  size_t i;

  case_create (trns_case, dict_get_next_value_idx (dict));
  for (i = 0; i < var_cnt; i++) 
    {
      struct variable *v = dict_get_var (dict, i);
      union value *value = case_data_rw (trns_case, v);

      if (var_is_numeric (v))
        value->f = var_get_leave (v) ? 0.0 : SYSMIS;
      else
        memset (value->s, ' ', var_get_width (v));
    }
}

/* Makes all preparations for reading from the data source and writing
   to the data sink. */
static void
open_active_file (struct dataset *ds)
{
  add_case_limit_trns (ds);
  add_filter_trns (ds);

  /* Finalize transformations. */
  trns_chain_finalize (ds->cur_trns_chain);

  /* Make permanent_dict refer to the dictionary right before
     data reaches the sink. */
  if (ds->permanent_dict == NULL)
    ds->permanent_dict = ds->dict;

  /* Figure out whether to compact. */
  ds->compactor = 
    (dict_compacting_would_shrink (ds->permanent_dict)
     ? dict_make_compactor (ds->permanent_dict)
     : NULL);

  /* Prepare sink. */
  if (ds->proc_sink == NULL)
    ds->proc_sink = create_case_sink (&storage_sink_class, ds->permanent_dict, NULL);
  if (ds->proc_sink->class->open != NULL)
    ds->proc_sink->class->open (ds->proc_sink);

  /* Allocate memory for lag queue. */
  if (ds->n_lag > 0)
    {
      int i;
  
      ds->lag_count = 0;
      ds->lag_head = 0;
      ds->lag_queue = xnmalloc (ds->n_lag, sizeof *ds->lag_queue);
      for (i = 0; i < ds->n_lag; i++)
        case_nullify (&ds->lag_queue[i]);
    }
}

/* Transforms trns_case and writes it to the replacement active
   file if advisable.  Returns true if more cases can be
   accepted, false otherwise.  Do not call this function again
   after it has returned false once.  */
static bool
write_case (struct write_case_data *wc_data)
{
  enum trns_result retval;
  size_t case_nr;

  struct dataset *ds = wc_data->dataset;
  
  /* Execute permanent transformations.  */
  case_nr = wc_data->cases_written + 1;
  retval = trns_chain_execute (ds->permanent_trns_chain,
                               &wc_data->trns_case, &case_nr);
  if (retval != TRNS_CONTINUE)
    goto done;

  /* Write case to LAG queue. */
  if (ds->n_lag)
    lag_case (ds, &wc_data->trns_case);

  /* Write case to replacement active file. */
  wc_data->cases_written++;
  if (ds->proc_sink->class->write != NULL) 
    {
      if (ds->compactor != NULL) 
        {
          dict_compactor_compact (ds->compactor, &wc_data->sink_case,
                                  &wc_data->trns_case);
          ds->proc_sink->class->write (ds->proc_sink, &wc_data->sink_case);
        }
      else
        ds->proc_sink->class->write (ds->proc_sink, &wc_data->trns_case);
    }
  
  /* Execute temporary transformations. */
  if (ds->temporary_trns_chain != NULL) 
    {
      retval = trns_chain_execute (ds->temporary_trns_chain,
                                   &wc_data->trns_case,
                                   &wc_data->cases_written);
      if (retval != TRNS_CONTINUE)
        goto done;
    }

  /* Pass case to procedure. */
  if (wc_data->proc != NULL)
    if (!wc_data->proc (&wc_data->trns_case, wc_data->aux, ds))
      retval = TRNS_ERROR;

 done:
  clear_case (ds, &wc_data->trns_case);
  return retval != TRNS_ERROR;
}

/* Add C to the lag queue. */
static void
lag_case (struct dataset *ds, const struct ccase *c)
{
  if (ds->lag_count < ds->n_lag)
    ds->lag_count++;
  case_destroy (&ds->lag_queue[ds->lag_head]);
  case_clone (&ds->lag_queue[ds->lag_head], c);
  if (++ds->lag_head >= ds->n_lag)
    ds->lag_head = 0;
}

/* Clears the variables in C that need to be cleared between
   processing cases.  */
static void
clear_case (const struct dataset *ds, struct ccase *c)
{
  size_t var_cnt = dict_get_var_cnt (ds->dict);
  size_t i;
  
  for (i = 0; i < var_cnt; i++) 
    {
      struct variable *v = dict_get_var (ds->dict, i);
      if (!var_get_leave (v)) 
        {
          if (var_is_numeric (v))
            case_data_rw (c, v)->f = SYSMIS; 
          else
            memset (case_data_rw (c, v)->s, ' ', var_get_width (v));
        } 
    }
}

/* Closes the active file. */
static bool
close_active_file (struct dataset *ds)
{
  /* Free memory for lag queue, and turn off lagging. */
  if (ds->n_lag > 0)
    {
      int i;
      
      for (i = 0; i < ds->n_lag; i++)
	case_destroy (&ds->lag_queue[i]);
      free (ds->lag_queue);
      ds->n_lag = 0;
    }
  
  /* Dictionary from before TEMPORARY becomes permanent. */
  proc_cancel_temporary_transformations (ds);

  /* Finish compacting. */
  if (ds->compactor != NULL) 
    {
      dict_compactor_destroy (ds->compactor);
      dict_compact_values (ds->dict);
      ds->compactor = NULL;
    }
    
  /* Free data source. */
  free_case_source (ds->proc_source);
  ds->proc_source = NULL;

  /* Old data sink becomes new data source. */
  if (ds->proc_sink->class->make_source != NULL)
    ds->proc_source = ds->proc_sink->class->make_source (ds->proc_sink);
  free_case_sink (ds->proc_sink);
  ds->proc_sink = NULL;

  dict_clear_vectors (ds->dict);
  ds->permanent_dict = NULL;
  return proc_cancel_all_transformations (ds);
}

/* Returns a pointer to the lagged case from N_BEFORE cases before the
   current one, or NULL if there haven't been that many cases yet. */
struct ccase *
lagged_case (const struct dataset *ds, int n_before)
{
  assert (n_before >= 1 );
  assert (n_before <= ds->n_lag);

  if (n_before <= ds->lag_count)
    {
      int index = ds->lag_head - n_before;
      if (index < 0)
        index += ds->n_lag;
      return &ds->lag_queue[index];
    }
  else
    return NULL;
}

/* Procedure that separates the data into SPLIT FILE groups. */

/* Represents auxiliary data for handling SPLIT FILE. */
struct split_aux_data 
  {
    struct dataset *dataset;    /* The dataset */
    struct ccase prev_case;     /* Data in previous case. */

    /* Callback functions. */
    begin_func *begin; 
    case_func *proc;
    end_func *end;
    void *func_aux;
  };

static int equal_splits (const struct ccase *, const struct ccase *, const struct dataset *ds);
static bool split_procedure_case_func (const struct ccase *c, void *, const struct dataset *);
static bool split_procedure_end_func (void *, const struct dataset *);

/* Like procedure(), but it automatically breaks the case stream
   into SPLIT FILE break groups.  Before each group of cases with
   identical SPLIT FILE variable values, BEGIN_FUNC is called
   with the first case in the group.
   Then PROC_FUNC is called for each case in the group (including
   the first).
   END_FUNC is called when the group is finished.  FUNC_AUX is
   passed to each of the functions as auxiliary data.

   If the active file is empty, none of BEGIN_FUNC, PROC_FUNC,
   and END_FUNC will be called at all. 

   If SPLIT FILE is not in effect, then there is one break group
   (if the active file is nonempty), and BEGIN_FUNC and END_FUNC
   will be called once.
   
   Returns true if successful, false if an I/O error occurred. */
bool
procedure_with_splits (struct dataset *ds,
		       begin_func begin, 
		       case_func *proc,
                       end_func *end,
                       void *func_aux) 
{
  struct split_aux_data split_aux;
  bool ok;

  case_nullify (&split_aux.prev_case);
  split_aux.begin = begin;
  split_aux.proc = proc;
  split_aux.end = end;
  split_aux.func_aux = func_aux;
  split_aux.dataset = ds;

  ok = internal_procedure (ds, split_procedure_case_func,
                           split_procedure_end_func, &split_aux);

  case_destroy (&split_aux.prev_case);

  return ok;
}

/* Case callback used by procedure_with_splits(). */
static bool
split_procedure_case_func (const struct ccase *c, void *split_aux_, const struct dataset *ds) 
{
  struct split_aux_data *split_aux = split_aux_;

  /* Start a new series if needed. */
  if (case_is_null (&split_aux->prev_case)
      || !equal_splits (c, &split_aux->prev_case, split_aux->dataset))
    {
      if (!case_is_null (&split_aux->prev_case) && split_aux->end != NULL)
        split_aux->end (split_aux->func_aux, ds);

      case_destroy (&split_aux->prev_case);
      case_clone (&split_aux->prev_case, c);

      if (split_aux->begin != NULL)
	split_aux->begin (&split_aux->prev_case, split_aux->func_aux, ds);
    }

  return (split_aux->proc == NULL
          || split_aux->proc (c, split_aux->func_aux, ds));
}

/* End-of-file callback used by procedure_with_splits(). */
static bool
split_procedure_end_func (void *split_aux_, const struct dataset *ds) 
{
  struct split_aux_data *split_aux = split_aux_;

  if (!case_is_null (&split_aux->prev_case) && split_aux->end != NULL)
    split_aux->end (split_aux->func_aux, ds);
  return true;
}

/* Compares the SPLIT FILE variables in cases A and B and returns
   nonzero only if they differ. */
static int
equal_splits (const struct ccase *a, const struct ccase *b, 
	      const struct dataset *ds) 
{
  return case_compare (a, b,
                       dict_get_split_vars (ds->dict),
                       dict_get_split_cnt (ds->dict)) == 0;
}

/* Multipass procedure that separates the data into SPLIT FILE
   groups. */

/* Represents auxiliary data for handling SPLIT FILE in a
   multipass procedure. */
struct multipass_split_aux_data 
  {
    struct dataset *dataset;    /* The dataset of the split */
    struct ccase prev_case;     /* Data in previous case. */
    struct casefile *casefile;  /* Accumulates data for a split. */
    split_func *split;          /* Function to call with the accumulated 
				   data. */
    void *func_aux;             /* Auxiliary data. */ 
  };

static bool multipass_split_case_func (const struct ccase *c, void *aux_, const struct dataset *);
static bool multipass_split_end_func (void *aux_, const struct dataset *ds);
static bool multipass_split_output (struct multipass_split_aux_data *, const struct dataset *ds);

/* Returns true if successful, false if an I/O error occurred. */
bool
multipass_procedure_with_splits (struct dataset *ds, 
				 split_func  *split,
                                 void *func_aux)
{
  struct multipass_split_aux_data aux;
  bool ok;

  case_nullify (&aux.prev_case);
  aux.casefile = NULL;
  aux.split = split;
  aux.func_aux = func_aux;
  aux.dataset = ds;

  ok = internal_procedure (ds, multipass_split_case_func,
                           multipass_split_end_func, &aux);
  case_destroy (&aux.prev_case);

  return ok;
}

/* Case callback used by multipass_procedure_with_splits(). */
static bool
multipass_split_case_func (const struct ccase *c, void *aux_, const struct dataset *ds)
{
  struct multipass_split_aux_data *aux = aux_;
  bool ok = true;

  /* Start a new series if needed. */
  if (aux->casefile == NULL || ! equal_splits (c, &aux->prev_case, ds))
    {
      /* Record split values. */
      case_destroy (&aux->prev_case);
      case_clone (&aux->prev_case, c);

      /* Pass any cases to split_func. */
      if (aux->casefile != NULL)
        ok = multipass_split_output (aux, ds);

      /* Start a new casefile. */
      aux->casefile = 
	fastfile_create (dict_get_next_value_idx (ds->dict));
    }

  return casefile_append (aux->casefile, c) && ok;
}

/* End-of-file callback used by multipass_procedure_with_splits(). */
static bool
multipass_split_end_func (void *aux_, const struct dataset *ds)
{
  struct multipass_split_aux_data *aux = aux_;
  return (aux->casefile == NULL || multipass_split_output (aux, ds));
}

static bool
multipass_split_output (struct multipass_split_aux_data *aux, const struct dataset *ds)
{
  bool ok;
  
  assert (aux->casefile != NULL);
  ok = aux->split (&aux->prev_case, aux->casefile, aux->func_aux, ds);
  casefile_destroy (aux->casefile);
  aux->casefile = NULL;

  return ok;
}

/* Discards all the current state in preparation for a data-input
   command like DATA LIST or GET. */
void
discard_variables (struct dataset *ds)
{
  dict_clear (ds->dict);
  fh_set_default_handle (NULL);

  ds->n_lag = 0;
  
  free_case_source (ds->proc_source);
  ds->proc_source = NULL;

  proc_cancel_all_transformations (ds);
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
  return chain;
}

/* Adds a transformation that processes a case with PROC and
   frees itself with FREE to the current set of transformations.
   The functions are passed AUX as auxiliary data. */
void
add_transformation (struct dataset *ds, trns_proc_func *proc, trns_free_func *free, void *aux)
{
  trns_chain_append (ds->cur_trns_chain, NULL, proc, free, aux);
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
  ok = trns_chain_destroy (ds->permanent_trns_chain);
  ok = trns_chain_destroy (ds->temporary_trns_chain) && ok;
  ds->permanent_trns_chain = ds->cur_trns_chain = trns_chain_create ();
  ds->temporary_trns_chain = NULL;
  return ok;
}

/* Initializes procedure handling. */
struct dataset *
create_dataset (void)
{
  struct dataset *ds = xzalloc (sizeof(*ds));
  ds->dict = dict_create ();
  proc_cancel_all_transformations (ds);
  return ds;
}

/* Finishes up procedure handling. */
void
destroy_dataset (struct dataset *ds)
{
  discard_variables (ds);
  dict_destroy (ds->dict);
  trns_chain_destroy (ds->permanent_trns_chain);
  free (ds);
}

/* Sets SINK as the destination for procedure output from the
   next procedure. */
void
proc_set_sink (struct dataset *ds, struct case_sink *sink) 
{
  assert (ds->proc_sink == NULL);
  ds->proc_sink = sink;
}

/* Sets SOURCE as the source for procedure input for the next
   procedure. */
void
proc_set_source (struct dataset *ds, struct case_source *source) 
{
  assert (ds->proc_source == NULL);
  ds->proc_source = source;
}

/* Returns true if a source for the next procedure has been
   configured, false otherwise. */
bool
proc_has_source (const struct dataset *ds) 
{
  return ds->proc_source != NULL;
}

/* Returns the output from the previous procedure.
   For use only immediately after executing a procedure.
   The returned casefile is owned by the caller; it will not be
   automatically used for the next procedure's input. */
struct casefile *
proc_capture_output (struct dataset *ds) 
{
  struct casefile *casefile;

  /* Try to make sure that this function is called immediately
     after procedure() or a similar function. */
  assert (ds->proc_source != NULL);
  assert (case_source_is_class (ds->proc_source, &storage_source_class));
  assert (trns_chain_is_empty (ds->permanent_trns_chain));
  assert (!proc_in_temporary_transformations (ds));

  casefile = storage_source_decapsulate (ds->proc_source);
  ds->proc_source = NULL;

  return casefile;
}

static trns_proc_func case_limit_trns_proc;
static trns_free_func case_limit_trns_free;

/* Adds a transformation that limits the number of cases that may
   pass through, if DS->DICT has a case limit. */
static void
add_case_limit_trns (struct dataset *ds) 
{
  size_t case_limit = dict_get_case_limit (ds->dict);
  if (case_limit != 0)
    {
      size_t *cases_remaining = xmalloc (sizeof *cases_remaining);
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
  return (f != 0.0 && !var_is_num_missing (filter_var, f)
          ? TRNS_CONTINUE : TRNS_DROP_CASE);
}


struct dictionary *
dataset_dict (const struct dataset *ds)
{
  return ds->dict;
}


void 
dataset_set_dict (struct dataset *ds, struct dictionary *dict)
{
  ds->dict = dict;
}

int 
dataset_n_lag (const struct dataset *ds)
{
  return ds->n_lag;
}

void 
dataset_set_n_lag (struct dataset *ds, int n_lag)
{
  ds->n_lag = n_lag;
}


