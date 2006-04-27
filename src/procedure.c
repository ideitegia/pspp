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

#include <procedure.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "expressions/public.h"
#include <data/case-source.h>
#include <data/case-sink.h>
#include <data/case.h>
#include <data/casefile.h>
#include <data/dictionary.h>
#include <data/file-handle-def.h>
#include <data/settings.h>
#include <data/storage-stream.h>
#include <data/value-labels.h>
#include <data/variable.h>
#include <language/control/control-stack.h>
#include <libpspp/alloc.h>
#include <libpspp/message.h>
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
    bool (*proc_func) (struct ccase *, void *); /* Function. */
    void *aux;                                 /* Auxiliary data. */ 

    struct ccase trns_case;     /* Case used for transformations. */
    struct ccase sink_case;     /* Case written to sink, if
                                   compaction is necessary. */
    size_t cases_written;       /* Cases output so far. */
    size_t cases_analyzed;      /* Cases passed to procedure so far. */
  };

/* The current active file, from which cases are read. */
struct case_source *vfm_source;

/* The replacement active file, to which cases are written. */
struct case_sink *vfm_sink;

/* The compactor used to compact a compact, if necessary;
   otherwise a null pointer. */
static struct dict_compactor *compactor;

/* Time at which vfm was last invoked. */
static time_t last_vfm_invocation;

/* Whether we're inside a procedure.
   For debugging purposes only. */
static bool in_procedure;

/* Lag queue. */
int n_lag;			/* Number of cases to lag. */
static int lag_count;		/* Number of cases in lag_queue so far. */
static int lag_head;		/* Index where next case will be added. */
static struct ccase *lag_queue; /* Array of n_lag ccase * elements. */

/* Active transformations. */
struct transformation *t_trns;
size_t n_trns, m_trns, f_trns;

static bool internal_procedure (bool (*proc_func) (struct ccase *, void *),
                                void *aux);
static void update_last_vfm_invocation (void);
static void create_trns_case (struct ccase *, struct dictionary *);
static void open_active_file (void);
static bool write_case (struct write_case_data *wc_data);
static int execute_transformations (struct ccase *c,
                                    struct transformation *trns,
                                    int first_idx, int last_idx,
                                    int case_num);
static int filter_case (const struct ccase *c, int case_num);
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

/* Reads the data from the input program and writes it to a new
   active file.  For each case we read from the input program, we
   do the following

   1. Execute permanent transformations.  If these drop the case,
      start the next case from step 1.

   2. N OF CASES.  If we have already written N cases, start the
      next case from step 1.
   
   3. Write case to replacement active file.
   
   4. Execute temporary transformations.  If these drop the case,
      start the next case from step 1.
      
   5. FILTER, PROCESS IF.  If these drop the case, start the next
      case from step 1.
   
   6. Post-TEMPORARY N OF CASES.  If we have already analyzed N
      cases, start the next case from step 1.
      
   7. Pass case to PROC_FUNC, passing AUX as auxiliary data.

   Returns true if successful, false if an I/O error occurred. */
bool
procedure (bool (*proc_func) (struct ccase *, void *), void *aux)
{
  if (proc_func == NULL
      && case_source_is_class (vfm_source, &storage_source_class)
      && vfm_sink == NULL
      && !temporary
      && n_trns == 0)
    {
      /* Nothing to do. */
      update_last_vfm_invocation ();
      return true;
    }
  else 
    {
      bool ok;
      
      open_active_file ();
      ok = internal_procedure (proc_func, aux);
      if (!close_active_file ())
        ok = false;

      return ok;
    }
}

/* Executes a procedure, as procedure(), except that the caller
   is responsible for calling open_active_file() and
   close_active_file().
   Returns true if successful, false if an I/O error occurred. */
static bool
internal_procedure (bool (*proc_func) (struct ccase *, void *), void *aux) 
{
  struct write_case_data wc_data;
  bool ok;

  wc_data.proc_func = proc_func;
  wc_data.aux = aux;
  create_trns_case (&wc_data.trns_case, default_dict);
  case_create (&wc_data.sink_case, dict_get_next_value_idx (default_dict));
  wc_data.cases_written = 0;

  update_last_vfm_invocation ();

  ok = (vfm_source == NULL
        || vfm_source->class->read (vfm_source,
                                    &wc_data.trns_case,
                                    write_case, &wc_data));

  case_destroy (&wc_data.sink_case);
  case_destroy (&wc_data.trns_case);

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
        value->f = v->reinit ? 0.0 : SYSMIS;
      else
        memset (value->s, ' ', v->width);
    }
}

/* Makes all preparations for reading from the data source and writing
   to the data sink. */
static void
open_active_file (void)
{
  assert (!in_procedure);
  in_procedure = true;

  /* Make temp_dict refer to the dictionary right before data
     reaches the sink */
  if (!temporary)
    {
      temp_trns = n_trns;
      temp_dict = default_dict;
    }

  /* Figure out compaction. */
  compactor = (dict_needs_compaction (temp_dict)
               ? dict_make_compactor (temp_dict)
               : NULL);

  /* Prepare sink. */
  if (vfm_sink == NULL)
    vfm_sink = create_case_sink (&storage_sink_class, temp_dict, NULL);
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

  /* Close any unclosed DO IF or LOOP constructs. */
  ctl_stack_clear ();
}

/* Transforms trns_case and writes it to the replacement active
   file if advisable.  Returns true if more cases can be
   accepted, false otherwise.  Do not call this function again
   after it has returned false once.  */
static bool
write_case (struct write_case_data *wc_data)
{
  int retval;
  
  /* Execute permanent transformations.  */
  retval = execute_transformations (&wc_data->trns_case, t_trns, f_trns,
                                    temp_trns, wc_data->cases_written + 1);
  if (retval != 1)
    goto done;

  /* N OF CASES. */
  if (dict_get_case_limit (default_dict)
      && wc_data->cases_written >= dict_get_case_limit (default_dict))
    goto done;
  wc_data->cases_written++;

  /* Write case to LAG queue. */
  if (n_lag)
    lag_case (&wc_data->trns_case);

  /* Write case to replacement active file. */
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
  retval = execute_transformations (&wc_data->trns_case, t_trns, temp_trns,
                                    n_trns, wc_data->cases_written);
  if (retval != 1)
    goto done;
  
  /* FILTER, PROCESS IF, post-TEMPORARY N OF CASES. */
  if (filter_case (&wc_data->trns_case, wc_data->cases_written)
      || (dict_get_case_limit (temp_dict)
          && wc_data->cases_analyzed >= dict_get_case_limit (temp_dict)))
    goto done;
  wc_data->cases_analyzed++;

  /* Pass case to procedure. */
  if (wc_data->proc_func != NULL)
    if (!wc_data->proc_func (&wc_data->trns_case, wc_data->aux))
      retval = -1;

 done:
  clear_case (&wc_data->trns_case);
  return retval != -1;
}

/* Transforms case C using the transformations in TRNS[] with
   indexes FIRST_IDX through LAST_IDX, exclusive.  Case C will
   become case CASE_NUM (1-based) in the output file.  Returns 1
   if the case was successfully transformed, 0 if it was filtered
   out by one of the transformations, or -1 if the procedure
   should be abandoned due to a fatal error. */
static int
execute_transformations (struct ccase *c,
                         struct transformation *trns,
                         int first_idx, int last_idx,
                         int case_num) 
{
  int idx;

  for (idx = first_idx; idx != last_idx; )
    {
      struct transformation *t = &trns[idx];
      int retval = t->proc (t->private, c, case_num);
      switch (retval)
        {
        case TRNS_CONTINUE:
          idx++;
          break;
          
        case TRNS_DROP_CASE:
          return 0;

        case TRNS_ERROR:
          return -1;

        case TRNS_NEXT_CASE:
          abort ();

        case TRNS_END_FILE:
          abort ();
          
        default:
          idx = retval;
          break;
        }
    }

  return 1;
}

/* Returns nonzero if case C with case number CASE_NUM should be
   excluded as specified on FILTER or PROCESS IF, otherwise
   zero. */
static int
filter_case (const struct ccase *c, int case_idx)
{
  /* FILTER. */
  struct variable *filter_var = dict_get_filter (default_dict);
  if (filter_var != NULL) 
    {
      double f = case_num (c, filter_var->fv);
      if (f == 0.0 || mv_is_num_missing (&filter_var->miss, f))
        return 1;
    }

  /* PROCESS IF. */
  if (process_if_expr != NULL
      && expr_evaluate_num (process_if_expr, c, case_idx) != 1.0)
    return 1;

  return 0;
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
      if (v->reinit) 
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
  
  /* Dictionary from before TEMPORARY becomes permanent.. */
  if (temporary)
    {
      dict_destroy (default_dict);
      default_dict = temp_dict;
      temp_dict = NULL;
    }

  /* Finish compaction. */
  if (compactor != NULL) 
    {
      dict_compactor_destroy (compactor);
      dict_compact_values (default_dict); 
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
  cancel_temporary ();
  expr_free (process_if_expr);
  process_if_expr = NULL;
  dict_set_case_limit (default_dict, 0);
  dict_clear_vectors (default_dict);

  assert (in_procedure);
  in_procedure = false;

  return cancel_transformations ();
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
   
/* Appends TRNS to t_trns[], the list of all transformations to be
   performed on data as it is read from the active file. */
void
add_transformation (trns_proc_func *proc, trns_free_func *free, void *private)
{
  struct transformation *trns;

  assert (!in_procedure);

  if (n_trns >= m_trns)
    t_trns = x2nrealloc (t_trns, &m_trns, sizeof *t_trns);
  trns = &t_trns[n_trns++];
  trns->proc = proc;
  trns->free = free;
  trns->private = private;
}

/* Returns the index number that the next transformation added by
   add_transformation() will receive.  A trns_proc_func that
   returns this index causes control flow to jump to it. */
size_t
next_transformation (void) 
{
  return n_trns;
}

/* Cancels all active transformations, including any transformations
   created by the input program.
   Returns true if successful, false if an I/O error occurred. */
bool
cancel_transformations (void)
{
  bool ok = true;
  size_t i;
  for (i = 0; i < n_trns; i++)
    {
      struct transformation *t = &t_trns[i];
      if (t->free != NULL) 
        {
          if (!t->free (t->private))
            ok = false; 
        }
    }
  n_trns = f_trns = 0;
  free (t_trns);
  t_trns = NULL;
  m_trns = 0;
  return ok;
}

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
static bool procedure_with_splits_callback (struct ccase *, void *);
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

  open_active_file ();
  ok = internal_procedure (procedure_with_splits_callback, &split_aux);
  if (split_aux.case_count > 0 && end_func != NULL)
    end_func (func_aux);
  if (!close_active_file ())
    ok = false;

  case_destroy (&split_aux.prev_case);

  return ok;
}

/* procedure() callback used by procedure_with_splits(). */
static bool
procedure_with_splits_callback (struct ccase *c, void *split_aux_) 
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
  if (split_aux->proc_func != NULL)
    return split_aux->proc_func (c, split_aux->func_aux);
  else
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

static bool multipass_split_callback (struct ccase *c, void *aux_);
static void multipass_split_output (struct multipass_split_aux_data *);

/* Returns true if successful, false if an I/O error occurred. */
bool
multipass_procedure_with_splits (bool (*split_func) (const struct casefile *,
                                                     void *),
                                 void *func_aux) 
{
  struct multipass_split_aux_data aux;
  bool ok;

  assert (split_func != NULL);

  open_active_file ();

  case_nullify (&aux.prev_case);
  aux.casefile = NULL;
  aux.split_func = split_func;
  aux.func_aux = func_aux;

  ok = internal_procedure (multipass_split_callback, &aux);
  if (aux.casefile != NULL)
    multipass_split_output (&aux);
  case_destroy (&aux.prev_case);

  if (!close_active_file ())
    ok = false;

  return ok;
}

/* procedure() callback used by multipass_procedure_with_splits(). */
static bool
multipass_split_callback (struct ccase *c, void *aux_)
{
  struct multipass_split_aux_data *aux = aux_;

  /* Start a new series if needed. */
  if (aux->casefile == NULL || !equal_splits (c, &aux->prev_case))
    {
      /* Pass any cases to split_func. */
      if (aux->casefile != NULL)
        multipass_split_output (aux);

      /* Start a new casefile. */
      aux->casefile = casefile_create (dict_get_next_value_idx (default_dict));

      /* Record split values. */
      dump_splits (c);
      case_destroy (&aux->prev_case);
      case_clone (&aux->prev_case, c);
    }

  return casefile_append (aux->casefile, c);
}

static void
multipass_split_output (struct multipass_split_aux_data *aux)
{
  assert (aux->casefile != NULL);
  aux->split_func (aux->casefile, aux->func_aux);
  casefile_destroy (aux->casefile);
  aux->casefile = NULL;
}


/* Discards all the current state in preparation for a data-input
   command like DATA LIST or GET. */
void
discard_variables (void)
{
  dict_clear (default_dict);
  fh_set_default_handle (NULL);

  n_lag = 0;
  
  if (vfm_source != NULL)
    {
      free_case_source (vfm_source);
      vfm_source = NULL;
    }

  cancel_transformations ();

  ctl_stack_clear ();

  expr_free (process_if_expr);
  process_if_expr = NULL;

  cancel_temporary ();
}
