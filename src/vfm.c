/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include "vfm.h"
#include "vfmP.h"
#include "error.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#if HAVE_UNISTD_H
#include <unistd.h>	/* Required by SunOS4. */
#endif
#include "alloc.h"
#include "do-ifP.h"
#include "error.h"
#include "expr.h"
#include "misc.h"
#include "random.h"
#include "settings.h"
#include "som.h"
#include "str.h"
#include "tab.h"
#include "var.h"
#include "value-labels.h"

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
    int (*proc_func) (struct ccase *, void *); /* Function. */
    void *aux;                                 /* Auxiliary data. */ 

    struct ccase *trns_case;    /* Case used for transformations. */
    struct ccase *sink_case;    /* Case written to sink, if
                                   compaction is necessary. */
    size_t cases_written;       /* Cases output so far. */
    size_t cases_analyzed;      /* Cases passed to procedure so far. */
  };

/* The current active file, from which cases are read. */
struct case_source *vfm_source;

/* The replacement active file, to which cases are written. */
struct case_sink *vfm_sink;

/* Nonzero if the case needs to have values deleted before being
   stored, zero otherwise. */
static int compaction_necessary;

/* Nonzero means that we've overflowed our allotted workspace.
   After that happens once during a session, we always store the
   active file on disk instead of in memory.  (This policy may be
   too aggressive.) */
static int workspace_overflow = 0;

/* Time at which vfm was last invoked. */
time_t last_vfm_invocation;

/* Lag queue. */
int n_lag;			/* Number of cases to lag. */
static int lag_count;		/* Number of cases in lag_queue so far. */
static int lag_head;		/* Index where next case will be added. */
static struct ccase **lag_queue; /* Array of n_lag ccase * elements. */

static struct ccase *create_trns_case (struct dictionary *);
static void open_active_file (void);
static int write_case (struct write_case_data *wc_data);
static int execute_transformations (struct ccase *c,
                                    struct trns_header **trns,
                                    int first_idx, int last_idx,
                                    int case_num);
static int filter_case (const struct ccase *c, int case_num);
static void lag_case (const struct ccase *c);
static void compact_case (struct ccase *dest, const struct ccase *src);
static void clear_case (struct ccase *c);
static void close_active_file (void);

/* Public functions. */

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
      
   7. Pass case to PROC_FUNC, passing AUX as auxiliary data. */
void
procedure (int (*proc_func) (struct ccase *, void *), void *aux)
{
  static int recursive_call;

  struct write_case_data wc_data;

  assert (++recursive_call == 1);

  wc_data.proc_func = proc_func;
  wc_data.aux = aux;
  wc_data.trns_case = create_trns_case (default_dict);
  wc_data.sink_case = xmalloc (dict_get_case_size (default_dict));
  wc_data.cases_written = 0;

  last_vfm_invocation = time (NULL);

  open_active_file ();
  if (vfm_source != NULL) 
    vfm_source->class->read (vfm_source,
                             wc_data.trns_case,
                             write_case, &wc_data);
  close_active_file ();

  free (wc_data.sink_case);
  free (wc_data.trns_case);

  assert (--recursive_call == 0);
}

/* Creates and returns a case, initializing it from the vectors
   that say which `value's need to be initialized just once, and
   which ones need to be re-initialized before every case. */
static struct ccase *
create_trns_case (struct dictionary *dict)
{
  struct ccase *c = xmalloc (dict_get_case_size (dict));
  size_t var_cnt = dict_get_var_cnt (dict);
  size_t i;

  for (i = 0; i < var_cnt; i++) 
    {
      struct variable *v = dict_get_var (dict, i);

      if (v->type == NUMERIC) 
        {
          if (v->reinit)
            c->data[v->fv].f = 0.0;
          else
            c->data[v->fv].f = SYSMIS;
        }
      else
        memset (c->data[v->fv].s, ' ', v->width);
    }
  return c;
}

/* Makes all preparations for reading from the data source and writing
   to the data sink. */
static void
open_active_file (void)
{
  /* Make temp_dict refer to the dictionary right before data
     reaches the sink */
  if (!temporary)
    {
      temp_trns = n_trns;
      temp_dict = default_dict;
    }

  /* Figure out compaction. */
  compaction_necessary = (dict_get_next_value_idx (temp_dict)
                          != dict_get_compacted_value_cnt (temp_dict));

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
      lag_queue = xmalloc (n_lag * sizeof *lag_queue);
      for (i = 0; i < n_lag; i++)
        lag_queue[i] = xmalloc (dict_get_case_size (temp_dict));
    }

  /* Close any unclosed DO IF or LOOP constructs. */
  discard_ctl_stack ();
}

/* Transforms trns_case and writes it to the replacement active
   file if advisable.  Returns nonzero if more cases can be
   accepted, zero otherwise.  Do not call this function again
   after it has returned zero once.  */
static int
write_case (struct write_case_data *wc_data)
{
  /* Execute permanent transformations.  */
  if (!execute_transformations (wc_data->trns_case, t_trns, f_trns, temp_trns,
                                wc_data->cases_written + 1))
    goto done;

  /* N OF CASES. */
  if (dict_get_case_limit (default_dict)
      && wc_data->cases_written >= dict_get_case_limit (default_dict))
    goto done;
  wc_data->cases_written++;

  /* Write case to LAG queue. */
  if (n_lag)
    lag_case (wc_data->trns_case);

  /* Write case to replacement active file. */
  if (vfm_sink->class->write != NULL) 
    {
      if (compaction_necessary) 
        {
          compact_case (wc_data->sink_case, wc_data->trns_case);
          vfm_sink->class->write (vfm_sink, wc_data->sink_case);
        }
      else
        vfm_sink->class->write (vfm_sink, wc_data->trns_case);
    }
  
  /* Execute temporary transformations. */
  if (!execute_transformations (wc_data->trns_case, t_trns, temp_trns, n_trns,
                                wc_data->cases_written))
    goto done;
  
  /* FILTER, PROCESS IF, post-TEMPORARY N OF CASES. */
  if (filter_case (wc_data->trns_case, wc_data->cases_written)
      || (dict_get_case_limit (temp_dict)
          && wc_data->cases_analyzed >= dict_get_case_limit (temp_dict)))
    goto done;
  wc_data->cases_analyzed++;

  /* Pass case to procedure. */
  if (wc_data->proc_func != NULL)
    wc_data->proc_func (wc_data->trns_case, wc_data->aux);

 done:
  clear_case (wc_data->trns_case);
  return 1;
}

/* Transforms case C using the transformations in TRNS[] with
   indexes FIRST_IDX through LAST_IDX, exclusive.  Case C will
   become case CASE_NUM (1-based) in the output file.  Returns
   zero if the case was filtered out by one of the
   transformations, nonzero otherwise. */
static int
execute_transformations (struct ccase *c,
                         struct trns_header **trns,
                         int first_idx, int last_idx,
                         int case_num) 
{
  int idx;

  for (idx = first_idx; idx != last_idx; )
    {
      int retval = trns[idx]->proc (trns[idx], c, case_num);
      switch (retval)
        {
        case -1:
          idx++;
          break;
          
        case -2:
          return 0;
          
        default:
          idx = retval;
          break;
        }
    }

  return 1;
}

/* Returns nonzero if case C with case number CASE_NUM should be
   exclude as specified on FILTER or PROCESS IF, otherwise
   zero. */
static int
filter_case (const struct ccase *c, int case_num)
{
  /* FILTER. */
  struct variable *filter_var = dict_get_filter (default_dict);
  if (filter_var != NULL) 
    {
      double f = c->data[filter_var->fv].f;
      if (f == 0.0 || f == SYSMIS || is_num_user_missing (f, filter_var))
        return 1;
    }

  /* PROCESS IF. */
  if (process_if_expr != NULL
      && expr_evaluate (process_if_expr, c, case_num, NULL) != 1.0)
    return 1;

  return 0;
}

/* Add C to the lag queue. */
static void
lag_case (const struct ccase *c)
{
  if (lag_count < n_lag)
    lag_count++;
  memcpy (lag_queue[lag_head], c, dict_get_case_size (temp_dict));
  if (++lag_head >= n_lag)
    lag_head = 0;
}

/* Copies case SRC to case DEST, compacting it in the process. */
static void
compact_case (struct ccase *dest, const struct ccase *src)
{
  int i;
  int nval = 0;
  size_t var_cnt;
  
  assert (compaction_necessary);

  /* Copy all the variables except scratch variables from SRC to
     DEST. */
  var_cnt = dict_get_var_cnt (default_dict);
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *v = dict_get_var (default_dict, i);
      
      if (dict_class_from_id (v->name) == DC_SCRATCH)
	continue;

      if (v->type == NUMERIC)
	dest->data[nval++] = src->data[v->fv];
      else
	{
	  int w = DIV_RND_UP (v->width, sizeof (union value));
	  
	  memcpy (&dest->data[nval], &src->data[v->fv], w * sizeof (union value));
	  nval += w;
	}
    }
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
      if (v->init && v->reinit) 
        {
          if (v->type == NUMERIC) 
            c->data[v->fv].f = SYSMIS;
          else
            memset (c->data[v->fv].s, ' ', v->width);
        } 
    }
}

/* Closes the active file. */
static void
close_active_file (void)
{
  /* Free memory for lag queue, and turn off lagging. */
  if (n_lag > 0)
    {
      int i;
      
      for (i = 0; i < n_lag; i++)
	free (lag_queue[i]);
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
  if (compaction_necessary)
    dict_compact_values (default_dict);
    
  /* Free data source. */
  if (vfm_source != NULL) 
    {
      if (vfm_source->class->destroy != NULL)
        vfm_source->class->destroy (vfm_source);
      free (vfm_source);
    }

  /* Old data sink becomes new data source. */
  if (vfm_sink->class->make_source != NULL)
    vfm_source = vfm_sink->class->make_source (vfm_sink);
  else 
    {
      if (vfm_sink->class->destroy != NULL)
        vfm_sink->class->destroy (vfm_sink);
      vfm_source = NULL; 
    }
  free_case_sink (vfm_sink);
  vfm_sink = NULL;

  /* Cancel TEMPORARY, PROCESS IF, FILTER, N OF CASES, vectors,
     and get rid of all the transformations. */
  cancel_temporary ();
  expr_free (process_if_expr);
  process_if_expr = NULL;
  if (dict_get_filter (default_dict) != NULL && !FILTER_before_TEMPORARY)
    dict_set_filter (default_dict, NULL);
  dict_set_case_limit (default_dict, 0);
  dict_clear_vectors (default_dict);
  cancel_transformations ();
}

/* Storage case stream. */

/* Information about storage sink or source. */
struct storage_stream_info 
  {
    size_t case_cnt;            /* Number of cases. */
    size_t case_size;           /* Number of bytes in case. */
    enum { DISK, MEMORY } mode; /* Where is data stored? */

    /* Disk storage.  */
    FILE *file;                 /* Data file. */

    /* Memory storage. */
    int max_cases;              /* Maximum cases before switching to disk. */
    struct case_list *head;     /* First case in list. */
    struct case_list *tail;     /* Last case in list. */
  };

static void open_storage_file (struct storage_stream_info *info);

/* Initializes a storage sink. */
static void
storage_sink_open (struct case_sink *sink)
{
  struct storage_stream_info *info;

  sink->aux = info = xmalloc (sizeof *info);
  info->case_cnt = 0;
  info->case_size = sink->value_cnt * sizeof (union value);
  info->file = NULL;
  info->max_cases = 0;
  info->head = info->tail = NULL;
  if (workspace_overflow) 
    {
      info->mode = DISK;
      open_storage_file (info);
    }
  else 
    {
      info->mode = MEMORY; 
      info->max_cases = (get_max_workspace()
                         / (sizeof (struct case_list) + info->case_size));
    }
}

/* Creates a new temporary file and puts it into INFO. */
static void
open_storage_file (struct storage_stream_info *info) 
{
  info->file = tmpfile ();
  if (info->file == NULL)
    {
      msg (ME, _("An error occurred creating a temporary "
                 "file for use as the active file: %s."),
           strerror (errno));
      err_failure ();
    }
}

/* Writes the VALUE_CNT values in VALUES to FILE. */
static void
write_storage_file (FILE *file, const union value *values, size_t value_cnt) 
{
  if (fwrite (values, sizeof *values * value_cnt, 1, file) != 1)
    {
      msg (ME, _("An error occurred writing to a "
		 "temporary file used as the active file: %s."),
	   strerror (errno));
      err_failure ();
    }
}

/* If INFO represents records in memory, moves them to disk.
   Each comprises VALUE_CNT `union value's. */
static void
storage_to_disk (struct storage_stream_info *info, size_t value_cnt) 
{
  struct case_list *cur, *next;

  if (info->mode == MEMORY) 
    {
      info->mode = DISK;
      open_storage_file (info);
      for (cur = info->head; cur; cur = next)
        {
          next = cur->next;
          write_storage_file (info->file, cur->c.data, value_cnt);
          free (cur);
        }
      info->head = info->tail = NULL; 
    }
}

/* Destroys storage stream represented by INFO. */
static void
destroy_storage_stream_info (struct storage_stream_info *info) 
{
  if (info->mode == DISK) 
    {
      if (info->file != NULL)
        fclose (info->file); 
    }
  else 
    {
      struct case_list *cur, *next;
  
      for (cur = info->head; cur; cur = next)
        {
          next = cur->next;
          free (cur);
        }
    }
  free (info); 
}

/* Writes case C to the storage sink SINK. */
static void
storage_sink_write (struct case_sink *sink, const struct ccase *c)
{
  struct storage_stream_info *info = sink->aux;

  info->case_cnt++;
  if (info->mode == MEMORY) 
    {
      struct case_list *new_case;

      /* Copy case. */
      new_case = xmalloc (sizeof (struct case_list)
                          + ((sink->value_cnt - 1) * sizeof (union value)));
      memcpy (&new_case->c, c, sizeof (union value) * sink->value_cnt);

      /* Append case to linked list. */
      new_case->next = NULL;
      if (info->head != NULL)
        info->tail->next = new_case;
      else
        info->head = new_case;
      info->tail = new_case;

      /* Dump all the cases to disk if we've run out of
         workspace. */
      if (info->case_cnt > info->max_cases) 
        {
          workspace_overflow = 1;
          msg (MW, _("Workspace limit of %d KB (%d cases at %d bytes each) "
                     "overflowed.  Writing active file to disk."),
               get_max_workspace() / 1024, info->max_cases,
               sizeof (struct case_list) + info->case_size);

          storage_to_disk (info, sink->value_cnt);
        }
    }
  else 
    write_storage_file (info->file, c->data, sink->value_cnt);
}

/* Destroys internal data in SINK. */
static void
storage_sink_destroy (struct case_sink *sink)
{
  destroy_storage_stream_info (sink->aux);
}

/* Closes and destroys the sink and returns a storage source to
   read back the written data. */
static struct case_source *
storage_sink_make_source (struct case_sink *sink) 
{
  struct storage_stream_info *info = sink->aux;

  if (info->mode == DISK) 
    {
      /* Rewind the file. */
      assert (info->file != NULL);
      if (fseek (info->file, 0, SEEK_SET) != 0)
        {
          msg (ME, _("An error occurred while attempting to rewind a "
                     "temporary file used as the active file: %s."),
               strerror (errno));
          err_failure ();
        }
    }

  return create_case_source (&storage_source_class, sink->dict, info); 
}

/* Storage sink. */
const struct case_sink_class storage_sink_class = 
  {
    "storage",
    storage_sink_open,
    storage_sink_write,
    storage_sink_destroy,
    storage_sink_make_source,
  };

/* Storage source. */

/* Returns the number of cases that will be read by
   storage_source_read(). */
static int
storage_source_count (const struct case_source *source) 
{
  struct storage_stream_info *info = source->aux;

  return info->case_cnt;
}

/* Reads all cases from the storage source and passes them one by one to
   write_case(). */
static void
storage_source_read (struct case_source *source,
                     struct ccase *c,
                     write_case_func *write_case, write_case_data wc_data)
{
  struct storage_stream_info *info = source->aux;

  if (info->mode == DISK) 
    {
      int i;

      for (i = 0; i < info->case_cnt; i++)
        {
          if (!fread (c, info->case_size, 1, info->file))
            {
              msg (ME, _("An error occurred while attempting to read from "
                         "a temporary file created for the active file: %s."),
                   strerror (errno));
              err_failure ();
              break;
            }

          if (!write_case (wc_data))
            break;
        }
    }
  else 
    {
      while (info->head != NULL) 
        {
          struct case_list *iter = info->head;
          memcpy (c, &iter->c, info->case_size);
          if (!write_case (wc_data)) 
            break;
            
          info->head = iter->next;
          free (iter);
        }
      info->tail = NULL;
    }
}

/* Destroys the source's internal data. */
static void
storage_source_destroy (struct case_source *source)
{
  destroy_storage_stream_info (source->aux);
}

/* Storage source. */
const struct case_source_class storage_source_class = 
  {
    "storage",
    storage_source_count,
    storage_source_read,
    storage_source_destroy,
  };

/* Returns nonzero only if SOURCE is stored on disk (instead of
   in memory). */
int
storage_source_on_disk (const struct case_source *source) 
{
  struct storage_stream_info *info = source->aux;

  return info->mode == DISK;
}

/* Returns the list of cases in storage source SOURCE. */
struct case_list *
storage_source_get_cases (const struct case_source *source) 
{
  struct storage_stream_info *info = source->aux;

  assert (info->mode == MEMORY);
  return info->head;
}

/* Sets the list of cases in memory source SOURCE to CASES. */
void
storage_source_set_cases (const struct case_source *source,
                          struct case_list *cases) 
{
  struct storage_stream_info *info = source->aux;

  assert (info->mode == MEMORY);
  info->head = cases;
}

/* If SOURCE has its cases in memory, writes them to disk. */
void
storage_source_to_disk (struct case_source *source) 
{
  struct storage_stream_info *info = source->aux;

  storage_to_disk (info, source->value_cnt);
}

/* Null sink.  Used by a few procedures that keep track of output
   themselves and would throw away anything that the sink
   contained anyway. */

const struct case_sink_class null_sink_class = 
  {
    "null",
    NULL,
    NULL,
    NULL,
    NULL,
  };

/* Returns a pointer to the lagged case from N_BEFORE cases before the
   current one, or NULL if there haven't been that many cases yet. */
struct ccase *
lagged_case (int n_before)
{
  assert (n_before <= n_lag);
  if (n_before > lag_count)
    return NULL;
  
  {
    int index = lag_head - n_before;
    if (index < 0)
      index += n_lag;
    return lag_queue[index];
  }
}
   
/* Appends TRNS to t_trns[], the list of all transformations to be
   performed on data as it is read from the active file. */
void
add_transformation (struct trns_header * trns)
{
  if (n_trns >= m_trns)
    {
      m_trns += 16;
      t_trns = xrealloc (t_trns, sizeof *t_trns * m_trns);
    }
  t_trns[n_trns] = trns;
  trns->index = n_trns++;
}

/* Cancels all active transformations, including any transformations
   created by the input program. */
void
cancel_transformations (void)
{
  int i;
  for (i = 0; i < n_trns; i++)
    {
      if (t_trns[i]->free)
	t_trns[i]->free (t_trns[i]);
      free (t_trns[i]);
    }
  n_trns = f_trns = 0;
  if (m_trns > 32)
    {
      free (t_trns);
      m_trns = 0;
    }
}

/* Creates a case source with class CLASS and auxiliary data AUX
   and based on dictionary DICT. */
struct case_source *
create_case_source (const struct case_source_class *class,
                    const struct dictionary *dict,
                    void *aux) 
{
  struct case_source *source = xmalloc (sizeof *source);
  source->class = class;
  source->value_cnt = dict_get_next_value_idx (dict);
  source->aux = aux;
  return source;
}

/* Returns nonzero if a case source is "complex". */
int
case_source_is_complex (const struct case_source *source) 
{
  return source != NULL && (source->class == &input_program_source_class
                            || source->class == &file_type_source_class);
}

/* Returns nonzero if CLASS is the class of SOURCE. */
int
case_source_is_class (const struct case_source *source,
                      const struct case_source_class *class) 
{
  return source != NULL && source->class == class;
}

/* Creates a case sink with class CLASS and auxiliary data
   AUX. */
struct case_sink *
create_case_sink (const struct case_sink_class *class,
                  const struct dictionary *dict,
                  void *aux) 
{
  struct case_sink *sink = xmalloc (sizeof *sink);
  sink->class = class;
  sink->dict = dict;
  sink->idx_to_fv = dict_get_compacted_idx_to_fv (dict);
  sink->value_cnt = dict_get_compacted_value_cnt (dict);
  sink->aux = aux;
  return sink;
}

/* Destroys case sink SINK.  It is the caller's responsible to
   call the sink's destroy function, if any. */
void
free_case_sink (struct case_sink *sink) 
{
  free (sink->idx_to_fv);
  free (sink);
}

/* Represents auxiliary data for handling SPLIT FILE. */
struct split_aux_data 
  {
    size_t case_count;          /* Number of cases so far. */
    struct ccase *prev_case;    /* Data in previous case. */

    /* Functions to call... */
    void (*begin_func) (void *);               /* ...before data. */
    int (*proc_func) (struct ccase *, void *); /* ...with data. */
    void (*end_func) (void *);                 /* ...after data. */
    void *func_aux;                            /* Auxiliary data. */ 
  };

static int equal_splits (const struct ccase *, const struct ccase *);
static int procedure_with_splits_callback (struct ccase *, void *);
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
   will be called once. */
void
procedure_with_splits (void (*begin_func) (void *aux),
                       int (*proc_func) (struct ccase *, void *aux),
                       void (*end_func) (void *aux),
                       void *func_aux) 
{
  struct split_aux_data split_aux;

  split_aux.case_count = 0;
  split_aux.prev_case = xmalloc (dict_get_case_size (default_dict));
  split_aux.begin_func = begin_func;
  split_aux.proc_func = proc_func;
  split_aux.end_func = end_func;
  split_aux.func_aux = func_aux;

  procedure (procedure_with_splits_callback, &split_aux);

  if (split_aux.case_count > 0 && end_func != NULL)
    end_func (func_aux);
  free (split_aux.prev_case);
}

/* procedure() callback used by procedure_with_splits(). */
static int
procedure_with_splits_callback (struct ccase *c, void *split_aux_) 
{
  struct split_aux_data *split_aux = split_aux_;

  /* Start a new series if needed. */
  if (split_aux->case_count == 0
      || !equal_splits (c, split_aux->prev_case))
    {
      if (split_aux->case_count > 0 && split_aux->end_func != NULL)
        split_aux->end_func (split_aux->func_aux);

      dump_splits (c);
      memcpy (split_aux->prev_case, c, dict_get_case_size (default_dict));

      if (split_aux->begin_func != NULL)
	split_aux->begin_func (split_aux->func_aux);
    }

  split_aux->case_count++;
  if (split_aux->proc_func != NULL)
    return split_aux->proc_func (c, split_aux->func_aux);
  else
    return 1;
}

/* Compares the SPLIT FILE variables in cases A and B and returns
   nonzero only if they differ. */
static int
equal_splits (const struct ccase *a, const struct ccase *b) 
{
  struct variable *const *split;
  size_t split_cnt;
  size_t i;
    
  split = dict_get_split_vars (default_dict);
  split_cnt = dict_get_split_cnt (default_dict);
  for (i = 0; i < split_cnt; i++)
    {
      struct variable *v = split[i];
      
      switch (v->type)
	{
	case NUMERIC:
	  if (a->data[v->fv].f != b->data[v->fv].f)
            return 0;
	  break;
	case ALPHA:
	  if (memcmp (a->data[v->fv].s, b->data[v->fv].s, v->width))
            return 0;
	  break;
	default:
	  assert (0);
	}
    }

  return 1;
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
  tab_vline (t, TAL_1 | TAL_SPACING, 1, 0, split_cnt);
  tab_vline (t, TAL_1 | TAL_SPACING, 2, 0, split_cnt);
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
      
      data_out (temp_buf, &v->print, &c->data[v->fv]);
      
      temp_buf[v->print.w] = 0;
      tab_text (t, 1, i + 1, TAT_PRINTF, "%.*s", v->print.w, temp_buf);

      val_lab = val_labs_find (v->val_labs, c->data[v->fv]);
      if (val_lab)
	tab_text (t, 2, i + 1, TAB_LEFT, val_lab);
    }
  tab_flags (t, SOMF_NO_TITLE);
  tab_submit (t);
}
