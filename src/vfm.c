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
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#if HAVE_UNISTD_H
#include <unistd.h>	/* Required by SunOS4. */
#endif
#include "alloc.h"
#include "approx.h"
#include "do-ifP.h"
#include "error.h"
#include "expr.h"
#include "misc.h"
#include "random.h"
#include "som.h"
#include "str.h"
#include "tab.h"
#include "var.h"
#include "value-labels.h"

/*
   Virtual File Manager (vfm):

   vfm is used to process data files.  It uses the model that data is
   read from one stream (the data source), then written to another
   (the data sink).  The data source is then deleted and the data sink
   becomes the data source for the next procedure. */

#include "debug-print.h"

/* Procedure execution data. */
struct write_case_data
  {
    void (*beginfunc) (void *);
    int (*procfunc) (struct ccase *, void *);
    void (*endfunc) (void *);
    void *aux;
  };

/* This is used to read from the active file. */
struct case_stream *vfm_source;

/* This is used to write to the replacement active file. */
struct case_stream *vfm_sink;

/* Information about the data source. */
struct stream_info vfm_source_info;

/* Information about the data sink. */
struct stream_info vfm_sink_info;

/* Nonzero if the case needs to have values deleted before being
   stored, zero otherwise. */
int compaction_necessary;

/* Number of values after compaction, or the same as
   vfm_sink_info.nval, if compaction is not necessary. */
int compaction_nval;

/* Temporary case buffer with enough room for `compaction_nval'
   `value's. */
struct ccase *compaction_case;

/* Within a session, when paging is turned on, it is never turned back
   off.  This policy might be too aggressive. */
static int paging = 0;

/* Time at which vfm was last invoked. */
time_t last_vfm_invocation;

/* Number of cases passed to proc_func(). */
static int case_count;

/* Lag queue. */
int n_lag;			/* Number of cases to lag. */
static int lag_count;		/* Number of cases in lag_queue so far. */
static int lag_head;		/* Index where next case will be added. */
static struct ccase **lag_queue; /* Array of n_lag ccase * elements. */

static void open_active_file (void);
static void close_active_file (struct write_case_data *);
static int SPLIT_FILE_procfunc (struct ccase *, void *);
static void finish_compaction (void);
static void lag_case (void);
static int procedure_write_case (struct write_case_data *);
static void clear_temp_case (void);
static int exclude_this_case (void);

/* Public functions. */

/* Reads all the cases from the active file, transforms them by
   the active set of transformations, calls PROCFUNC with CURCASE
   set to the case, and writes them to a new active file.

   Divides the active file into zero or more series of one or more
   cases each.  BEGINFUNC is called before each series.  ENDFUNC is
   called after each series.

   Arbitrary user-specified data AUX is passed to BEGINFUNC,
   PROCFUNC, and ENDFUNC as auxiliary data. */
void
procedure (void (*beginfunc) (void *),
	   int (*procfunc) (struct ccase *curcase, void *),
	   void (*endfunc) (void *),
           void *aux)
{
  struct write_case_data procedure_write_data;
  struct write_case_data split_file_data;

  if (dict_get_split_cnt (default_dict) == 0) 
    {
      /* Normally we just use the data passed by the user. */
      procedure_write_data.beginfunc = beginfunc;
      procedure_write_data.procfunc = procfunc;
      procedure_write_data.endfunc = endfunc;
      procedure_write_data.aux = aux;
    }
  else
    {
      /* Under SPLIT FILE, we add a layer of indirection. */
      procedure_write_data.beginfunc = NULL;
      procedure_write_data.procfunc = SPLIT_FILE_procfunc;
      procedure_write_data.endfunc = endfunc;
      procedure_write_data.aux = &split_file_data;

      split_file_data.beginfunc = beginfunc;
      split_file_data.procfunc = procfunc;
      split_file_data.endfunc = endfunc;
      split_file_data.aux = aux;
    }

  last_vfm_invocation = time (NULL);

  open_active_file ();
  vfm_source->read (procedure_write_case, &procedure_write_data);
  close_active_file (&procedure_write_data);
}

/* Active file processing support.  Subtly different semantics from
   procedure(). */

static int process_active_file_write_case (struct write_case_data *data);

/* The casefunc might want us to stop calling it. */
static int not_canceled;

/* Reads all the cases from the active file and passes them one-by-one
   to CASEFUNC in temp_case.  Before any cases are passed, calls
   BEGINFUNC.  After all the cases have been passed, calls ENDFUNC.
   BEGINFUNC, CASEFUNC, and ENDFUNC can write temp_case to the output
   file by calling process_active_file_output_case().

   process_active_file() ignores TEMPORARY, SPLIT FILE, and N. */
void
process_active_file (void (*beginfunc) (void *),
		     int (*casefunc) (struct ccase *curcase, void *),
		     void (*endfunc) (void *),
                     void *aux)
{
  struct write_case_data process_active_write_data;

  process_active_write_data.beginfunc = beginfunc;
  process_active_write_data.procfunc = casefunc;
  process_active_write_data.endfunc = endfunc;
  process_active_write_data.aux = aux;

  not_canceled = 1;

  open_active_file ();
  beginfunc (aux);
  
  /* There doesn't necessarily need to be an active file. */
  if (vfm_source)
    vfm_source->read (process_active_file_write_case,
                      &process_active_write_data);
  
  endfunc (aux);
  close_active_file (&process_active_write_data);
}

/* Pass the current case to casefunc. */
static int
process_active_file_write_case (struct write_case_data *data)
{
  /* Index of current transformation. */
  int cur_trns;

  for (cur_trns = f_trns ; cur_trns != temp_trns; )
    {
      int code;
	
      code = t_trns[cur_trns]->proc (t_trns[cur_trns], temp_case);
      switch (code)
	{
	case -1:
	  /* Next transformation. */
	  cur_trns++;
	  break;
	case -2:
	  /* Delete this case. */
	  goto done;
	default:
	  /* Go to that transformation. */
	  cur_trns = code;
	  break;
	}
    }

  if (n_lag)
    lag_case ();
	  
  /* Call the procedure if FILTER and PROCESS IF don't prohibit it. */
  if (not_canceled && !exclude_this_case ())
    not_canceled = data->procfunc (temp_case, data->aux);
  
  case_count++;
  
 done:
  clear_temp_case ();

  return 1;
}

/* Write temp_case to the active file. */
void
process_active_file_output_case (void)
{
  vfm_sink_info.ncases++;
  vfm_sink->write ();
}

/* Opening the active file. */

/* It might be usefully noted that the following several functions are
   given in the order that they are called by open_active_file(). */

/* Prepare to write to the replacement active file. */
static void
prepare_for_writing (void)
{
  /* FIXME: If ALL the conditions listed below hold true, then the
     replacement active file is guaranteed to be identical to the
     original active file:

     1. TEMPORARY was the first transformation, OR, there were no
     transformations at all.

     2. Input is not coming from an input program.

     3. Compaction is not necessary.

     So, in this case, we shouldn't have to replace the active
     file--it's just a waste of time and space. */

  vfm_sink_info.ncases = 0;
  vfm_sink_info.nval = dict_get_next_value_idx (default_dict);
  vfm_sink_info.case_size = dict_get_case_size (default_dict);
  
  if (vfm_sink == NULL)
    {
      if (vfm_sink_info.case_size * vfm_source_info.ncases > MAX_WORKSPACE
	  && !paging)
	{
	  msg (MW, _("Workspace overflow predicted.  Max workspace is "
		     "currently set to %d KB (%d cases at %d bytes each).  "
		     "Paging active file to disk."),
	       MAX_WORKSPACE / 1024, MAX_WORKSPACE / vfm_sink_info.case_size,
	       vfm_sink_info.case_size);
	  
	  paging = 1;
	}
      
      vfm_sink = paging ? &vfm_disk_stream : &vfm_memory_stream;
    }
}

/* Arrange for compacting the output cases for storage. */
static void
arrange_compaction (void)
{
  int count_values = 0;

  {
    int i;
    
    /* Count up the number of `value's that will be output. */
    for (i = 0; i < dict_get_var_cnt (temp_dict); i++) 
      {
        struct variable *v = dict_get_var (temp_dict, i);

        if (v->name[0] != '#')
          {
            assert (v->nv > 0);
            count_values += v->nv;
          } 
      }
    assert (temporary == 2
            || count_values <= dict_get_next_value_idx (temp_dict));
  }
  
  /* Compaction is only necessary if the number of `value's to output
     differs from the number already present. */
  compaction_nval = count_values;
  if (temporary == 2 || count_values != dict_get_next_value_idx (temp_dict))
    compaction_necessary = 1;
  else
    compaction_necessary = 0;
  
  if (vfm_sink->init)
    vfm_sink->init ();
}

/* Prepares the temporary case and compaction case. */
static void
make_temp_case (void)
{
  temp_case = xmalloc (vfm_sink_info.case_size);

  if (compaction_necessary)
    compaction_case = xmalloc (sizeof (struct ccase)
			       + sizeof (union value) * (compaction_nval - 1));
}

#if DEBUGGING
/* Returns the name of the variable that owns the index CCASE_INDEX
   into ccase. */
static const char *
index_to_varname (int ccase_index)
{
  int i;

  for (i = 0; i < default_dict.nvar; i++)
    {
      struct variable *v = default_dict.var[i];
      
      if (ccase_index >= v->fv && ccase_index < v->fv + v->nv)
	return default_dict.var[i]->name;
    }
  return _("<NOVAR>");
}
#endif

/* Initializes temp_case from the vectors that say which `value's
   need to be initialized just once, and which ones need to be
   re-initialized before every case. */
static void
vector_initialization (void)
{
  size_t var_cnt = dict_get_var_cnt (default_dict);
  size_t i;
  
  for (i = 0; i < var_cnt; i++) 
    {
      struct variable *v = dict_get_var (default_dict, i);

      if (v->type == NUMERIC) 
        {
          if (v->reinit)
            temp_case->data[v->fv].f = 0.0;
          else
            temp_case->data[v->fv].f = SYSMIS;
        }
      else
        memset (temp_case->data[v->fv].s, ' ', v->width);
    }
}

/* Sets all the lag-related variables based on value of n_lag. */
static void
setup_lag (void)
{
  int i;
  
  if (n_lag == 0)
    return;

  lag_count = 0;
  lag_head = 0;
  lag_queue = xmalloc (n_lag * sizeof *lag_queue);
  for (i = 0; i < n_lag; i++)
    lag_queue[i] = xmalloc (dict_get_case_size (temp_dict));
}

/* There is a lot of potential confusion in the vfm and related
   routines over the number of `value's at each stage of the process.
   Here is each nval count, with explanation, as set up by
   open_active_file():

   vfm_source_info.nval: Number of `value's in the cases returned by
   the source stream.  This value turns out not to be very useful, but
   we maintain it anyway.

   vfm_sink_info.nval: Number of `value's in the cases after all
   transformations have been performed.  Never less than
   vfm_source_info.nval.

   temp_dict->nval: Number of `value's in the cases after the
   transformations leading up to TEMPORARY have been performed.  If
   TEMPORARY was not specified, this is equal to vfm_sink_info.nval.
   Never less than vfm_sink_info.nval.

   compaction_nval: Number of `value's in the cases after the
   transformations leading up to TEMPORARY have been performed and the
   case has been compacted by compact_case(), if compaction is
   necessary.  This the number of `value's in the cases saved by the
   sink stream.  (However, note that the cases passed to the sink
   stream have not yet been compacted.  It is the responsibility of
   the data sink to call compact_case().)  This may be less than,
   greater than, or equal to vfm_source_info.nval.  `compaction'
   becomes the new value of default_dict.nval after the procedure is
   completed.

   default_dict.nval: This is often an alias for temp_dict->nval.  As
   such it can really have no separate existence until the procedure
   is complete.  For this reason it should *not* be referenced inside
   the execution of a procedure. */
/* Makes all preparations for reading from the data source and writing
   to the data sink. */
static void
open_active_file (void)
{
  /* Sometimes we want to refer to the dictionary that applies to the
     data actually written to the sink.  This is either temp_dict or
     default_dict.  However, if TEMPORARY is not on, then temp_dict
     does not apply.  So, we can set temp_dict to default_dict in this
     case. */
  if (!temporary)
    {
      temp_trns = n_trns;
      temp_dict = default_dict;
    }

  /* No cases passed to the procedure yet. */
  case_count = 0;

  /* The rest. */
  prepare_for_writing ();
  arrange_compaction ();
  make_temp_case ();
  vector_initialization ();
  discard_ctl_stack ();
  setup_lag ();

  /* Debug output. */
  debug_printf (("vfm: reading from %s source, writing to %s sink.\n",
		 vfm_source->name, vfm_sink->name));
  debug_printf (("vfm: vfm_source_info.nval=%d, vfm_sink_info.nval=%d, "
		 "temp_dict->nval=%d, compaction_nval=%d, "
		 "default_dict.nval=%d\n",
		 vfm_source_info.nval, vfm_sink_info.nval, temp_dict->nval,
		 compaction_nval, default_dict.nval));
}

/* Closes the active file. */
static void
close_active_file (struct write_case_data *data)
{
  /* Close the current case group. */
  if (case_count && data->endfunc != NULL)
    data->endfunc (data->aux);

  /* Stop lagging (catch up?). */
  if (n_lag)
    {
      int i;
      
      for (i = 0; i < n_lag; i++)
	free (lag_queue[i]);
      free (lag_queue);
      n_lag = 0;
    }
  
  /* Assume the dictionary from right before TEMPORARY, if any.  Turn
     off TEMPORARY. */
  if (temporary)
    {
      dict_destroy (default_dict);
      default_dict = temp_dict;
      temp_dict = NULL;
    }

  /* Finish compaction. */
  if (compaction_necessary)
    finish_compaction ();
    
  /* Old data sink --> New data source. */
  if (vfm_source && vfm_source->destroy_source)
    vfm_source->destroy_source ();
  
  vfm_source = vfm_sink;
  vfm_source_info.ncases = vfm_sink_info.ncases;
  vfm_source_info.nval = compaction_nval;
  vfm_source_info.case_size = (sizeof (struct ccase)
			       + (compaction_nval - 1) * sizeof (union value));
  if (vfm_source->mode)
    vfm_source->mode ();

  /* Old data sink is gone now. */
  vfm_sink = NULL;

  /* Cancel TEMPORARY. */
  cancel_temporary ();

  /* Free temporary cases. */
  free (temp_case);
  temp_case = NULL;

  free (compaction_case);
  compaction_case = NULL;

  /* Cancel PROCESS IF. */
  expr_free (process_if_expr);
  process_if_expr = NULL;

  /* Cancel FILTER if temporary. */
  if (dict_get_filter (default_dict) != NULL && !FILTER_before_TEMPORARY)
    dict_set_filter (default_dict, NULL);

  /* Cancel transformations. */
  cancel_transformations ();

  /* Turn off case limiter. */
  dict_set_case_limit (default_dict, 0);

  /* Clear VECTOR vectors. */
  dict_clear_vectors (default_dict);

  debug_printf (("vfm: procedure complete\n\n"));
}

/* Disk case stream. */

/* Associated files. */
FILE *disk_source_file;
FILE *disk_sink_file;

/* Initializes the disk sink. */
static void
disk_stream_init (void)
{
  disk_sink_file = tmpfile ();
  if (!disk_sink_file)
    {
      msg (ME, _("An error occurred attempting to create a temporary "
		 "file for use as the active file: %s."),
	   strerror (errno));
      err_failure ();
    }
}

/* Reads all cases from the disk source and passes them one by one to
   write_case(). */
static void
disk_stream_read (write_case_func *write_case, write_case_data wc_data)
{
  int i;

  for (i = 0; i < vfm_source_info.ncases; i++)
    {
      if (!fread (temp_case, vfm_source_info.case_size, 1, disk_source_file))
	{
	  msg (ME, _("An error occurred while attempting to read from "
	       "a temporary file created for the active file: %s."),
	       strerror (errno));
	  err_failure ();
	  return;
	}

      if (!write_case (wc_data))
	return;
    }
}

/* Writes temp_case to the disk sink. */
static void
disk_stream_write (void)
{
  union value *src_case;

  if (compaction_necessary)
    {
      compact_case (compaction_case, temp_case);
      src_case = (union value *) compaction_case;
    }
  else src_case = (union value *) temp_case;

  if (fwrite (src_case, sizeof *src_case * compaction_nval, 1,
	      disk_sink_file) != 1)
    {
      msg (ME, _("An error occurred while attempting to write to a "
		 "temporary file used as the active file: %s."),
	   strerror (errno));
      err_failure ();
    }
}

/* Switches the stream from a sink to a source. */
static void
disk_stream_mode (void)
{
  /* Rewind the sink. */
  if (fseek (disk_sink_file, 0, SEEK_SET) != 0)
    {
      msg (ME, _("An error occurred while attempting to rewind a "
		 "temporary file used as the active file: %s."),
	   strerror (errno));
      err_failure ();
    }
  
  /* Sink --> source variables. */
  disk_source_file = disk_sink_file;
}

/* Destroys the source's internal data. */
static void
disk_stream_destroy_source (void)
{
  if (disk_source_file)
    {
      fclose (disk_source_file);
      disk_source_file = NULL;
    }
}

/* Destroys the sink's internal data. */
static void
disk_stream_destroy_sink (void)
{
  if (disk_sink_file)
    {
      fclose (disk_sink_file);
      disk_sink_file = NULL;
    }
}

/* Disk stream. */
struct case_stream vfm_disk_stream = 
  {
    disk_stream_init,
    disk_stream_read,
    disk_stream_write,
    disk_stream_mode,
    disk_stream_destroy_source,
    disk_stream_destroy_sink,
    "disk",
  };

/* Memory case stream. */

/* List of cases stored in the stream. */
struct case_list *memory_source_cases;
struct case_list *memory_sink_cases;

/* Current case. */
struct case_list *memory_sink_iter;

/* Maximum number of cases. */
int memory_sink_max_cases;

/* Initializes the memory stream variables for writing. */
static void
memory_stream_init (void)
{
  memory_sink_cases = NULL;
  memory_sink_iter = NULL;
  
  assert (compaction_nval);
  memory_sink_max_cases = MAX_WORKSPACE / (sizeof (union value) * compaction_nval);
}

/* Reads the case stream from memory and passes it to write_case(). */
static void
memory_stream_read (write_case_func *write_case, write_case_data wc_data)
{
  while (memory_source_cases != NULL)
    {
      memcpy (temp_case, &memory_source_cases->c, vfm_source_info.case_size);
      
      {
	struct case_list *current = memory_source_cases;
	memory_source_cases = memory_source_cases->next;
	free (current);
      }
      
      if (!write_case (wc_data))
	return;
    }
}

/* Writes temp_case to the memory stream. */
static void
memory_stream_write (void)
{
  struct case_list *new_case = malloc (sizeof (struct case_list)
				       + ((compaction_nval - 1)
					  * sizeof (union value)));

  /* If we've got memory to spare then add it to the linked list. */
  if (vfm_sink_info.ncases <= memory_sink_max_cases && new_case != NULL)
    {
      if (compaction_necessary)
	compact_case (&new_case->c, temp_case);
      else
	memcpy (&new_case->c, temp_case, sizeof (union value) * compaction_nval);

      /* Append case to linked list. */
      if (memory_sink_cases)
	memory_sink_iter = memory_sink_iter->next = new_case;
      else
	memory_sink_iter = memory_sink_cases = new_case;
    }
  else
    {
      /* Out of memory.  Write the active file to disk. */
      struct case_list *cur, *next;

      /* Notify the user. */
      if (!new_case)
	msg (MW, _("Virtual memory exhausted.  Paging active file "
		   "to disk."));
      else
	msg (MW, _("Workspace limit of %d KB (%d cases at %d bytes each) "
		   "overflowed.  Paging active file to disk."),
	     MAX_WORKSPACE / 1024, memory_sink_max_cases,
	     compaction_nval * sizeof (union value));

      free (new_case);

      /* Switch to a disk sink. */
      vfm_sink = &vfm_disk_stream;
      vfm_sink->init ();
      paging = 1;

      /* Terminate the list. */
      if (memory_sink_iter)
	memory_sink_iter->next = NULL;

      /* Write the cases to disk and destroy them.  We can't call
         vfm->sink->write() because of compaction. */
      for (cur = memory_sink_cases; cur; cur = next)
	{
	  next = cur->next;
	  if (fwrite (cur->c.data, sizeof (union value) * compaction_nval, 1,
		      disk_sink_file) != 1)
	    {
	      msg (ME, _("An error occurred while attempting to "
			 "write to a temporary file created as the "
			 "active file, while paging to disk: %s."),
		   strerror (errno));
	      err_failure ();
	    }
	  free (cur);
	}

      /* Write the current case to disk. */
      vfm_sink->write ();
    }
}

/* If the data is stored in memory, causes it to be written to disk.
   To be called only *between* procedure()s, not within them. */
void
page_to_disk (void)
{
  if (vfm_source == &vfm_memory_stream)
    {
      /* Switch to a disk sink. */
      vfm_sink = &vfm_disk_stream;
      vfm_sink->init ();
      paging = 1;
      
      /* Write the cases to disk and destroy them.  We can't call
         vfm->sink->write() because of compaction. */
      {
	struct case_list *cur, *next;
	
	for (cur = memory_source_cases; cur; cur = next)
	  {
	    next = cur->next;
	    if (fwrite (cur->c.data, sizeof *cur->c.data * compaction_nval, 1,
			disk_sink_file) != 1)
	      {
		msg (ME, _("An error occurred while attempting to "
			   "write to a temporary file created as the "
			   "active file, while paging to disk: %s."),
		     strerror (errno));
		err_failure ();
	      }
	    free (cur);
	  }
      }
      
      vfm_source = &vfm_disk_stream;
      vfm_source->mode ();

      vfm_sink = NULL;
    }
}

/* Switch the memory stream from sink to source mode. */
static void
memory_stream_mode (void)
{
  /* Terminate the list. */
  if (memory_sink_iter)
    memory_sink_iter->next = NULL;

  /* Sink --> source variables. */
  memory_source_cases = memory_sink_cases;
  memory_sink_cases = NULL;
}

/* Destroy all memory source data. */
static void
memory_stream_destroy_source (void)
{
  struct case_list *cur, *next;
  
  for (cur = memory_source_cases; cur; cur = next)
    {
      next = cur->next;
      free (cur);
    }
  memory_source_cases = NULL;
}

/* Destroy all memory sink data. */
static void
memory_stream_destroy_sink (void)
{
  struct case_list *cur, *next;
  
  for (cur = memory_sink_cases; cur; cur = next)
    {
      next = cur->next;
      free (cur);
    }
  memory_sink_cases = NULL;
}
  
/* Memory stream. */
struct case_stream vfm_memory_stream = 
  {
    memory_stream_init,
    memory_stream_read,
    memory_stream_write,
    memory_stream_mode,
    memory_stream_destroy_source,
    memory_stream_destroy_sink,
    "memory",
  };

#include "debug-print.h"

/* Add temp_case to the lag queue. */
static void
lag_case (void)
{
  if (lag_count < n_lag)
    lag_count++;
  memcpy (lag_queue[lag_head], temp_case,
          dict_get_case_size (temp_dict));
  if (++lag_head >= n_lag)
    lag_head = 0;
}

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
   
/* Transforms temp_case and writes it to the replacement active file
   if advisable.  Returns nonzero if more cases can be accepted, zero
   otherwise.  Do not call this function again after it has returned
   zero once.  */
int
procedure_write_case (write_case_data wc_data)
{
  /* Index of current transformation. */
  int cur_trns;

  /* Return value: whether it's reasonable to write any more cases. */
  int more_cases = 1;

  debug_printf ((_("transform: ")));

  cur_trns = f_trns;
  for (;;)
    {
      /* Output the case if this is temp_trns. */
      if (cur_trns == temp_trns)
	{
	  debug_printf (("REC"));

	  if (n_lag)
	    lag_case ();
	  
	  vfm_sink_info.ncases++;
	  vfm_sink->write ();

	  if (dict_get_case_limit (default_dict))
	    more_cases = (vfm_sink_info.ncases
                          < dict_get_case_limit (default_dict));
	}

      /* Are we done? */
      if (cur_trns >= n_trns)
	break;
      
      debug_printf (("$%d", cur_trns));

      /* Decide which transformation should come next. */
      {
	int code;
	
	code = t_trns[cur_trns]->proc (t_trns[cur_trns], temp_case);
	switch (code)
	  {
	  case -1:
	    /* Next transformation. */
	    cur_trns++;
	    break;
	  case -2:
	    /* Delete this case. */
	    goto done;
	  default:
	    /* Go to that transformation. */
	    cur_trns = code;
	    break;
	  }
      }
    }

  /* Call the beginning of group function. */
  if (!case_count && wc_data->beginfunc != NULL)
    wc_data->beginfunc (wc_data->aux);

  /* Call the procedure if there is one and FILTER and PROCESS IF
     don't prohibit it. */
  if (wc_data->procfunc != NULL && !exclude_this_case ())
    wc_data->procfunc (temp_case, wc_data->aux);

  case_count++;
  
done:
  debug_putc ('\n', stdout);

  clear_temp_case ();
  
  /* Return previously determined value. */
  return more_cases;
}

/* Clears the variables in the temporary case that need to be
   cleared between processing cases.  */
static void
clear_temp_case (void)
{
  /* FIXME?  This is linear in the number of variables, but
     doesn't need to be, so it's an easy optimization target. */
  size_t var_cnt = dict_get_var_cnt (default_dict);
  size_t i;
  
  for (i = 0; i < var_cnt; i++) 
    {
      struct variable *v = dict_get_var (default_dict, i);
      if (v->init && v->reinit) 
        {
          if (v->type == NUMERIC) 
            temp_case->data[v->fv].f = SYSMIS;
          else
            memset (temp_case->data[v->fv].s, ' ', v->width);
        } 
    }
}

/* Returns nonzero if this case should be exclude as specified on
   FILTER or PROCESS IF, otherwise zero. */
static int
exclude_this_case (void)
{
  /* FILTER. */
  struct variable *filter_var = dict_get_filter (default_dict);
  if (filter_var != NULL) 
    {
      double f = temp_case->data[filter_var->fv].f;
      if (f == 0.0 || f == SYSMIS || is_num_user_missing (f, filter_var))
        return 1;
    }

  /* PROCESS IF. */
  if (process_if_expr != NULL
      && expr_evaluate (process_if_expr, temp_case, NULL) != 1.0)
    return 1;

  return 0;
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

/* Dumps out the values of all the split variables for the case C. */
static void
dump_splits (struct ccase *c)
{
  struct variable *const *split;
  struct tab_table *t;
  size_t split_cnt;
  int i;

  split_cnt = dict_get_split_cnt (default_dict);
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

/* This procfunc is substituted for the user-supplied procfunc when
   SPLIT FILE is active.  This function forms a wrapper around that
   procfunc by dividing the input into series. */
static int
SPLIT_FILE_procfunc (struct ccase *c, void *data_)
{
  struct write_case_data *data = data_;
  static struct ccase *prev_case;
  struct variable *const *split;
  size_t split_cnt;
  size_t i;

  /* The first case always begins a new series.  We also need to
     preserve the values of the case for later comparison. */
  if (case_count == 0)
    {
      if (prev_case)
	free (prev_case);
      prev_case = xmalloc (vfm_sink_info.case_size);
      memcpy (prev_case, c, vfm_sink_info.case_size);

      dump_splits (c);
      if (data->beginfunc != NULL)
	data->beginfunc (data->aux);
      
      return data->procfunc (c, data->aux);
    }

  /* Compare the value of each SPLIT FILE variable to the values on
     the previous case. */
  split = dict_get_split_vars (default_dict);
  split_cnt = dict_get_split_cnt (default_dict);
  for (i = 0; i < split_cnt; i++)
    {
      struct variable *v = split[i];
      
      switch (v->type)
	{
	case NUMERIC:
	  if (approx_ne (c->data[v->fv].f, prev_case->data[v->fv].f))
	    goto not_equal;
	  break;
	case ALPHA:
	  if (memcmp (c->data[v->fv].s, prev_case->data[v->fv].s, v->width))
	    goto not_equal;
	  break;
	default:
	  assert (0);
	}
    }
  return data->procfunc (c, data->aux);
  
not_equal:
  /* The values of the SPLIT FILE variable are different from the
     values on the previous case.  That means that it's time to begin
     a new series. */
  if (data->endfunc != NULL)
    data->endfunc (data->aux);
  dump_splits (c);
  if (data->beginfunc != NULL)
    data->beginfunc (data->aux);
  memcpy (prev_case, c, vfm_sink_info.case_size);
  return data->procfunc (c, data->aux);
}

/* Case compaction. */

/* Copies case SRC to case DEST, compacting it in the process. */
void
compact_case (struct ccase *dest, const struct ccase *src)
{
  int i;
  int nval = 0;
  size_t var_cnt;
  
  assert (compaction_necessary);

  if (temporary == 2)
    {
      if (dest != compaction_case)
	memcpy (dest, compaction_case, sizeof (union value) * compaction_nval);
      return;
    }

  /* Copy all the variables except the scratch variables from SRC to
     DEST. */
  var_cnt = dict_get_var_cnt (default_dict);
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *v = dict_get_var (default_dict, i);
      
      if (v->name[0] == '#')
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

/* Reassigns `fv' for each variable.  Deletes scratch variables. */
static void
finish_compaction (void)
{
  int i;

  for (i = 0; i < dict_get_var_cnt (default_dict); )
    {
      struct variable *v = dict_get_var (default_dict, i);

      if (v->name[0] == '#') 
        dict_delete_var (default_dict, v);
      else
        i++;
    }
  dict_compact_values (default_dict);
}

  
