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
#include "sort.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "algorithm.h"
#include "alloc.h"
#include "command.h"
#include "error.h"
#include "expr.h"
#include "lexer.h"
#include "misc.h"
#include "settings.h"
#include "str.h"
#include "var.h"
#include "vfm.h"
#include "vfmP.h"

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#include "debug-print.h"

/* Other prototypes. */
static int compare_record (const union value *, const union value *,
                           const struct sort_cases_pgm *, int *idx_to_fv);
static int compare_case_lists (const void *, const void *, void *);
static struct internal_sort *do_internal_sort (struct sort_cases_pgm *,
                                               int separate);
static void destroy_internal_sort (struct internal_sort *);
static struct external_sort *do_external_sort (struct sort_cases_pgm *,
                                               int separate);
static void destroy_external_sort (struct external_sort *);
struct sort_cases_pgm *parse_sort (void);

/* Performs the SORT CASES procedures. */
int
cmd_sort_cases (void)
{
  struct sort_cases_pgm *scp;
  int success;

  lex_match_id ("SORT");
  lex_match_id ("CASES");
  lex_match (T_BY);

  scp = parse_sort ();
  if (scp == NULL)
    return CMD_FAILURE;

  if (temporary != 0)
    {
      msg (SE, _("SORT CASES may not be used after TEMPORARY.  "
                 "Temporary transformations will be made permanent."));
      cancel_temporary (); 
    }

  success = sort_cases (scp, 0);
  destroy_sort_cases_pgm (scp);
  if (success)
    return lex_end_of_command ();
  else
    return CMD_FAILURE;
}

/* Parses a list of sort keys and returns a struct sort_cases_pgm
   based on it.  Returns a null pointer on error. */
struct sort_cases_pgm *
parse_sort (void)
{
  struct sort_cases_pgm *scp;

  scp = xmalloc (sizeof *scp);
  scp->ref_cnt = 1;
  scp->vars = NULL;
  scp->dirs = NULL;
  scp->var_cnt = 0;
  scp->isrt = NULL;
  scp->xsrt = NULL;

  do
    {
      int prev_var_cnt = scp->var_cnt;
      enum sort_direction direction = SRT_ASCEND;

      /* Variables. */
      if (!parse_variables (default_dict, &scp->vars, &scp->var_cnt,
			    PV_NO_DUPLICATE | PV_APPEND | PV_NO_SCRATCH))
        goto error;

      /* Sort direction. */
      if (lex_match ('('))
	{
	  if (lex_match_id ("D") || lex_match_id ("DOWN"))
	    direction = SRT_DESCEND;
	  else if (!lex_match_id ("A") && !lex_match_id ("UP"))
	    {
	      msg (SE, _("`A' or `D' expected inside parentheses."));
              goto error;
	    }
	  if (!lex_match (')'))
	    {
	      msg (SE, _("`)' expected."));
              goto error;
	    }
	}
      scp->dirs = xrealloc (scp->dirs, sizeof *scp->dirs * scp->var_cnt);
      for (; prev_var_cnt < scp->var_cnt; prev_var_cnt++)
        scp->dirs[prev_var_cnt] = direction;
    }
  while (token != '.' && token != '/');
  
  return scp;

 error:
  destroy_sort_cases_pgm (scp);
  return NULL;
}

/* Destroys a SORT CASES program. */
void
destroy_sort_cases_pgm (struct sort_cases_pgm *scp) 
{
  if (scp != NULL) 
    {
      assert (scp->ref_cnt > 0);
      if (--scp->ref_cnt > 0)
        return;

      free (scp->vars);
      free (scp->dirs);
      destroy_internal_sort (scp->isrt);
      destroy_external_sort (scp->xsrt);
      free (scp);
    }
}

/* Sorts the active file based on the key variables specified in
   global variables vars and var_cnt.  The output is either to the
   active file, if SEPARATE is zero, or to a separate file, if
   SEPARATE is nonzero.  In the latter case the output cases can be
   read with a call to read_sort_output().  (In the former case the
   output cases should be dealt with through the usual vfm interface.)

   The caller is responsible for freeing vars[]. */
int
sort_cases (struct sort_cases_pgm *scp, int separate)
{
  scp->case_size
    = sizeof (union value) * dict_get_compacted_value_cnt (default_dict);

  /* Not sure this is necessary but it's good to be safe. */
  if (separate && case_source_is_class (vfm_source, &sort_source_class))
    procedure (NULL, NULL);
  
  /* SORT CASES cancels PROCESS IF. */
  expr_free (process_if_expr);
  process_if_expr = NULL;

  /* Try an internal sort first. */
  scp->isrt = do_internal_sort (scp, separate);
  if (scp->isrt != NULL) 
    return 1; 

  /* Fall back to an external sort. */
  if (vfm_source != NULL
      && case_source_is_class (vfm_source, &storage_source_class))
    storage_source_to_disk (vfm_source);
  scp->xsrt = do_external_sort (scp, separate);
  if (scp->xsrt != NULL) 
    return 1;

  destroy_sort_cases_pgm (scp);
  return 0;
}

/* Results of an internal sort. */
struct internal_sort 
  {
    struct case_list **results;
  };

/* If the data is in memory, do an internal sort.  Return
   success. */
static struct internal_sort *
do_internal_sort (struct sort_cases_pgm *scp, int separate)
{
  struct internal_sort *isrt;

  isrt = xmalloc (sizeof *isrt);
  isrt->results = NULL;

  if (case_source_is_class (vfm_source, &storage_source_class)
      && !storage_source_on_disk (vfm_source))
    {
      struct case_list *case_list;
      struct case_list **case_array;
      int case_cnt;
      int i;

      case_cnt = vfm_source->class->count (vfm_source);
      if (case_cnt <= 0)
        return isrt;

      if (case_cnt > get_max_workspace() / sizeof *case_array)
        goto error;

      case_list = storage_source_get_cases (vfm_source);
      case_array = malloc (sizeof *case_array * (case_cnt + 1));
      if (case_array == NULL)
        goto error;

      for (i = 0; case_list != NULL; i++) 
        {
          case_array[i] = case_list;
          case_list = case_list->next;
        }
      assert (i == case_cnt);
      case_array[case_cnt] = NULL;

      sort (case_array, case_cnt, sizeof *case_array,
            compare_case_lists, scp);

      if (!separate) 
        {
          storage_source_set_cases (vfm_source, case_array[0]);
          for (i = 1; i <= case_cnt; i++)
            case_array[i - 1]->next = case_array[i]; 
          free (case_array);
        }
      else 
        isrt->results = case_array;
          	      
      return isrt;
    }

 error:
  free (isrt);
  return NULL;
}

/* Destroys an internal sort result. */
static void
destroy_internal_sort (struct internal_sort *isrt) 
{
  if (isrt != NULL) 
    {
      free (isrt->results);
      free (isrt);
    }
}

/* Compares the VAR_CNT variables in VARS[] between the
   `case_list's at A and B, and returns a strcmp()-type
   result. */
static int
compare_case_lists (const void *a_, const void *b_, void *scp_)
{
  struct sort_cases_pgm *scp = scp_;
  struct case_list *const *pa = a_;
  struct case_list *const *pb = b_;
  struct case_list *a = *pa;
  struct case_list *b = *pb;

  return compare_record (a->c.data, b->c.data, scp, NULL);
}

/* External sort. */

/* Maximum order of merge.  If you increase this, then you should
   use a heap for comparing cases during merge.  */
#define MAX_MERGE_ORDER		7

/* Minimum total number of records for buffers. */
#define MIN_BUFFER_TOTAL_SIZE_RECS	64

/* Minimum single input buffer size, in bytes and records. */
#define MIN_BUFFER_SIZE_BYTES	4096
#define MIN_BUFFER_SIZE_RECS	16

#if MIN_BUFFER_SIZE_RECS * 2 + 16 > MIN_BUFFER_TOTAL_SIZE_RECS
#error MIN_BUFFER_SIZE_RECS and MIN_BUFFER_TOTAL_SIZE_RECS do not make sense.
#endif

/* An initial run and its length. */
struct initial_run 
  {
    int file_idx;                     /* File index. */
    size_t case_cnt;                  /* Number of cases. */
  };

/* Sorts initial runs A and B in decending order by length. */
static int
compare_initial_runs (const void *a_, const void *b_, void *aux UNUSED) 
{
  const struct initial_run *a = a_;
  const struct initial_run *b = b_;
  
  return a->case_cnt > b->case_cnt ? -1 : a->case_cnt <b->case_cnt;
}

/* Results of an external sort. */
struct external_sort 
  {
    struct sort_cases_pgm *scp;       /* SORT CASES info. */
    struct initial_run *initial_runs; /* Array of initial runs. */
    size_t run_cnt, run_cap;          /* Number of runs, allocated capacity. */
    char *temp_dir;                   /* Temporary file directory name. */
    char *temp_name;                  /* Name of a temporary file. */
    int next_file_idx;                /* Lowest unused file index. */
  };

/* Prototypes for helper functions. */
static void sort_sink_write (struct case_sink *, const struct ccase *);
static int write_initial_runs (struct external_sort *, int separate);
static int init_external_sort (struct external_sort *);
static int merge (struct external_sort *);
static void rmdir_temp_dir (struct external_sort *);
static void remove_temp_file (struct external_sort *xsrt, int file_idx);

/* Performs an external sort of the active file according to the
   specification in SCP.  Forms initial runs using a heap as a
   reservoir.  Determines the optimum merge pattern via Huffman's
   method (see Knuth vol. 3, 2nd edition, p. 365-366), and merges
   according to that pattern. */
static struct external_sort *
do_external_sort (struct sort_cases_pgm *scp, int separate)
{
  struct external_sort *xsrt;
  int success = 0;

  xsrt = xmalloc (sizeof *xsrt);
  xsrt->scp = scp;
  if (!init_external_sort (xsrt))
    goto done;
  if (!write_initial_runs (xsrt, separate))
    goto done;
  if (!merge (xsrt))
    goto done;

  success = 1;

 done:
  if (success)
    {
      /* Don't destroy anything because we'll need it for reading
         the output. */
      return xsrt;
    }
  else
    {
      destroy_external_sort (xsrt);
      return NULL;
    }
}

/* Destroys XSRT. */
static void
destroy_external_sort (struct external_sort *xsrt) 
{
  if (xsrt != NULL) 
    {
      int i;
      
      for (i = 0; i < xsrt->run_cnt; i++)
        remove_temp_file (xsrt, xsrt->initial_runs[i].file_idx);
      rmdir_temp_dir (xsrt);
      free (xsrt->temp_dir);
      free (xsrt->temp_name);
      free (xsrt->initial_runs);
      free (xsrt);
    }
}

#ifdef HAVE_MKDTEMP
/* Creates and returns the name of a temporary directory. */
static char *
make_temp_dir (void) 
{
  const char *parent_dir;
  char *temp_dir;

  if (getenv ("TMPDIR") != NULL)
    parent_dir = getenv ("TMPDIR");
  else
    parent_dir = P_tmpdir;

  temp_dir = xmalloc (strlen (parent_dir) + 32);
  sprintf (temp_dir, "%s%cpsppXXXXXX", parent_dir, DIR_SEPARATOR);
  if (mkdtemp (temp_dir) == NULL) 
    {
      msg (SE, _("%s: Creating temporary directory: %s."),
           temp_dir, strerror (errno));
      free (temp_dir);
      return NULL;
    }
  else
    return temp_dir;
}
#else /* !HAVE_MKDTEMP */
/* Creates directory DIR. */
static int
do_mkdir (const char *dir) 
{
#ifndef __MSDOS__
  return mkdir (dir, S_IRWXU);
#else
  return mkdir (dir);
#endif
}

/* Creates and returns the name of a temporary directory. */
static char *
make_temp_dir (void) 
{
  int i;
  
  for (i = 0; i < 100; i++)
    {
      char temp_dir[L_tmpnam + 1];
      if (tmpnam (temp_dir) == NULL) 
        {
          msg (SE, _("Generating temporary directory name failed: %s."),
               strerror (errno));
          return NULL; 
        }
      else if (do_mkdir (temp_dir) == 0)
        return xstrdup (temp_dir);
    }
  
  msg (SE, _("Creating temporary directory failed: %s."), strerror (errno));
  return NULL;
}
#endif /* !HAVE_MKDTEMP */

/* Sets up to open temporary files. */
static int
init_external_sort (struct external_sort *xsrt)
{
  /* Zero. */
  xsrt->temp_dir = NULL;
  xsrt->next_file_idx = 0;

  /* Huffman queue. */
  xsrt->run_cap = 512;
  xsrt->run_cnt = 0;
  xsrt->initial_runs = xmalloc (sizeof *xsrt->initial_runs * xsrt->run_cap);

  /* Temporary directory. */
  xsrt->temp_dir = make_temp_dir ();
  xsrt->temp_name = NULL;
  if (xsrt->temp_dir == NULL)
    return 0;
  xsrt->temp_name = xmalloc (strlen (xsrt->temp_dir) + 64);

  return 1;
}

/* Returns nonzero if we should return an error even though the
   operation succeeded.  Useful for testing. */
static int
simulate_error (void) 
{
  static int op_err_cnt = -1;
  static int op_cnt;
  
  if (op_err_cnt == -1 || op_cnt++ < op_err_cnt)
    return 0;
  else
    {
      errno = 0;
      return 1;
    }
}

/* Removes the directory created for temporary files, if one
   exists. */
static void
rmdir_temp_dir (struct external_sort *xsrt)
{
  if (xsrt->temp_dir != NULL && rmdir (xsrt->temp_dir) == -1) 
    {
      msg (SE, _("%s: Error removing directory for temporary files: %s."),
           xsrt->temp_dir, strerror (errno));
      xsrt->temp_dir = NULL; 
    }
}

/* Returns the name of temporary file number FILE_IDX for XSRT.
   The name is written into a static buffer, so be careful.  */
static char *
get_temp_file_name (struct external_sort *xsrt, int file_idx)
{
  assert (xsrt->temp_dir != NULL);
  sprintf (xsrt->temp_name, "%s%c%04d",
           xsrt->temp_dir, DIR_SEPARATOR, file_idx);
  return xsrt->temp_name;
}

/* Opens temporary file numbered FILE_IDX for XSRT with mode MODE
   and returns the FILE *. */
static FILE *
open_temp_file (struct external_sort *xsrt, int file_idx, const char *mode)
{
  char *temp_file;
  FILE *file;

  temp_file = get_temp_file_name (xsrt, file_idx);

  file = fopen (temp_file, mode);
  if (simulate_error () || file == NULL) 
    msg (SE, _("%s: Error opening temporary file for %s: %s."),
         temp_file, mode[0] == 'r' ? "reading" : "writing",
         strerror (errno));

  return file;
}

/* Closes FILE, which is the temporary file numbered FILE_IDX
   under XSRT.  Returns nonzero only if successful.  */
static int
close_temp_file (struct external_sort *xsrt, int file_idx, FILE *file)
{
  if (file != NULL) 
    {
      char *temp_file = get_temp_file_name (xsrt, file_idx);
      if (simulate_error () || fclose (file) == EOF) 
        {
          msg (SE, _("%s: Error closing temporary file: %s."),
               temp_file, strerror (errno));
          return 0;
        }
    }
  return 1;
}

/* Delete temporary file numbered FILE_IDX for XSRT. */
static void
remove_temp_file (struct external_sort *xsrt, int file_idx) 
{
  if (file_idx != -1)
    {
      char *temp_file = get_temp_file_name (xsrt, file_idx);
      if (simulate_error () || remove (temp_file) != 0)
        msg (SE, _("%s: Error removing temporary file: %s."),
             temp_file, strerror (errno));
    }
}

/* Writes SIZE bytes from buffer DATA into FILE, which is
   temporary file numbered FILE_IDX from XSRT. */
static int
write_temp_file (struct external_sort *xsrt, int file_idx,
                 FILE *file, const void *data, size_t size) 
{
  if (!simulate_error () && fwrite (data, size, 1, file) == 1)
    return 1;
  else
    {
      char *temp_file = get_temp_file_name (xsrt, file_idx);
      msg (SE, _("%s: Error writing temporary file: %s."),
           temp_file, strerror (errno));
      return 0;
    }
}

/* Reads SIZE bytes into buffer DATA into FILE, which is
   temporary file numbered FILE_IDX from XSRT. */
static int
read_temp_file (struct external_sort *xsrt, int file_idx,
                FILE *file, void *data, size_t size) 
{
  if (!simulate_error () && fread (data, size, 1, file) == 1)
    return 1;
  else 
    {
      char *temp_file = get_temp_file_name (xsrt, file_idx);
      if (ferror (file))
        msg (SE, _("%s: Error reading temporary file: %s."),
             temp_file, strerror (errno));
      else
        msg (SE, _("%s: Unexpected end of temporary file."),
             temp_file);
      return 0;
    }
}

/* Replacement selection. */

/* Pairs a record with a run number. */
struct record_run
  {
    int run;                    /* Run number of case. */
    struct case_list *record;   /* Case data. */
  };

/* Represents a set of initial runs during an external sort. */
struct initial_run_state 
  {
    struct external_sort *xsrt;

    int *idx_to_fv;             /* Translation table copied from sink. */

    /* Reservoir. */
    struct record_run *records; /* Records arranged as a heap. */
    size_t record_cnt;          /* Current number of records. */
    size_t record_cap;          /* Capacity for records. */
    struct case_list *free_list;/* Cases not in heap. */
    
    /* Run currently being output. */
    int file_idx;               /* Temporary file number. */
    size_t case_cnt;            /* Number of cases so far. */
    FILE *output_file;          /* Output file. */
    struct case_list *last_output;/* Record last output. */

    int okay;                   /* Zero if an error has been encountered. */
  };

static const struct case_sink_class sort_sink_class;

static void destroy_initial_run_state (struct initial_run_state *irs);
static int allocate_cases (struct initial_run_state *);
static struct case_list *grab_case (struct initial_run_state *);
static void release_case (struct initial_run_state *, struct case_list *);
static void output_record (struct initial_run_state *irs);
static void start_run (struct initial_run_state *irs);
static void end_run (struct initial_run_state *irs);
static int compare_record_run (const struct record_run *,
                               const struct record_run *,
                               struct initial_run_state *);
static int compare_record_run_minheap (const void *, const void *, void *);

/* Writes initial runs for XSRT, sending them to a separate file
   if SEPARATE is nonzero. */
static int
write_initial_runs (struct external_sort *xsrt, int separate)
{
  struct initial_run_state *irs;
  int success = 0;

  /* Allocate memory for cases. */
  irs = xmalloc (sizeof *irs);
  irs->xsrt = xsrt;
  irs->records = NULL;
  irs->record_cnt = irs->record_cap = 0;
  irs->free_list = NULL;
  irs->output_file = NULL;
  irs->last_output = NULL;
  irs->file_idx = 0;
  irs->case_cnt = 0;
  irs->okay = 1;
  if (!allocate_cases (irs)) 
    goto done;

  /* Create case sink. */
  if (!separate)
    {
      if (vfm_sink != NULL && vfm_sink->class->destroy != NULL)
	vfm_sink->class->destroy (vfm_sink);
      vfm_sink = create_case_sink (&sort_sink_class, default_dict, irs);
      xsrt->scp->ref_cnt++;
    }

  /* Create initial runs. */
  start_run (irs);
  procedure (NULL, NULL);
  irs->idx_to_fv = NULL;
  while (irs->record_cnt > 0 && irs->okay)
    output_record (irs);
  end_run (irs);

  success = irs->okay;

 done:
  destroy_initial_run_state (irs);

  return success;
}

/* Add a single case to an initial run. */
static void
sort_sink_write (struct case_sink *sink, const struct ccase *c)
{
  struct initial_run_state *irs = sink->aux;
  struct record_run *new_record_run;

  if (!irs->okay)
    return;

  irs->idx_to_fv = sink->idx_to_fv;

  /* Compose record_run for this run and add to heap. */
  assert (irs->record_cnt < irs->record_cap);
  new_record_run = irs->records + irs->record_cnt++;
  new_record_run->record = grab_case (irs);
  memcpy (new_record_run->record->c.data, c->data, irs->xsrt->scp->case_size);
  new_record_run->run = irs->file_idx;
  if (irs->last_output != NULL
      && compare_record (c->data, irs->last_output->c.data,
                         irs->xsrt->scp, sink->idx_to_fv) < 0)
    new_record_run->run = irs->xsrt->next_file_idx;
  push_heap (irs->records, irs->record_cnt, sizeof *irs->records,
             compare_record_run_minheap, irs);

  /* Output a record if the reservoir is full. */
  if (irs->record_cnt == irs->record_cap && irs->okay)
    output_record (irs);
}

/* Destroys the initial run state represented by IRS. */
static void
destroy_initial_run_state (struct initial_run_state *irs) 
{
  struct case_list *iter, *next;
  int i;

  if (irs == NULL)
    return;

  /* Release cases to free list. */
  for (i = 0; i < irs->record_cnt; i++)
    release_case (irs, irs->records[i].record);
  if (irs->last_output != NULL)
    release_case (irs, irs->last_output);

  /* Free cases in free list. */
  for (iter = irs->free_list; iter != NULL; iter = next) 
    {
      next = iter->next;
      free (iter);
    }

  free (irs->records);
  if (irs->output_file != NULL)
    close_temp_file (irs->xsrt, irs->file_idx, irs->output_file);

  free (irs);
}

/* Allocates room for lots of cases as a buffer. */
static int
allocate_cases (struct initial_run_state *irs)
{
  int approx_case_cost; /* Approximate memory cost of one case in bytes. */
  int max_cases;        /* Maximum number of cases to allocate. */
  int i;

  /* Allocate as many cases as we can within the workspace
     limit. */
  approx_case_cost = (sizeof *irs->records
                      + sizeof *irs->free_list
                      + irs->xsrt->scp->case_size
                      + 4 * sizeof (void *));
  max_cases = get_max_workspace() / approx_case_cost;
  irs->records = malloc (sizeof *irs->records * max_cases);
  for (i = 0; i < max_cases; i++)
    {
      struct case_list *c;
      c = malloc (sizeof *c
                  + irs->xsrt->scp->case_size
                  - sizeof (union value));
      if (c == NULL) 
        {
          max_cases = i;
          break;
        }
      release_case (irs, c);
    }

  /* irs->records gets all but one of the allocated cases.
     The extra is used for last_output. */
  irs->record_cap = max_cases - 1;

  /* Fail if we didn't allocate an acceptable number of cases. */
  if (irs->records == NULL || max_cases < MIN_BUFFER_TOTAL_SIZE_RECS)
    {
      msg (SE, _("Out of memory.  Could not allocate room for minimum of %d "
		 "cases of %d bytes each.  (PSPP workspace is currently "
		 "restricted to a maximum of %d KB.)"),
	   MIN_BUFFER_TOTAL_SIZE_RECS, approx_case_cost, get_max_workspace() / 1024);
      return 0;
    }
  return 1;
}

/* Compares the VAR_CNT variables in VARS[] between the `value's at
   A and B, and returns a strcmp()-type result. */
static int
compare_record (const union value *a, const union value *b,
                const struct sort_cases_pgm *scp,
                int *idx_to_fv)
{
  int i;

  assert (a != NULL);
  assert (b != NULL);
  
  for (i = 0; i < scp->var_cnt; i++)
    {
      struct variable *v = scp->vars[i];
      int fv;
      int result;

      if (idx_to_fv != NULL)
        fv = idx_to_fv[v->index];
      else
        fv = v->fv;
      
      if (v->type == NUMERIC)
        {
          double af = a[fv].f;
          double bf = b[fv].f;
          
          result = af < bf ? -1 : af > bf;
        }
      else
        result = memcmp (a[fv].s, b[fv].s, v->width);

      if (result != 0) 
        {
          if (scp->dirs[i] == SRT_DESCEND)
            result = -result;
          return result;
        }
    }

  return 0;
}

/* Compares record-run tuples A and B on run number first, then
   on the current record according to SCP. */
static int
compare_record_run (const struct record_run *a,
                    const struct record_run *b,
                    struct initial_run_state *irs)
{
  if (a->run != b->run)
    return a->run > b->run ? 1 : -1;
  else
    return compare_record (a->record->c.data, b->record->c.data,
                           irs->xsrt->scp, irs->idx_to_fv);
}

/* Compares record-run tuples A and B on run number first, then
   on the current record according to SCP, but in descending
   order. */
static int
compare_record_run_minheap (const void *a, const void *b, void *irs) 
{
  return -compare_record_run (a, b, irs);
}

/* Begins a new initial run, specifically its output file. */
static void
start_run (struct initial_run_state *irs)
{
  irs->file_idx = irs->xsrt->next_file_idx++;
  irs->case_cnt = 0;
  irs->output_file = open_temp_file (irs->xsrt, irs->file_idx, "wb");
  if (irs->output_file == NULL) 
    irs->okay = 0;
  if (irs->last_output != NULL) 
    {
      release_case (irs, irs->last_output);
      irs->last_output = NULL; 
    }
}

/* Ends the current initial run.  */
static void
end_run (struct initial_run_state *irs)
{
  struct external_sort *xsrt = irs->xsrt;
  
  /* Record initial run. */
  if (xsrt->run_cnt >= xsrt->run_cap) 
    {
      xsrt->run_cap *= 2;
      xsrt->initial_runs
        = xrealloc (xsrt->initial_runs,
                    sizeof *xsrt->initial_runs * xsrt->run_cap);
    }
  xsrt->initial_runs[xsrt->run_cnt].file_idx = irs->file_idx;
  xsrt->initial_runs[xsrt->run_cnt].case_cnt = irs->case_cnt;
  xsrt->run_cnt++;

  /* Close file handle. */
  if (irs->output_file != NULL
      && !close_temp_file (irs->xsrt, irs->file_idx, irs->output_file)) 
    irs->okay = 0;
  irs->output_file = NULL;
}

/* Writes a record to the current initial run. */
static void
output_record (struct initial_run_state *irs)
{
  struct record_run *record_run;
  
  /* Extract minimum case from heap. */
  assert (irs->record_cnt > 0);
  pop_heap (irs->records, irs->record_cnt--, sizeof *irs->records,
            compare_record_run_minheap, irs);
  record_run = irs->records + irs->record_cnt;

  /* Bail if an error has occurred. */
  if (!irs->okay)
    return;

  /* Start new run if necessary. */
  assert (record_run->run == irs->file_idx
          || record_run->run == irs->xsrt->next_file_idx);
  if (record_run->run != irs->file_idx)
    {
      end_run (irs);
      start_run (irs);
    }
  assert (record_run->run == irs->file_idx);
  irs->case_cnt++;

  /* Write to disk. */
  if (irs->output_file != NULL
      && !write_temp_file (irs->xsrt, irs->file_idx, irs->output_file,
                           &record_run->record->c, irs->xsrt->scp->case_size))
    irs->okay = 0;

  /* This record becomes last_output. */
  if (irs->last_output != NULL)
    release_case (irs, irs->last_output);
  irs->last_output = record_run->record;
}

/* Gets a case from the free list in IRS.  It is an error to call
   this function if the free list is empty. */
static struct case_list *
grab_case (struct initial_run_state *irs)
{
  struct case_list *c;
  
  assert (irs != NULL);
  assert (irs->free_list != NULL);

  c = irs->free_list;
  irs->free_list = c->next;
  return c;
}

/* Returns C to the free list in IRS. */
static void 
release_case (struct initial_run_state *irs, struct case_list *c) 
{
  assert (irs != NULL);
  assert (c != NULL);

  c->next = irs->free_list;
  irs->free_list = c;
}

/* Merging. */

/* State of merging initial runs. */
struct merge_state 
  {
    struct external_sort *xsrt; /* External sort state. */
    struct ccase **cases;       /* Buffers. */
    size_t case_cnt;            /* Number of buffers. */
  };

struct run;
static int merge_once (struct merge_state *,
                       const struct initial_run[], size_t,
                       struct initial_run *);
static int fill_run_buffer (struct merge_state *, struct run *);
static int mod (int, int);

/* Performs a series of P-way merges of initial runs
   method. */
static int
merge (struct external_sort *xsrt)
{
  struct merge_state mrg;       /* State of merge. */
  size_t approx_case_cost;      /* Approximate memory cost of one case. */
  int max_order;                /* Maximum order of merge. */
  size_t dummy_run_cnt;         /* Number of dummy runs to insert. */
  int success = 0;
  int i;

  mrg.xsrt = xsrt;

  /* Allocate as many cases as possible into cases. */
  approx_case_cost = (sizeof *mrg.cases
                      + xsrt->scp->case_size + 4 * sizeof (void *));
  mrg.case_cnt = get_max_workspace() / approx_case_cost;
  mrg.cases = malloc (sizeof *mrg.cases * mrg.case_cnt);
  if (mrg.cases == NULL)
    goto done;
  for (i = 0; i < mrg.case_cnt; i++) 
    {
      mrg.cases[i] = malloc (xsrt->scp->case_size);
      if (mrg.cases[i] == NULL) 
        {
          mrg.case_cnt = i;
          break;
        }
    }
  if (mrg.case_cnt < MIN_BUFFER_TOTAL_SIZE_RECS)
    {
      msg (SE, _("Out of memory.  Could not allocate room for minimum of %d "
		 "cases of %d bytes each.  (PSPP workspace is currently "
		 "restricted to a maximum of %d KB.)"),
	   MIN_BUFFER_TOTAL_SIZE_RECS, approx_case_cost, get_max_workspace() / 1024);
      return 0;
    }

  /* Determine maximum order of merge. */
  max_order = MAX_MERGE_ORDER;
  if (mrg.case_cnt / max_order < MIN_BUFFER_SIZE_RECS)
    max_order = mrg.case_cnt / MIN_BUFFER_SIZE_RECS;
  else if (mrg.case_cnt / max_order * xsrt->scp->case_size
           < MIN_BUFFER_SIZE_BYTES)
    max_order = mrg.case_cnt / (MIN_BUFFER_SIZE_BYTES / xsrt->scp->case_size);
  if (max_order < 2)
    max_order = 2;
  if (max_order > xsrt->run_cnt)
    max_order = xsrt->run_cnt;

  /* Repeatedly merge the P shortest existing runs until only one run
     is left. */
  make_heap (xsrt->initial_runs, xsrt->run_cnt, sizeof *xsrt->initial_runs,
             compare_initial_runs, NULL);
  dummy_run_cnt = mod (1 - (int) xsrt->run_cnt, max_order - 1);
  assert (max_order == 1
          || (xsrt->run_cnt + dummy_run_cnt) % (max_order - 1) == 1);
  while (xsrt->run_cnt > 1)
    {
      struct initial_run output_run;
      int order;
      int i;

      /* Choose order of merge (max_order after first merge). */
      order = max_order - dummy_run_cnt;
      dummy_run_cnt = 0;

      /* Choose runs to merge. */
      assert (xsrt->run_cnt >= order);
      for (i = 0; i < order; i++) 
        pop_heap (xsrt->initial_runs, xsrt->run_cnt--,
                  sizeof *xsrt->initial_runs,
                  compare_initial_runs, NULL); 
          
      /* Merge runs. */
      if (!merge_once (&mrg, xsrt->initial_runs + xsrt->run_cnt, order,
                       &output_run))
        goto done;

      /* Add output run to heap. */
      xsrt->initial_runs[xsrt->run_cnt++] = output_run;
      push_heap (xsrt->initial_runs, xsrt->run_cnt, sizeof *xsrt->initial_runs,
                 compare_initial_runs, NULL);
    }

  /* Exactly one run is left, which contains the entire sorted
     file.  We could use it to find a total case count. */
  assert (xsrt->run_cnt == 1);

  success = 1;

 done:
  for (i = 0; i < mrg.case_cnt; i++)
    free (mrg.cases[i]);
  free (mrg.cases);

  return success;
}

/* Modulo function as defined by Knuth. */
static int
mod (int x, int y)
{
  if (y == 0)
    return x;
  else if (x == 0)
    return 0;
  else if (x > 0 && y > 0)
    return x % y;
  else if (x < 0 && y > 0)
    return y - (-x) % y;

  assert (0);
}

/* A run of data for use in merging. */
struct run 
  {
    FILE *file;                 /* File that contains run. */
    int file_idx;               /* Index of file that contains run. */
    struct ccase **buffer;      /* Case buffer. */
    struct ccase **buffer_head; /* First unconsumed case in buffer. */
    struct ccase **buffer_tail; /* One past last unconsumed case in buffer. */
    size_t buffer_cap;          /* Number of cases buffer can hold. */
    size_t unread_case_cnt;     /* Number of cases not yet read. */
  };

/* Merges the RUN_CNT initial runs specified in INPUT_RUNS into a
   new run.  Returns nonzero only if successful.  Adds an entry
   to MRG->xsrt->runs for the output file if and only if the
   output file is actually created.  Always deletes all the input
   files. */
static int
merge_once (struct merge_state *mrg,
            const struct initial_run input_runs[],
            size_t run_cnt,
            struct initial_run *output_run)
{
  struct run runs[MAX_MERGE_ORDER];
  FILE *output_file = NULL;
  int success = 0;
  int i;

  /* Initialize runs[]. */
  for (i = 0; i < run_cnt; i++) 
    {
      runs[i].file = NULL;
      runs[i].file_idx = input_runs[i].file_idx;
      runs[i].buffer = mrg->cases + mrg->case_cnt / run_cnt * i;
      runs[i].buffer_head = runs[i].buffer;
      runs[i].buffer_tail = runs[i].buffer;
      runs[i].buffer_cap = mrg->case_cnt / run_cnt;
      runs[i].unread_case_cnt = input_runs[i].case_cnt;
    }

  /* Open input files. */
  for (i = 0; i < run_cnt; i++) 
    {
      runs[i].file = open_temp_file (mrg->xsrt, runs[i].file_idx, "rb");
      if (runs[i].file == NULL)
        goto error;
    }
  
  /* Create output file and count cases to be output. */
  output_run->file_idx = mrg->xsrt->next_file_idx++;
  output_run->case_cnt = 0;
  for (i = 0; i < run_cnt; i++)
    output_run->case_cnt += input_runs[i].case_cnt;
  output_file = open_temp_file (mrg->xsrt, output_run->file_idx, "wb");
  if (output_file == NULL) 
    goto error;

  /* Prime buffers. */
  for (i = 0; i < run_cnt; i++)
    if (!fill_run_buffer (mrg, runs + i))
      goto error;

  /* Merge. */
  while (run_cnt > 0) 
    {
      struct run *min_run;

      /* Find minimum. */
      min_run = runs;
      for (i = 1; i < run_cnt; i++)
	if (compare_record ((*runs[i].buffer_head)->data,
                            (*min_run->buffer_head)->data,
                            mrg->xsrt->scp, NULL) < 0)
          min_run = runs + i;

      /* Write minimum to output file. */
      if (!write_temp_file (mrg->xsrt, min_run->file_idx, output_file,
                            (*min_run->buffer_head)->data,
                            mrg->xsrt->scp->case_size))
        goto error;

      /* Remove case from buffer. */
      if (++min_run->buffer_head >= min_run->buffer_tail)
        {
          /* Buffer is empty.  Fill from file. */
          if (!fill_run_buffer (mrg, min_run))
            goto error;

          /* If buffer is still empty, delete its run. */
          if (min_run->buffer_head >= min_run->buffer_tail)
            {
              close_temp_file (mrg->xsrt, min_run->file_idx, min_run->file);
              remove_temp_file (mrg->xsrt, min_run->file_idx);
              *min_run = runs[--run_cnt];

              /* We could donate the now-unused buffer space to
                 other runs. */
            }
        } 
    }

  /* Close output file.  */
  close_temp_file (mrg->xsrt, output_run->file_idx, output_file);

  return 1;

 error:
  /* Close and remove output file.  */
  if (output_file != NULL) 
    {
      close_temp_file (mrg->xsrt, output_run->file_idx, output_file);
      remove_temp_file (mrg->xsrt, output_run->file_idx);
    }
  
  /* Close and remove any remaining input runs. */
  for (i = 0; i < run_cnt; i++) 
    {
      close_temp_file (mrg->xsrt, runs[i].file_idx, runs[i].file);
      remove_temp_file (mrg->xsrt, runs[i].file_idx);
    }

  return success;
}

/* Reads as many cases as possible into RUN's buffer.
   Reads nonzero unless a disk error occurs. */
static int
fill_run_buffer (struct merge_state *mrg, struct run *run) 
{
  run->buffer_head = run->buffer_tail = run->buffer;
  while (run->unread_case_cnt > 0
         && run->buffer_tail < run->buffer + run->buffer_cap)
    {
      if (!read_temp_file (mrg->xsrt, run->file_idx, run->file,
                           (*run->buffer_tail)->data,
                           mrg->xsrt->scp->case_size))
        return 0;

      run->unread_case_cnt--;
      run->buffer_tail++;
    }

  return 1;
}

static struct case_source *
sort_sink_make_source (struct case_sink *sink) 
{
  struct initial_run_state *irs = sink->aux;

  return create_case_source (&sort_source_class, default_dict,
                             irs->xsrt->scp);
}

static const struct case_sink_class sort_sink_class = 
  {
    "SORT CASES",
    NULL,
    sort_sink_write,
    NULL,
    sort_sink_make_source,
  };

struct sort_source_aux 
  {
    struct sort_cases_pgm *scp;
    struct ccase *dst;
    write_case_func *write_case;
    write_case_data wc_data;
  };

/* Passes C to the write_case function. */
static int
sort_source_read_helper (const struct ccase *src, void *aux_) 
{
  struct sort_source_aux *aux = aux_;

  memcpy (aux->dst, src, aux->scp->case_size);
  return aux->write_case (aux->wc_data);
}

/* Reads all the records from the source stream and passes them
   to write_case(). */
static void
sort_source_read (struct case_source *source,
                  struct ccase *c,
                  write_case_func *write_case, write_case_data wc_data)
{
  struct sort_cases_pgm *scp = source->aux;
  struct sort_source_aux aux;

  aux.scp = scp;
  aux.dst = c;
  aux.write_case = write_case;
  aux.wc_data = wc_data;
  
  read_sort_output (scp, sort_source_read_helper, &aux);
}

static void read_internal_sort_output (struct internal_sort *isrt,
                                       read_sort_output_func *, void *aux);
static void read_external_sort_output (struct external_sort *xsrt,
                                       read_sort_output_func *, void *aux);

/* Reads all the records from the output stream and passes them to the
   function provided, which must have an interface identical to
   write_case(). */
void
read_sort_output (struct sort_cases_pgm *scp,
                  read_sort_output_func *output_func, void *aux)
{
  assert ((scp->isrt != NULL) + (scp->xsrt != NULL) <= 1);
  if (scp->isrt != NULL)
    read_internal_sort_output (scp->isrt, output_func, aux);
  else if (scp->xsrt != NULL)
    read_external_sort_output (scp->xsrt, output_func, aux);
  else 
    {
      /* No results.  Probably an external sort that failed. */
    }
}

static void
read_internal_sort_output (struct internal_sort *isrt,
                           read_sort_output_func *output_func,
                           void *aux)
{
  struct case_list **p;

  for (p = isrt->results; *p; p++)
    if (!output_func (&(*p)->c, aux))
      break;
  free (isrt->results);
}

static void
read_external_sort_output (struct external_sort *xsrt,
                           read_sort_output_func *output_func, void *aux)
{
  FILE *file;
  int file_idx;
  size_t i;
  struct ccase *c;

  assert (xsrt->run_cnt == 1);
  file_idx = xsrt->initial_runs[0].file_idx;

  file = open_temp_file (xsrt, file_idx, "rb");
  if (file == NULL)
    {
      err_failure ();
      return;
    }

  c = xmalloc (xsrt->scp->case_size);
  for (i = 0; i < xsrt->initial_runs[0].case_cnt; i++)
    {
      if (!read_temp_file (xsrt, file_idx, file, c, xsrt->scp->case_size))
	{
          err_failure ();
          break;
        }

      if (!output_func (c, aux))
        break;
    }
  free (c);
}

static void
sort_source_destroy (struct case_source *source) 
{
  struct sort_cases_pgm *scp = source->aux;
  
  destroy_sort_cases_pgm (scp);
}

const struct case_source_class sort_source_class =
  {
    "SORT CASES",
    NULL, /* FIXME */
    sort_source_read,
    sort_source_destroy,
  };
