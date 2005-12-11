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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>
#include "sort.h"
#include "error.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "algorithm.h"
#include "alloc.h"
#include <stdbool.h>
#include "case.h"
#include "casefile.h"
#include "command.h"
#include "error.h"
#include "expressions/public.h"
#include "glob.h"
#include "lexer.h"
#include "misc.h"
#include "settings.h"
#include "sort-prs.h"
#include "str.h"
#include "var.h"
#include "vfm.h"
#include "vfmP.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

#include "debug-print.h"

/* These should only be changed for testing purposes. */
static int min_buffers = 64;
static int max_buffers = INT_MAX;
static bool allow_internal_sort = true;

static int compare_record (const struct ccase *, const struct ccase *,
                           const struct sort_criteria *);
static struct casefile *do_internal_sort (struct casereader *,
                                          const struct sort_criteria *);
static struct casefile *do_external_sort (struct casereader *,
                                          const struct sort_criteria *);

/* Performs the SORT CASES procedures. */
int
cmd_sort_cases (void)
{
  struct sort_criteria *criteria;
  bool success = false;

  lex_match (T_BY);

  criteria = sort_parse_criteria (default_dict, NULL, NULL, NULL, NULL);
  if (criteria == NULL)
    return CMD_FAILURE;

  if (get_testing_mode () && lex_match ('/')) 
    {
      if (!lex_force_match_id ("BUFFERS") || !lex_match ('=')
          || !lex_force_int ())
        goto done;

      min_buffers = max_buffers = lex_integer ();
      allow_internal_sort = false;
      if (max_buffers < 2) 
        {
          msg (SE, _("Buffer limit must be at least 2."));
          goto done;
        }

      lex_get ();
    }

  success = sort_active_file_in_place (criteria);

 done:
  min_buffers = 64;
  max_buffers = INT_MAX;
  allow_internal_sort = true;
  
  sort_destroy_criteria (criteria);
  return success ? lex_end_of_command () : CMD_FAILURE;
}

/* Gets ready to sort the active file, either in-place or to a
   separate casefile. */
static void
prepare_to_sort_active_file (void) 
{
  /* Cancel temporary transformations and PROCESS IF. */
  if (temporary != 0)
    cancel_temporary (); 
  expr_free (process_if_expr);
  process_if_expr = NULL;

  /* Make sure source cases are in a storage source. */
  procedure (NULL, NULL);
  assert (case_source_is_class (vfm_source, &storage_source_class));
}

/* Sorts the active file in-place according to CRITERIA.
   Returns nonzero if successful. */
int
sort_active_file_in_place (const struct sort_criteria *criteria) 
{
  struct casefile *src, *dst;
  
  prepare_to_sort_active_file ();

  src = storage_source_get_casefile (vfm_source);
  dst = sort_execute (casefile_get_destructive_reader (src), criteria);
  free_case_source (vfm_source);
  vfm_source = NULL;

  if (dst == NULL) 
    return 0;

  vfm_source = storage_source_create (dst);
  return 1;
}

/* Sorts the active file to a separate casefile.  If successful,
   returns the sorted casefile.  Returns a null pointer on
   failure. */
struct casefile *
sort_active_file_to_casefile (const struct sort_criteria *criteria) 
{
  struct casefile *src;
  
  prepare_to_sort_active_file ();

  src = storage_source_get_casefile (vfm_source);
  return sort_execute (casefile_get_reader (src), criteria);
}


/* Reads all the cases from READER, which is destroyed.  Sorts
   the cases according to CRITERIA.  Returns the sorted cases in
   a newly created casefile. */
struct casefile *
sort_execute (struct casereader *reader, const struct sort_criteria *criteria)
{
  struct casefile *output = do_internal_sort (reader, criteria);
  if (output == NULL)
    output = do_external_sort (reader, criteria);
  casereader_destroy (reader);
  return output;
}

/* A case and its index. */
struct indexed_case 
  {
    struct ccase c;     /* Case. */
    unsigned long idx;  /* Index to allow for stable sorting. */
  };

static int compare_indexed_cases (const void *, const void *, void *);

/* If the data is in memory, do an internal sort and return a new
   casefile for the data.  Otherwise, return a null pointer. */
static struct casefile *
do_internal_sort (struct casereader *reader,
                  const struct sort_criteria *criteria)
{
  const struct casefile *src;
  struct casefile *dst;
  unsigned long case_cnt;

  if (!allow_internal_sort)
    return NULL;

  src = casereader_get_casefile (reader);
  if (casefile_get_case_cnt (src) > 1 && !casefile_in_core (src))
    return NULL;
      
  case_cnt = casefile_get_case_cnt (src);
  dst = casefile_create (casefile_get_value_cnt (src));
  if (case_cnt != 0) 
    {
      struct indexed_case *cases = nmalloc (sizeof *cases, case_cnt);
      if (cases != NULL) 
        {
          unsigned long i;
          
          for (i = 0; i < case_cnt; i++)
            {
              casereader_read_xfer_assert (reader, &cases[i].c);
              cases[i].idx = i;
            }

          sort (cases, case_cnt, sizeof *cases, compare_indexed_cases,
                (void *) criteria);
      
          for (i = 0; i < case_cnt; i++)
            casefile_append_xfer (dst, &cases[i].c);

          free (cases);
        }
      else 
        {
          /* Failure. */
          casefile_destroy (dst);
          dst = NULL;
        }
    }

  return dst;
}

/* Compares the variables specified by CRITERIA between the cases
   at A and B, with a "last resort" comparison for stability, and
   returns a strcmp()-type result. */
static int
compare_indexed_cases (const void *a_, const void *b_, void *criteria_)
{
  struct sort_criteria *criteria = criteria_;
  const struct indexed_case *a = a_;
  const struct indexed_case *b = b_;
  int result = compare_record (&a->c, &b->c, criteria);
  if (result == 0)
    result = a->idx < b->idx ? -1 : a->idx > b->idx;
  return result;
}

/* External sort. */

/* Maximum order of merge (external sort only).  The maximum
   reasonable value is about 7.  Above that, it would be a good
   idea to use a heap in merge_once() to select the minimum. */
#define MAX_MERGE_ORDER 7

/* Results of an external sort. */
struct external_sort 
  {
    const struct sort_criteria *criteria; /* Sort criteria. */
    size_t value_cnt;                 /* Size of data in `union value's. */
    struct casefile **runs;           /* Array of initial runs. */
    size_t run_cnt, run_cap;          /* Number of runs, allocated capacity. */
  };

/* Prototypes for helper functions. */
static int write_runs (struct external_sort *, struct casereader *);
static struct casefile *merge (struct external_sort *);
static void destroy_external_sort (struct external_sort *);

/* Performs a stable external sort of the active file according
   to the specification in SCP.  Forms initial runs using a heap
   as a reservoir.  Merges the initial runs according to a
   pattern that assures stability. */
static struct casefile *
do_external_sort (struct casereader *reader,
                  const struct sort_criteria *criteria)
{
  struct external_sort *xsrt;

  casefile_to_disk (casereader_get_casefile (reader));

  xsrt = xmalloc (sizeof *xsrt);
  xsrt->criteria = criteria;
  xsrt->value_cnt = casefile_get_value_cnt (casereader_get_casefile (reader));
  xsrt->run_cap = 512;
  xsrt->run_cnt = 0;
  xsrt->runs = xnmalloc (xsrt->run_cap, sizeof *xsrt->runs);
  if (write_runs (xsrt, reader))
    {
      struct casefile *output = merge (xsrt);
      destroy_external_sort (xsrt);
      return output;
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
        casefile_destroy (xsrt->runs[i]);
      free (xsrt->runs);
      free (xsrt);
    }
}

/* Replacement selection. */

/* Pairs a record with a run number. */
struct record_run
  {
    int run;                    /* Run number of case. */
    struct ccase record;        /* Case data. */
    size_t idx;                 /* Case number (for stability). */
  };

/* Represents a set of initial runs during an external sort. */
struct initial_run_state 
  {
    struct external_sort *xsrt;

    /* Reservoir. */
    struct record_run *records; /* Records arranged as a heap. */
    size_t record_cnt;          /* Current number of records. */
    size_t record_cap;          /* Capacity for records. */
    
    /* Run currently being output. */
    int run;                    /* Run number. */
    size_t case_cnt;            /* Number of cases so far. */
    struct casefile *casefile;  /* Output file. */
    struct ccase last_output;   /* Record last output. */

    int okay;                   /* Zero if an error has been encountered. */
  };

static const struct case_sink_class sort_sink_class;

static void destroy_initial_run_state (struct initial_run_state *);
static void process_case (struct initial_run_state *, const struct ccase *,
                          size_t);
static int allocate_cases (struct initial_run_state *);
static void output_record (struct initial_run_state *);
static void start_run (struct initial_run_state *);
static void end_run (struct initial_run_state *);
static int compare_record_run (const struct record_run *,
                               const struct record_run *,
                               struct initial_run_state *);
static int compare_record_run_minheap (const void *, const void *, void *);

/* Reads cases from READER and composes initial runs in XSRT. */
static int
write_runs (struct external_sort *xsrt, struct casereader *reader)
{
  struct initial_run_state *irs;
  struct ccase c;
  size_t idx = 0;
  int success = 0;

  /* Allocate memory for cases. */
  irs = xmalloc (sizeof *irs);
  irs->xsrt = xsrt;
  irs->records = NULL;
  irs->record_cnt = irs->record_cap = 0;
  irs->run = 0;
  irs->case_cnt = 0;
  irs->casefile = NULL;
  case_nullify (&irs->last_output);
  irs->okay = 1;
  if (!allocate_cases (irs)) 
    goto done;

  /* Create initial runs. */
  start_run (irs);
  for (; irs->okay && casereader_read (reader, &c); case_destroy (&c))
    process_case (irs, &c, idx++);
  while (irs->okay && irs->record_cnt > 0)
    output_record (irs);
  end_run (irs);

  success = irs->okay;

 done:
  destroy_initial_run_state (irs);

  return success;
}

/* Add a single case to an initial run. */
static void
process_case (struct initial_run_state *irs, const struct ccase *c, size_t idx)
{
  struct record_run *rr;

  /* Compose record_run for this run and add to heap. */
  assert (irs->record_cnt < irs->record_cap - 1);
  rr = irs->records + irs->record_cnt++;
  case_copy (&rr->record, 0, c, 0, irs->xsrt->value_cnt);
  rr->run = irs->run;
  rr->idx = idx;
  if (!case_is_null (&irs->last_output)
      && compare_record (c, &irs->last_output, irs->xsrt->criteria) < 0)
    rr->run = irs->run + 1;
  push_heap (irs->records, irs->record_cnt, sizeof *irs->records,
             compare_record_run_minheap, irs);

  /* Output a record if the reservoir is full. */
  if (irs->record_cnt == irs->record_cap - 1 && irs->okay)
    output_record (irs);
}

/* Destroys the initial run state represented by IRS. */
static void
destroy_initial_run_state (struct initial_run_state *irs) 
{
  int i;

  if (irs == NULL)
    return;

  for (i = 0; i < irs->record_cap; i++)
    case_destroy (&irs->records[i].record);
  free (irs->records);

  if (irs->casefile != NULL)
    casefile_sleep (irs->casefile);

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
                      + irs->xsrt->value_cnt * sizeof (union value)
                      + 4 * sizeof (void *));
  max_cases = get_workspace() / approx_case_cost;
  if (max_cases > max_buffers)
    max_cases = max_buffers;
  irs->records = nmalloc (sizeof *irs->records, max_cases);
  if (irs->records != NULL)
    for (i = 0; i < max_cases; i++)
      if (!case_try_create (&irs->records[i].record, irs->xsrt->value_cnt))
        {
          max_cases = i;
          break;
        }
  irs->record_cap = max_cases;

  /* Fail if we didn't allocate an acceptable number of cases. */
  if (irs->records == NULL || max_cases < min_buffers)
    {
      msg (SE, _("Out of memory.  Could not allocate room for minimum of %d "
		 "cases of %d bytes each.  (PSPP workspace is currently "
		 "restricted to a maximum of %d KB.)"),
	   min_buffers, approx_case_cost, get_workspace() / 1024);
      return 0;
    }
  return 1;
}

/* Compares the VAR_CNT variables in VARS[] between the `value's at
   A and B, and returns a strcmp()-type result. */
static int
compare_record (const struct ccase *a, const struct ccase *b,
                const struct sort_criteria *criteria)
{
  int i;

  assert (a != NULL);
  assert (b != NULL);
  
  for (i = 0; i < criteria->crit_cnt; i++)
    {
      const struct sort_criterion *c = &criteria->crits[i];
      int result;
      
      if (c->width == 0)
        {
          double af = case_num (a, c->fv);
          double bf = case_num (b, c->fv);
          
          result = af < bf ? -1 : af > bf;
        }
      else
        result = memcmp (case_str (a, c->fv), case_str (b, c->fv), c->width);

      if (result != 0)
        return c->dir == SRT_ASCEND ? result : -result;
    }

  return 0;
}

/* Compares record-run tuples A and B on run number first, then
   on record, then on case index. */
static int
compare_record_run (const struct record_run *a,
                    const struct record_run *b,
                    struct initial_run_state *irs)
{
  int result = a->run < b->run ? -1 : a->run > b->run;
  if (result == 0)
    result = compare_record (&a->record, &b->record, irs->xsrt->criteria);
  if (result == 0)
    result = a->idx < b->idx ? -1 : a->idx > b->idx;
  return result;
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
  irs->run++;
  irs->case_cnt = 0;
  irs->casefile = casefile_create (irs->xsrt->value_cnt);
  casefile_to_disk (irs->casefile);
  case_nullify (&irs->last_output); 
}

/* Ends the current initial run.  */
static void
end_run (struct initial_run_state *irs)
{
  struct external_sort *xsrt = irs->xsrt;

  /* Record initial run. */
  if (irs->casefile != NULL) 
    {
      casefile_sleep (irs->casefile);
      if (xsrt->run_cnt >= xsrt->run_cap) 
        {
          xsrt->run_cap *= 2;
          xsrt->runs = xnrealloc (xsrt->runs,
                                  xsrt->run_cap, sizeof *xsrt->runs);
        }
      xsrt->runs[xsrt->run_cnt++] = irs->casefile;
      irs->casefile = NULL; 
    }
}

/* Writes a record to the current initial run. */
static void
output_record (struct initial_run_state *irs)
{
  struct record_run *record_run;
  struct ccase case_tmp;
  
  /* Extract minimum case from heap. */
  assert (irs->record_cnt > 0);
  pop_heap (irs->records, irs->record_cnt--, sizeof *irs->records,
            compare_record_run_minheap, irs);
  record_run = irs->records + irs->record_cnt;

  /* Bail if an error has occurred. */
  if (!irs->okay)
    return;

  /* Start new run if necessary. */
  assert (record_run->run == irs->run
          || record_run->run == irs->run + 1);
  if (record_run->run != irs->run)
    {
      end_run (irs);
      start_run (irs);
    }
  assert (record_run->run == irs->run);
  irs->case_cnt++;

  /* Write to disk. */
  if (irs->casefile != NULL)
    casefile_append (irs->casefile, &record_run->record);

  /* This record becomes last_output. */
  irs->last_output = case_tmp = record_run->record;
  record_run->record = irs->records[irs->record_cap - 1].record;
  irs->records[irs->record_cap - 1].record = case_tmp;
}

/* Merging. */

static int choose_merge (struct casefile *runs[], int run_cnt, int order);
static struct casefile *merge_once (struct external_sort *,
                                    struct casefile *[], size_t);

/* Repeatedly merges run until only one is left,
   and returns the final casefile.  */
static struct casefile *
merge (struct external_sort *xsrt)
{
  while (xsrt->run_cnt > 1)
    {
      int order = min (MAX_MERGE_ORDER, xsrt->run_cnt);
      int idx = choose_merge (xsrt->runs, xsrt->run_cnt, order);
      xsrt->runs[idx] = merge_once (xsrt, xsrt->runs + idx, order);
      remove_range (xsrt->runs, xsrt->run_cnt, sizeof *xsrt->runs,
                    idx + 1, order - 1);
      xsrt->run_cnt -= order - 1;
    }
  assert (xsrt->run_cnt == 1);
  xsrt->run_cnt = 0;
  return xsrt->runs[0];
}

/* Chooses ORDER runs out of the RUN_CNT runs in RUNS to merge,
   and returns the index of the first one.

   For stability, we must merge only consecutive runs.  For
   efficiency, we choose the shortest consecutive sequence of
   runs. */
static int
choose_merge (struct casefile *runs[], int run_cnt, int order) 
{
  int min_idx, min_sum;
  int cur_idx, cur_sum;
  int i;

  /* Sum up the length of the first ORDER runs. */
  cur_sum = 0;
  for (i = 0; i < order; i++)
    cur_sum += casefile_get_case_cnt (runs[i]);

  /* Find the shortest group of ORDER runs,
     using a running total for efficiency. */
  min_idx = 0;
  min_sum = cur_sum;
  for (cur_idx = 1; cur_idx + order <= run_cnt; cur_idx++)
    {
      cur_sum -= casefile_get_case_cnt (runs[cur_idx - 1]);
      cur_sum += casefile_get_case_cnt (runs[cur_idx + order - 1]);
      if (cur_sum < min_sum)
        {
          min_sum = cur_sum;
          min_idx = cur_idx;
        }
    }

  return min_idx;
}

/* Merges the RUN_CNT initial runs specified in INPUT_FILES into a
   new run, and returns the new run. */
static struct casefile *
merge_once (struct external_sort *xsrt,
            struct casefile **const input_files,
            size_t run_cnt)
{
  struct run
    {
      struct casefile *file;
      struct casereader *reader;
      struct ccase ccase;
    }
  *runs;

  struct casefile *output = NULL;
  int i;

  /* Open input files. */
  runs = xnmalloc (run_cnt, sizeof *runs);
  for (i = 0; i < run_cnt; i++) 
    {
      struct run *r = &runs[i];
      r->file = input_files[i];
      r->reader = casefile_get_destructive_reader (r->file);
      if (!casereader_read_xfer (r->reader, &r->ccase))
        {
          run_cnt--;
          i--;
        }
    }

  /* Create output file. */
  output = casefile_create (xsrt->value_cnt);
  casefile_to_disk (output);

  /* Merge. */
  while (run_cnt > 0) 
    {
      struct run *min_run, *run;
      
      /* Find minimum. */
      min_run = runs;
      for (run = runs + 1; run < runs + run_cnt; run++)
	if (compare_record (&run->ccase, &min_run->ccase, xsrt->criteria) < 0)
          min_run = run;

      /* Write minimum to output file. */
      casefile_append_xfer (output, &min_run->ccase);

      /* Read another case from minimum run. */
      if (!casereader_read_xfer (min_run->reader, &min_run->ccase))
        {
          casereader_destroy (min_run->reader);
          casefile_destroy (min_run->file);

          remove_element (runs, run_cnt, sizeof *runs, min_run - runs);
          run_cnt--;
        } 
    }

  casefile_sleep (output);
  free (runs);

  return output;
}
