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

#include "debug-print.h"

/* Procedure execution data. */
struct write_case_data
  {
    void (*beginfunc) (void *);
    int (*procfunc) (struct ccase *, void *);
    void (*endfunc) (void *);
    void *aux;
  };

/* The current active file, from which cases are read. */
struct case_source *vfm_source;

/* The replacement active file, to which cases are written. */
struct case_sink *vfm_sink;

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

/* Nonzero means that we've overflowed our allotted workspace.
   After that happens once during a session, we always store the
   active file on disk instead of in memory.  (This policy may be
   too aggressive.) */
static int workspace_overflow = 0;

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
  static int recursive_call;

  struct write_case_data procedure_write_data;
  struct write_case_data split_file_data;

  assert (++recursive_call == 1);

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
  if (vfm_source != NULL) 
    vfm_source->class->read (vfm_source,
                             procedure_write_case, &procedure_write_data);
  close_active_file (&procedure_write_data);

  assert (--recursive_call == 0);
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
  if (vfm_source != NULL)
    vfm_source->class->read (vfm_source, process_active_file_write_case,
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
  vfm_sink->class->write (vfm_sink, temp_case);
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
      if (vfm_sink_info.case_size * vfm_source_info.ncases > set_max_workspace
	  && !workspace_overflow)
	{
	  msg (MW, _("Workspace overflow predicted.  Max workspace is "
		     "currently set to %d KB (%d cases at %d bytes each).  "
		     "Writing active file to disk."),
	       set_max_workspace / 1024, set_max_workspace / vfm_sink_info.case_size,
	       vfm_sink_info.case_size);
	  
	  workspace_overflow = 1;
	}

      if (workspace_overflow)
        vfm_sink = create_case_sink (&disk_sink_class, NULL);
      else
        vfm_sink = create_case_sink (&memory_sink_class, NULL);
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
  
  if (vfm_sink->class->open != NULL)
    vfm_sink->class->open (vfm_sink);
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
  if (vfm_source != NULL) 
    {
      if (vfm_source->class->destroy != NULL)
        vfm_source->class->destroy (vfm_source);
      free (vfm_source);
    }

  vfm_source = vfm_sink->class->make_source (vfm_sink);
  vfm_source_info.ncases = vfm_sink_info.ncases;
  vfm_source_info.nval = compaction_nval;
  vfm_source_info.case_size = (sizeof (struct ccase)
			       + (compaction_nval - 1) * sizeof (union value));

  /* Old data sink is gone now. */
  free (vfm_sink);
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

/* Initializes the disk sink. */
static void
disk_sink_create (struct case_sink *sink)
{
  sink->aux = tmpfile ();
  if (!sink->aux)
    {
      msg (ME, _("An error occurred attempting to create a temporary "
		 "file for use as the active file: %s."),
	   strerror (errno));
      err_failure ();
    }
}

/* Writes temp_case to the disk sink. */
static void
disk_sink_write (struct case_sink *sink, struct ccase *c)
{
  FILE *file = sink->aux;
  union value *src_case;

  if (compaction_necessary)
    {
      compact_case (compaction_case, c);
      src_case = compaction_case->data;
    }
  else src_case = c->data;

  if (fwrite (src_case, sizeof *src_case * compaction_nval, 1, file) != 1)
    {
      msg (ME, _("An error occurred while attempting to write to a "
		 "temporary file used as the active file: %s."),
	   strerror (errno));
      err_failure ();
    }
}

/* Destroys the sink's internal data. */
static void
disk_sink_destroy (struct case_sink *sink)
{
  FILE *file = sink->aux;
  if (file != NULL)
    fclose (file);
}

/* Closes and destroys the sink and returns a disk source to read
   back the written data. */
static struct case_source *
disk_sink_make_source (struct case_sink *sink) 
{
  FILE *file = sink->aux;
  
  /* Rewind the file. */
  assert (file != NULL);
  if (fseek (file, 0, SEEK_SET) != 0)
    {
      msg (ME, _("An error occurred while attempting to rewind a "
		 "temporary file used as the active file: %s."),
	   strerror (errno));
      err_failure ();
    }
  
  return create_case_source (&disk_source_class, file);
}

/* Disk sink. */
const struct case_sink_class disk_sink_class = 
  {
    "disk",
    disk_sink_create,
    disk_sink_write,
    disk_sink_destroy,
    disk_sink_make_source,
  };

/* Disk source. */

/* Reads all cases from the disk source and passes them one by one to
   write_case(). */
static void
disk_source_read (struct case_source *source,
                  write_case_func *write_case, write_case_data wc_data)
{
  FILE *file = source->aux;
  int i;

  for (i = 0; i < vfm_source_info.ncases; i++)
    {
      if (!fread (temp_case, vfm_source_info.case_size, 1, file))
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

/* Destroys the source's internal data. */
static void
disk_source_destroy (struct case_source *source)
{
  FILE *file = source->aux;
  if (file != NULL)
    fclose (file);
}

/* Disk source. */
const struct case_source_class disk_source_class = 
  {
    "disk",
    disk_source_read,
    disk_source_destroy,
  };

/* Memory case stream. */

/* Memory sink data. */
struct memory_sink_info
  {
    int max_cases;              /* Maximum cases before switching to disk. */
    struct case_list *head;     /* First case in list. */
    struct case_list *tail;     /* Last case in list. */
  };

/* Memory source data. */
struct memory_source_info 
  {
    struct case_list *cases;    /* List of cases. */
  };

static void
memory_sink_create (struct case_sink *sink) 
{
  struct memory_sink_info *info;
  
  sink->aux = info = xmalloc (sizeof *info);

  assert (compaction_nval > 0);
  info->max_cases = set_max_workspace / (sizeof (union value) * compaction_nval);
  info->head = info->tail = NULL;
}

static void
memory_sink_write (struct case_sink *sink, struct ccase *c) 
{
  struct memory_sink_info *info = sink->aux;
  size_t case_size;
  struct case_list *new_case;

  case_size = sizeof (struct case_list)
                      + ((compaction_nval - 1) * sizeof (union value));
  new_case = malloc (case_size);

  /* If we've got memory to spare then add it to the linked list. */
  if (vfm_sink_info.ncases <= info->max_cases && new_case != NULL)
    {
      /* Append case to linked list. */
      new_case->next = NULL;
      if (info->head != NULL)
        info->tail->next = new_case;
      else
        info->head = new_case;
      info->tail = new_case;

      /* Copy data into case. */
      if (compaction_necessary)
	compact_case (&new_case->c, c);
      else
	memcpy (&new_case->c, c, sizeof (union value) * compaction_nval);
    }
  else
    {
      /* Out of memory.  Write the active file to disk. */
      struct case_list *cur, *next;

      /* Notify the user. */
      if (!new_case)
	msg (MW, _("Virtual memory exhausted.  Writing active file "
		   "to disk."));
      else
	msg (MW, _("Workspace limit of %d KB (%d cases at %d bytes each) "
		   "overflowed.  Writing active file to disk."),
	     set_max_workspace / 1024, info->max_cases,
	     compaction_nval * sizeof (union value));

      free (new_case);

      /* Switch to a disk sink. */
      vfm_sink = create_case_sink (&disk_sink_class, NULL);
      vfm_sink->class->open (vfm_sink);
      workspace_overflow = 1;

      /* Write the cases to disk and destroy them.  We can't call
         vfm->sink->write() because of compaction. */
      for (cur = info->head; cur; cur = next)
	{
	  next = cur->next;
	  if (fwrite (cur->c.data, sizeof (union value) * compaction_nval, 1,
		      vfm_sink->aux) != 1)
	    {
	      msg (ME, _("An error occurred while attempting to "
			 "write to a temporary file created as the "
			 "active file: %s."),
		   strerror (errno));
	      err_failure ();
	    }
	  free (cur);
	}

      /* Write the current case to disk. */
      vfm_sink->class->write (vfm_sink, c);
    }
}

/* If the data is stored in memory, causes it to be written to disk.
   To be called only *between* procedure()s, not within them. */
void
write_active_file_to_disk (void)
{
  if (case_source_is_class (vfm_source, &memory_source_class))
    {
      struct memory_source_info *info = vfm_source->aux;

      /* Switch to a disk sink. */
      vfm_sink = create_case_sink (&disk_sink_class, NULL);
      vfm_sink->class->open (vfm_sink);
      workspace_overflow = 1;
      
      /* Write the cases to disk and destroy them.  We can't call
         vfm->sink->write() because of compaction. */
      {
	struct case_list *cur, *next;
	
	for (cur = info->cases; cur; cur = next)
	  {
	    next = cur->next;
	    if (fwrite (cur->c.data, sizeof *cur->c.data * compaction_nval, 1,
			vfm_sink->aux) != 1)
	      {
		msg (ME, _("An error occurred while attempting to "
			   "write to a temporary file created as the "
			   "active file: %s."),
		     strerror (errno));
		err_failure ();
	      }
	    free (cur);
	  }
      }
      
      vfm_source = vfm_sink->class->make_source (vfm_sink);
      vfm_sink = NULL;
    }
}

/* Destroy all memory sink data. */
static void
memory_sink_destroy (struct case_sink *sink)
{
  struct memory_sink_info *info = sink->aux;
  struct case_list *cur, *next;
  
  for (cur = info->head; cur; cur = next)
    {
      next = cur->next;
      free (cur);
    }
  free (info);
}

/* Switch the memory stream from sink to source mode. */
static struct case_source *
memory_sink_make_source (struct case_sink *sink)
{
  struct memory_sink_info *sink_info = sink->aux;
  struct memory_source_info *source_info;

  source_info = xmalloc (sizeof *source_info);
  source_info->cases = sink_info->head;

  free (sink_info);

  return create_case_source (&memory_source_class, source_info);
}

const struct case_sink_class memory_sink_class = 
  {
    "memory",
    memory_sink_create,
    memory_sink_write,
    memory_sink_destroy,
    memory_sink_make_source,
  };

/* Reads the case stream from memory and passes it to write_case(). */
static void
memory_source_read (struct case_source *source,
                    write_case_func *write_case, write_case_data wc_data)
{
  struct memory_source_info *info = source->aux;

  while (info->cases != NULL) 
    {
      struct case_list *iter = info->cases;
      info->cases = iter->next;
      memcpy (temp_case, &iter->c, vfm_source_info.case_size);
      free (iter);
      
      if (!write_case (wc_data))
	return;
    }
}

/* Destroy all memory source data. */
static void
memory_source_destroy (struct case_source *source)
{
  struct memory_source_info *info = source->aux;
  struct case_list *cur, *next;
  
  for (cur = info->cases; cur; cur = next)
    {
      next = cur->next;
      free (cur);
    }
  free (info);
}

struct case_list *
memory_source_get_cases (const struct case_source *source) 
{
  struct memory_source_info *info = source->aux;

  return info->cases;
}

void
memory_source_set_cases (const struct case_source *source,
                         struct case_list *cases) 
{
  struct memory_source_info *info = source->aux;

  info->cases = cases;
}

/* Memory stream. */
const struct case_source_class memory_source_class = 
  {
    "memory",
    memory_source_read,
    memory_source_destroy,
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
	  vfm_sink->class->write (vfm_sink, temp_case);

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
	  if (c->data[v->fv].f != prev_case->data[v->fv].f)
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

struct case_source *
create_case_source (const struct case_source_class *class, void *aux) 
{
  struct case_source *source = xmalloc (sizeof *source);
  source->class = class;
  source->aux = aux;
  return source;
}

int
case_source_is_complex (const struct case_source *source) 
{
  return source != NULL && (source->class == &input_program_source_class
                            || source->class == &file_type_source_class);
}

int
case_source_is_class (const struct case_source *source,
                      const struct case_source_class *class) 
{
  return source != NULL && source->class == class;

}

struct case_sink *
create_case_sink (const struct case_sink_class *class, void *aux) 
{
  struct case_sink *sink = xmalloc (sizeof *sink);
  sink->class = class;
  sink->aux = aux;
  return sink;
}

