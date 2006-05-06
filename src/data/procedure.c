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
#include <data/dictionary.h>
#include <data/file-handle-def.h>
#include <data/procedure.h>
#include <data/settings.h>
#include <data/storage-stream.h>
#include <data/transformations.h>
#include <data/value-labels.h>
#include <data/variable.h>
#include <language/expressions/public.h>
#include <libpspp/alloc.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>
#include <output/manager.h>
#include <output/table.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/*
   Virtual File Manager (vfm):

   vfm is used to process data files.  It uses the model that
   data is read from one stream (the data source), processed,
   then written to another (the data sink).  The data source is
   then deleted and the data sink becomes the data source for the
   next procedure. */

/* Procedure execution data. */
struct write_case_data
  {
    /* Function to call for each case. */
    bool (*case_func) (struct ccase *, void *); /* Function. */
    void *aux;                                 /* Auxiliary data. */ 

    struct ccase trns_case;     /* Case used for transformations. */
    struct ccase sink_case;     /* Case written to sink, if
                                   compaction is necessary. */
    size_t cases_written;       /* Cases output so far. */
  };

/* Cases are read from vfm_source,
   pass through permanent_trns_chain (which transforms them into
   the format described by permanent_dict),
   are written to vfm_sink,
   pass through temporary_trns_chain (which transforms them into
   the format described by default_dict),
   and are finally passed to the procedure. */
static struct case_source *vfm_source;
static struct trns_chain *permanent_trns_chain;
static struct dictionary *permanent_dict;
static struct case_sink *vfm_sink;
static struct trns_chain *temporary_trns_chain;
struct dictionary *default_dict;

/* The transformation chain that the next transformation will be
   added to. */
static struct trns_chain *cur_trns_chain;

/* The compactor used to compact a case, if necessary;
   otherwise a null pointer. */
static struct dict_compactor *compactor;

/* Time at which vfm was last invoked. */
static time_t last_vfm_invocation;

/* Lag queue. */
int n_lag;			/* Number of cases to lag. */
static int lag_count;		/* Number of cases in lag_queue so far. */
static int lag_head;		/* Index where next case will be added. */
static struct ccase *lag_queue; /* Array of n_lag ccase * elements. */

static void add_case_limit_trns (void);
static void add_filter_trns (void);
static void add_process_if_trns (void);

static bool internal_procedure (bool (*case_func) (struct ccase *, void *),
                                bool (*end_func) (void *),
                                void *aux);
static void update_last_vfm_invocation (void);
static void create_trns_case (struct ccase *, struct dictionary *);
static void open_active_file (void);
static bool write_case (struct write_case_data *wc_data);
static void lag_case (const struct ccase *c);
static void clear_case (struct ccase *c);
static bool close_active_file (void);

/* Public functions. */

/* Returns the last time the data was read. */
time_t
time_of_last_procedure (void) 
{
  if (last_vfm_invocation == 0)
    update_last_vfm_invocation ();
  return last_vfm_invocation;
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
procedure (bool (*proc_func) (struct ccase *, void *), void *aux)
{
  return internal_procedure (proc_func, NULL, aux);
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
multipass_case_func (struct ccase *c, void *aux_data_) 
{
  struct multipass_aux_data *aux_data = aux_data_;
  return casefile_append (aux_data->casefile, c);
}

/* End-of-file function for multipass_procedure(). */
static bool
multipass_end_func (void *aux_data_) 
{
  struct multipass_aux_data *aux_data = aux_data_;
  return (aux_data->proc_func == NULL
          || aux_data->proc_func (aux_data->casefile, aux_data->aux));
}

/* Procedure that allows multiple passes over the input data.
   The entire active file is passed to PROC_FUNC, with the given
   AUX as auxiliary data, as a unit. */
bool
multipass_procedure (bool (*proc_func) (const struct casefile *, void *aux),
                     void *aux) 
{
  struct multipass_aux_data aux_data;
  bool ok;

  aux_data.casefile = casefile_create (dict_get_next_value_idx (default_dict));
  aux_data.proc_func = proc_func;
  aux_data.aux = aux;

  ok = internal_procedure (multipass_case_func, multipass_end_func, &aux_data);
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
internal_procedure (bool (*case_func) (struct ccase *, void *),
                    bool (*end_func) (void *),
                    void *aux) 
{
  struct write_case_data wc_data;
  bool ok = true;

  assert (vfm_source != NULL);

  update_last_vfm_invocation ();

  /* Optimize the trivial case where we're not going to do
     anything with the data, by not reading the data at all. */
  if (case_func == NULL && end_func == NULL
      && case_source_is_class (vfm_source, &storage_source_class)
      && vfm_sink == NULL
      && (temporary_trns_chain == NULL
          || trns_chain_is_empty (temporary_trns_chain))
      && trns_chain_is_empty (permanent_trns_chain))
    {
      n_lag = 0;
      expr_free (process_if_expr);
      process_if_expr = NULL;
      dict_set_case_limit (default_dict, 0);
      dict_clear_vectors (default_dict);
      return true;
    }
  
  open_active_file ();
  
  wc_data.case_func = case_func;
  wc_data.aux = aux;
  create_trns_case (&wc_data.trns_case, default_dict);
  case_create (&wc_data.sink_case, dict_get_next_value_idx (default_dict));
  wc_data.cases_written = 0;

  ok = vfm_source->class->read (vfm_source,
                                &wc_data.trns_case,
                                write_case, &wc_data) && ok;
  if (end_func != NULL)
    ok = end_func (aux) && ok;

  case_destroy (&wc_data.sink_case);
  case_destroy (&wc_data.trns_case);

  ok = close_active_file () && ok;

  return ok;
}

/* Updates last_vfm_invocation. */
static void
update_last_vfm_invocation (void) 
{
  last_vfm_invocation = time (NULL);
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
      union value *value = case_data_rw (trns_case, v->fv);

      if (v->type == NUMERIC)
        value->f = v->leave ? 0.0 : SYSMIS;
      else
        memset (value->s, ' ', v->width);
    }
}

/* Makes all preparations for reading from the data source and writing
   to the data sink. */
static void
open_active_file (void)
{
  add_case_limit_trns ();
  add_filter_trns ();
  add_process_if_trns ();

  /* Finalize transformations. */
  trns_chain_finalize (cur_trns_chain);

  /* Make permanent_dict refer to the dictionary right before
     data reaches the sink. */
  if (permanent_dict == NULL)
    permanent_dict = default_dict;

  /* Figure out compaction. */
  compactor = (dict_needs_compaction (permanent_dict)
               ? dict_make_compactor (permanent_dict)
               : NULL);

  /* Prepare sink. */
  if (vfm_sink == NULL)
    vfm_sink = create_case_sink (&storage_sink_class, permanent_dict, NULL);
  if (vfm_sink->class->open != NULL)
    vfm_sink->class->open (vfm_sink);

  /* Allocate memory for lag queue. */
  if (n_lag > 0)
    {
      int i;
  
      lag_count = 0;
      lag_head = 0;
      lag_queue = xnmalloc (n_lag, sizeof *lag_queue);
      for (i = 0; i < n_lag; i++)
        case_nullify (&lag_queue[i]);
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
  
  /* Execute permanent transformations.  */
  case_nr = wc_data->cases_written + 1;
  retval = trns_chain_execute (permanent_trns_chain,
                               &wc_data->trns_case, &case_nr);
  if (retval != TRNS_CONTINUE)
    goto done;

  /* Write case to LAG queue. */
  if (n_lag)
    lag_case (&wc_data->trns_case);

  /* Write case to replacement active file. */
  wc_data->cases_written++;
  if (vfm_sink->class->write != NULL) 
    {
      if (compactor != NULL) 
        {
          dict_compactor_compact (compactor, &wc_data->sink_case,
                                  &wc_data->trns_case);
          vfm_sink->class->write (vfm_sink, &wc_data->sink_case);
        }
      else
        vfm_sink->class->write (vfm_sink, &wc_data->trns_case);
    }
  
  /* Execute temporary transformations. */
  if (temporary_trns_chain != NULL) 
    {
      retval = trns_chain_execute (temporary_trns_chain,
                                   &wc_data->trns_case,
                                   &wc_data->cases_written);
      if (retval != TRNS_CONTINUE)
        goto done;
    }

  /* Pass case to procedure. */
  if (wc_data->case_func != NULL)
    if (!wc_data->case_func (&wc_data->trns_case, wc_data->aux))
      retval = TRNS_ERROR;

 done:
  clear_case (&wc_data->trns_case);
  return retval != TRNS_ERROR;
}

/* Add C to the lag queue. */
static void
lag_case (const struct ccase *c)
{
  if (lag_count < n_lag)
    lag_count++;
  case_destroy (&lag_queue[lag_head]);
  case_clone (&lag_queue[lag_head], c);
  if (++lag_head >= n_lag)
    lag_head = 0;
}

/* Clears the variables in C that need to be cleared between
   processing cases.  */
static void
clear_case (struct ccase *c)
{
  size_t var_cnt = dict_get_var_cnt (default_dict);
  size_t i;
  
  for (i = 0; i < var_cnt; i++) 
    {
      struct variable *v = dict_get_var (default_dict, i);
      if (!v->leave) 
        {
          if (v->type == NUMERIC)
            case_data_rw (c, v->fv)->f = SYSMIS;
          else
            memset (case_data_rw (c, v->fv)->s, ' ', v->width);
        } 
    }
}

/* Closes the active file. */
static bool
close_active_file (void)
{
  /* Free memory for lag queue, and turn off lagging. */
  if (n_lag > 0)
    {
      int i;
      
      for (i = 0; i < n_lag; i++)
	case_destroy (&lag_queue[i]);
      free (lag_queue);
      n_lag = 0;
    }
  
  /* Dictionary from before TEMPORARY becomes permanent. */
  proc_cancel_temporary_transformations ();

  /* Finish compaction. */
  if (compactor != NULL) 
    {
      dict_compactor_destroy (compactor);
      dict_compact_values (default_dict);
      compactor = NULL;
    }
    
  /* Free data source. */
  free_case_source (vfm_source);
  vfm_source = NULL;

  /* Old data sink becomes new data source. */
  if (vfm_sink->class->make_source != NULL)
    vfm_source = vfm_sink->class->make_source (vfm_sink);
  free_case_sink (vfm_sink);
  vfm_sink = NULL;

  /* Cancel TEMPORARY, PROCESS IF, FILTER, N OF CASES, vectors,
     and get rid of all the transformations. */
  dict_clear_vectors (default_dict);
  permanent_dict = NULL;
  return proc_cancel_all_transformations ();
}

/* Returns a pointer to the lagged case from N_BEFORE cases before the
   current one, or NULL if there haven't been that many cases yet. */
struct ccase *
lagged_case (int n_before)
{
  assert (n_before >= 1 );
  assert (n_before <= n_lag);

  if (n_before <= lag_count)
    {
      int index = lag_head - n_before;
      if (index < 0)
        index += n_lag;
      return &lag_queue[index];
    }
  else
    return NULL;
}

/* Procedure that separates the data into SPLIT FILE groups. */

/* Represents auxiliary data for handling SPLIT FILE. */
struct split_aux_data 
  {
    size_t case_count;          /* Number of cases so far. */
    struct ccase prev_case;     /* Data in previous case. */

    /* Functions to call... */
    void (*begin_func) (void *);               /* ...before data. */
    bool (*proc_func) (struct ccase *, void *); /* ...with data. */
    void (*end_func) (void *);                 /* ...after data. */
    void *func_aux;                            /* Auxiliary data. */ 
  };

static int equal_splits (const struct ccase *, const struct ccase *);
static bool split_procedure_case_func (struct ccase *c, void *split_aux_);
static bool split_procedure_end_func (void *split_aux_);
static void dump_splits (struct ccase *);

/* Like procedure(), but it automatically breaks the case stream
   into SPLIT FILE break groups.  Before each group of cases with
   identical SPLIT FILE variable values, BEGIN_FUNC is called.
   Then PROC_FUNC is called with each case in the group.  
   END_FUNC is called when the group is finished.  FUNC_AUX is
   passed to each of the functions as auxiliary data.

   If the active file is empty, none of BEGIN_FUNC, PROC_FUNC,
   and END_FUNC will be called at all. 

   If SPLIT FILE is not in effect, then there is one break group
   (if the active file is nonempty), and BEGIN_FUNC and END_FUNC
   will be called once.
   
   Returns true if successful, false if an I/O error occurred. */
bool
procedure_with_splits (void (*begin_func) (void *aux),
                       bool (*proc_func) (struct ccase *, void *aux),
                       void (*end_func) (void *aux),
                       void *func_aux) 
{
  struct split_aux_data split_aux;
  bool ok;

  split_aux.case_count = 0;
  case_nullify (&split_aux.prev_case);
  split_aux.begin_func = begin_func;
  split_aux.proc_func = proc_func;
  split_aux.end_func = end_func;
  split_aux.func_aux = func_aux;

  ok = internal_procedure (split_procedure_case_func,
                           split_procedure_end_func, &split_aux);

  case_destroy (&split_aux.prev_case);

  return ok;
}

/* Case callback used by procedure_with_splits(). */
static bool
split_procedure_case_func (struct ccase *c, void *split_aux_) 
{
  struct split_aux_data *split_aux = split_aux_;

  /* Start a new series if needed. */
  if (split_aux->case_count == 0
      || !equal_splits (c, &split_aux->prev_case))
    {
      if (split_aux->case_count > 0 && split_aux->end_func != NULL)
        split_aux->end_func (split_aux->func_aux);

      dump_splits (c);
      case_destroy (&split_aux->prev_case);
      case_clone (&split_aux->prev_case, c);

      if (split_aux->begin_func != NULL)
	split_aux->begin_func (split_aux->func_aux);
    }

  split_aux->case_count++;
  return (split_aux->proc_func == NULL
          || split_aux->proc_func (c, split_aux->func_aux));
}

/* End-of-file callback used by procedure_with_splits(). */
static bool
split_procedure_end_func (void *split_aux_) 
{
  struct split_aux_data *split_aux = split_aux_;

  if (split_aux->case_count > 0 && split_aux->end_func != NULL)
    split_aux->end_func (split_aux->func_aux);
  return true;
}

/* Compares the SPLIT FILE variables in cases A and B and returns
   nonzero only if they differ. */
static int
equal_splits (const struct ccase *a, const struct ccase *b) 
{
  return case_compare (a, b,
                       dict_get_split_vars (default_dict),
                       dict_get_split_cnt (default_dict)) == 0;
}

/* Dumps out the values of all the split variables for the case C. */
static void
dump_splits (struct ccase *c)
{
  struct variable *const *split;
  struct tab_table *t;
  size_t split_cnt;
  int i;

  split_cnt = dict_get_split_cnt (default_dict);
  if (split_cnt == 0)
    return;

  t = tab_create (3, split_cnt + 1, 0);
  tab_dim (t, tab_natural_dimensions);
  tab_vline (t, TAL_GAP, 1, 0, split_cnt);
  tab_vline (t, TAL_GAP, 2, 0, split_cnt);
  tab_text (t, 0, 0, TAB_NONE, _("Variable"));
  tab_text (t, 1, 0, TAB_LEFT, _("Value"));
  tab_text (t, 2, 0, TAB_LEFT, _("Label"));
  split = dict_get_split_vars (default_dict);
  for (i = 0; i < split_cnt; i++)
    {
      struct variable *v = split[i];
      char temp_buf[80];
      const char *val_lab;

      assert (v->type == NUMERIC || v->type == ALPHA);
      tab_text (t, 0, i + 1, TAB_LEFT | TAT_PRINTF, "%s", v->name);
      
      data_out (temp_buf, &v->print, case_data (c, v->fv));
      
      temp_buf[v->print.w] = 0;
      tab_text (t, 1, i + 1, TAT_PRINTF, "%.*s", v->print.w, temp_buf);

      val_lab = val_labs_find (v->val_labs, *case_data (c, v->fv));
      if (val_lab)
	tab_text (t, 2, i + 1, TAB_LEFT, val_lab);
    }
  tab_flags (t, SOMF_NO_TITLE);
  tab_submit (t);
}

/* Multipass procedure that separates the data into SPLIT FILE
   groups. */

/* Represents auxiliary data for handling SPLIT FILE in a
   multipass procedure. */
struct multipass_split_aux_data 
  {
    struct ccase prev_case;     /* Data in previous case. */
    struct casefile *casefile;  /* Accumulates data for a split. */

    /* Function to call with the accumulated data. */
    bool (*split_func) (const struct casefile *, void *);
    void *func_aux;                            /* Auxiliary data. */ 
  };

static bool multipass_split_case_func (struct ccase *c, void *aux_);
static bool multipass_split_end_func (void *aux_);
static bool multipass_split_output (struct multipass_split_aux_data *);

/* Returns true if successful, false if an I/O error occurred. */
bool
multipass_procedure_with_splits (bool (*split_func) (const struct casefile *,
                                                     void *),
                                 void *func_aux) 
{
  struct multipass_split_aux_data aux;
  bool ok;

  case_nullify (&aux.prev_case);
  aux.casefile = NULL;
  aux.split_func = split_func;
  aux.func_aux = func_aux;

  ok = internal_procedure (multipass_split_case_func,
                           multipass_split_end_func, &aux);
  case_destroy (&aux.prev_case);

  return ok;
}

/* Case callback used by multipass_procedure_with_splits(). */
static bool
multipass_split_case_func (struct ccase *c, void *aux_)
{
  struct multipass_split_aux_data *aux = aux_;
  bool ok = true;

  /* Start a new series if needed. */
  if (aux->casefile == NULL || !equal_splits (c, &aux->prev_case))
    {
      /* Pass any cases to split_func. */
      if (aux->casefile != NULL)
        ok = multipass_split_output (aux);

      /* Start a new casefile. */
      aux->casefile = casefile_create (dict_get_next_value_idx (default_dict));

      /* Record split values. */
      dump_splits (c);
      case_destroy (&aux->prev_case);
      case_clone (&aux->prev_case, c);
    }

  return casefile_append (aux->casefile, c) && ok;
}

/* End-of-file callback used by multipass_procedure_with_splits(). */
static bool
multipass_split_end_func (void *aux_)
{
  struct multipass_split_aux_data *aux = aux_;
  return (aux->casefile == NULL || multipass_split_output (aux));
}

static bool
multipass_split_output (struct multipass_split_aux_data *aux)
{
  bool ok;
  
  assert (aux->casefile != NULL);
  ok = aux->split_func (aux->casefile, aux->func_aux);
  casefile_destroy (aux->casefile);
  aux->casefile = NULL;

  return ok;
}

/* Discards all the current state in preparation for a data-input
   command like DATA LIST or GET. */
void
discard_variables (void)
{
  dict_clear (default_dict);
  fh_set_default_handle (NULL);

  n_lag = 0;
  
  free_case_source (vfm_source);
  vfm_source = NULL;

  proc_cancel_all_transformations ();

  expr_free (process_if_expr);
  process_if_expr = NULL;

  proc_cancel_temporary_transformations ();
}

/* Returns the current set of permanent transformations,
   and clears the permanent transformations.
   For use by INPUT PROGRAM. */
struct trns_chain *
proc_capture_transformations (void) 
{
  struct trns_chain *chain;
  
  assert (temporary_trns_chain == NULL);
  chain = permanent_trns_chain;
  cur_trns_chain = permanent_trns_chain = trns_chain_create ();
  return chain;
}

/* Adds a transformation that processes a case with PROC and
   frees itself with FREE to the current set of transformations.
   The functions are passed AUX as auxiliary data. */
void
add_transformation (trns_proc_func *proc, trns_free_func *free, void *aux)
{
  trns_chain_append (cur_trns_chain, NULL, proc, free, aux);
}

/* Adds a transformation that processes a case with PROC and
   frees itself with FREE to the current set of transformations.
   When parsing of the block of transformations is complete,
   FINALIZE will be called.
   The functions are passed AUX as auxiliary data. */
void
add_transformation_with_finalizer (trns_finalize_func *finalize,
                                   trns_proc_func *proc,
                                   trns_free_func *free, void *aux)
{
  trns_chain_append (cur_trns_chain, finalize, proc, free, aux);
}

/* Returns the index of the next transformation.
   This value can be returned by a transformation procedure
   function to indicate a "jump" to that transformation. */
size_t
next_transformation (void) 
{
  return trns_chain_next (cur_trns_chain);
}

/* Returns true if the next call to add_transformation() will add
   a temporary transformation, false if it will add a permanent
   transformation. */
bool
proc_in_temporary_transformations (void) 
{
  return temporary_trns_chain != NULL;
}

/* Marks the start of temporary transformations.
   Further calls to add_transformation() will add temporary
   transformations. */
void
proc_start_temporary_transformations (void) 
{
  if (!proc_in_temporary_transformations ())
    {
      add_case_limit_trns ();

      permanent_dict = dict_clone (default_dict);
      trns_chain_finalize (permanent_trns_chain);
      temporary_trns_chain = cur_trns_chain = trns_chain_create ();
    }
}

/* Converts all the temporary transformations, if any, to
   permanent transformations.  Further transformations will be
   permanent.
   Returns true if anything changed, false otherwise. */
bool
proc_make_temporary_transformations_permanent (void) 
{
  if (proc_in_temporary_transformations ()) 
    {
      trns_chain_finalize (temporary_trns_chain);
      trns_chain_splice (permanent_trns_chain, temporary_trns_chain);
      temporary_trns_chain = NULL;

      dict_destroy (permanent_dict);
      permanent_dict = NULL;

      return true;
    }
  else
    return false;
}

/* Cancels all temporary transformations, if any.  Further
   transformations will be permanent.
   Returns true if anything changed, false otherwise. */
bool
proc_cancel_temporary_transformations (void) 
{
  if (proc_in_temporary_transformations ()) 
    {
      dict_destroy (default_dict);
      default_dict = permanent_dict;
      permanent_dict = NULL;

      trns_chain_destroy (temporary_trns_chain);
      temporary_trns_chain = NULL;

      return true;
    }
  else
    return false;
}

/* Cancels all transformations, if any.
   Returns true if successful, false on I/O error. */
bool
proc_cancel_all_transformations (void)
{
  bool ok;
  ok = trns_chain_destroy (permanent_trns_chain);
  ok = trns_chain_destroy (temporary_trns_chain) && ok;
  permanent_trns_chain = cur_trns_chain = trns_chain_create ();
  temporary_trns_chain = NULL;
  return ok;
}

/* Initializes procedure handling. */
void
proc_init (void) 
{
  default_dict = dict_create ();
  proc_cancel_all_transformations ();
}

/* Finishes up procedure handling. */
void
proc_done (void)
{
  discard_variables ();
}

/* Sets SINK as the destination for procedure output from the
   next procedure. */
void
proc_set_sink (struct case_sink *sink) 
{
  assert (vfm_sink == NULL);
  vfm_sink = sink;
}

/* Sets SOURCE as the source for procedure input for the next
   procedure. */
void
proc_set_source (struct case_source *source) 
{
  assert (vfm_source == NULL);
  vfm_source = source;
}

/* Returns true if a source for the next procedure has been
   configured, false otherwise. */
bool
proc_has_source (void) 
{
  return vfm_source != NULL;
}

/* Returns the output from the previous procedure.
   For use only immediately after executing a procedure.
   The returned casefile is owned by the caller; it will not be
   automatically used for the next procedure's input. */
struct casefile *
proc_capture_output (void) 
{
  struct casefile *casefile;

  /* Try to make sure that this function is called immediately
     after procedure() or a similar function. */
  assert (vfm_source != NULL);
  assert (case_source_is_class (vfm_source, &storage_source_class));
  assert (trns_chain_is_empty (permanent_trns_chain));
  assert (!proc_in_temporary_transformations ());

  casefile = storage_source_decapsulate (vfm_source);
  vfm_source = NULL;

  return casefile;
}

static trns_proc_func case_limit_trns_proc;
static trns_free_func case_limit_trns_free;

/* Adds a transformation that limits the number of cases that may
   pass through, if default_dict has a case limit. */
static void
add_case_limit_trns (void) 
{
  size_t case_limit = dict_get_case_limit (default_dict);
  if (case_limit != 0)
    {
      size_t *cases_remaining = xmalloc (sizeof *cases_remaining);
      *cases_remaining = case_limit;
      add_transformation (case_limit_trns_proc, case_limit_trns_free,
                          cases_remaining);
      dict_set_case_limit (default_dict, 0);
    }
}

/* Limits the maximum number of cases processed to
   *CASES_REMAINING. */
static int
case_limit_trns_proc (void *cases_remaining_,
                      struct ccase *c UNUSED, int case_nr UNUSED) 
{
  size_t *cases_remaining = cases_remaining_;
  if (*cases_remaining > 0) 
    {
      *cases_remaining--;
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
add_filter_trns (void) 
{
  struct variable *filter_var = dict_get_filter (default_dict);
  if (filter_var != NULL) 
    {
      proc_start_temporary_transformations ();
      add_transformation (filter_trns_proc, NULL, filter_var);
    }
}

/* FILTER transformation. */
static int
filter_trns_proc (void *filter_var_,
                  struct ccase *c UNUSED, int case_nr UNUSED) 
  
{
  struct variable *filter_var = filter_var_;
  double f = case_num (c, filter_var->fv);
  return (f != 0.0 && !mv_is_num_missing (&filter_var->miss, f)
          ? TRNS_CONTINUE : TRNS_DROP_CASE);
}

static trns_proc_func process_if_trns_proc;
static trns_free_func process_if_trns_free;

/* Adds a temporary transformation to filter data according to
   the expression specified on PROCESS IF, if any. */
static void
add_process_if_trns (void) 
{
  if (process_if_expr != NULL) 
    {
      proc_start_temporary_transformations ();
      add_transformation (process_if_trns_proc, process_if_trns_free,
                          process_if_expr);
      process_if_expr = NULL;
    }
}

/* PROCESS IF transformation. */
static int
process_if_trns_proc (void *expression_,
                      struct ccase *c UNUSED, int case_nr UNUSED) 
  
{
  struct expression *expression = expression_;
  return (expr_evaluate_num (expression, c, case_nr) == 1.0
          ? TRNS_CONTINUE : TRNS_DROP_CASE);
}

/* Frees a PROCESS IF transformation. */
static bool
process_if_trns_free (void *expression_) 
{
  struct expression *expression = expression_;
  expr_free (expression);
  return true;
}
