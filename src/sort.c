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
#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "algorithm.h"
#include "alloc.h"
#include "case.h"
#include "casefile.h"
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

/* Sort direction. */
enum sort_direction
  {
    SRT_ASCEND,			/* A, B, C, ..., X, Y, Z. */
    SRT_DESCEND			/* Z, Y, X, ..., C, B, A. */
  };

/* A sort criterion. */
struct sort_criterion
  {
    int fv;                     /* Variable data index. */
    int width;                  /* 0=numeric, otherwise string widthe. */
    enum sort_direction dir;    /* Sort direction. */
  };

/* A set of sort criteria. */
struct sort_criteria 
  {
    struct sort_criterion *crits;
    size_t crit_cnt;
  };

static int compare_case_dblptrs (const void *, const void *, void *);
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
  int success;

  lex_match (T_BY);

  criteria = sort_parse_criteria (default_dict, NULL, NULL);
  if (criteria == NULL)
    return CMD_FAILURE;

  success = sort_active_file_in_place (criteria);
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

  vfm_source = storage_source_create (dst, default_dict);
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

/* Parses a list of sort keys and returns a struct sort_cases_pgm
   based on it.  Returns a null pointer on error. */
struct sort_criteria *
sort_parse_criteria (const struct dictionary *dict,
                     struct variable ***vars, int *var_cnt)
{
  struct sort_criteria *criteria;
  struct variable **local_vars = NULL;
  size_t local_var_cnt;

  assert ((vars == NULL) == (var_cnt == NULL));
  if (vars == NULL) 
    {
      vars = &local_vars;
      var_cnt = &local_var_cnt;
    }

  criteria = xmalloc (sizeof *criteria);
  criteria->crits = NULL;
  criteria->crit_cnt = 0;

  *vars = NULL;
  *var_cnt = 0;

  do
    {
      int prev_var_cnt = *var_cnt;
      enum sort_direction direction;

      /* Variables. */
      if (!parse_variables (dict, vars, var_cnt,
			    PV_NO_DUPLICATE | PV_APPEND | PV_NO_SCRATCH))
        goto error;

      /* Sort direction. */
      if (lex_match ('('))
	{
	  if (lex_match_id ("D") || lex_match_id ("DOWN"))
	    direction = SRT_DESCEND;
	  else if (lex_match_id ("A") || lex_match_id ("UP"))
            direction = SRT_ASCEND;
          else
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
      else
        direction = SRT_ASCEND;

      criteria->crits = xrealloc (criteria->crits,
                                  sizeof *criteria->crits * *var_cnt);
      criteria->crit_cnt = *var_cnt;
      for (; prev_var_cnt < criteria->crit_cnt; prev_var_cnt++) 
        {
          struct sort_criterion *c = &criteria->crits[prev_var_cnt];
          c->fv = (*vars)[prev_var_cnt]->fv;
          c->width = (*vars)[prev_var_cnt]->width;
          c->dir = direction;
        }
    }
  while (token != '.' && token != '/');

  free (local_vars);
  return criteria;

 error:
  free (local_vars);
  sort_destroy_criteria (criteria);
  return NULL;
}

/* Destroys a SORT CASES program. */
void
sort_destroy_criteria (struct sort_criteria *criteria) 
{
  if (criteria != NULL) 
    {
      free (criteria->crits);
      free (criteria);
    }
}

/* Reads all the cases from READER, which is destroyed.  Sorts
   the cases according to CRITERIA.  Returns the sorted cases in
   a newly created casefile. */
struct casefile *
sort_execute (struct casereader *reader, const struct sort_criteria *criteria)
{
  struct casefile *output;

  output = do_internal_sort (reader, criteria);
  if (output == NULL)
    output = do_external_sort (reader, criteria);
  casereader_destroy (reader);
  return output;
}

/* If the data is in memory, do an internal sort and return a new
   casefile for the data. */
static struct casefile *
do_internal_sort (struct casereader *reader,
                  const struct sort_criteria *criteria)
{
  const struct casefile *src;
  struct casefile *dst;
  struct ccase *cases, **case_ptrs;
  unsigned long case_cnt;

  src = casereader_get_casefile (reader);
  if (casefile_get_case_cnt (src) > 1 && !casefile_in_core (src))
    return NULL;
      
  case_cnt = casefile_get_case_cnt (src);
  cases = malloc (sizeof *cases * case_cnt);
  case_ptrs = malloc (sizeof *case_ptrs * case_cnt);
  if ((cases != NULL && case_ptrs != NULL) || case_cnt == 0) 
    {
      unsigned long case_idx;
      
      for (case_idx = 0; case_idx < case_cnt; case_idx++) 
        {
          int success = casereader_read_xfer (reader, &cases[case_idx]);
          assert (success);
          case_ptrs[case_idx] = &cases[case_idx];
        }

      sort (case_ptrs, case_cnt, sizeof *case_ptrs, compare_case_dblptrs,
            (void *) criteria);
      
      dst = casefile_create (casefile_get_value_cnt (src));
      for (case_idx = 0; case_idx < case_cnt; case_idx++) 
        casefile_append_xfer (dst, case_ptrs[case_idx]);
    }
  else
    dst = NULL;
  
  free (case_ptrs);
  free (cases);

  return dst;
}

/* Compares the variables specified by CRITERIA between the cases
   at A and B, and returns a strcmp()-type result. */
static int
compare_case_dblptrs (const void *a_, const void *b_, void *criteria_)
{
  struct sort_criteria *criteria = criteria_;
  struct ccase *const *pa = a_;
  struct ccase *const *pb = b_;
  struct ccase *a = *pa;
  struct ccase *b = *pb;
 
  return compare_record (a, b, criteria);
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

/* Sorts initial runs A and B in decending order by length. */
static int
compare_initial_runs (const void *a_, const void *b_, void *aux UNUSED) 
{
  const struct casefile *a = a_;
  const struct casefile *b = b_;
  unsigned long a_case_cnt = casefile_get_case_cnt (a);
  unsigned long b_case_cnt = casefile_get_case_cnt (b);
  
  return a_case_cnt > b_case_cnt ? -1 : a_case_cnt < b_case_cnt;
}

/* Results of an external sort. */
struct external_sort 
  {
    const struct sort_criteria *criteria; /* Sort criteria. */
    size_t value_cnt;                 /* Size of data in `union value's. */
    struct casefile **initial_runs;   /* Array of initial runs. */
    size_t run_cnt, run_cap;          /* Number of runs, allocated capacity. */
  };

/* Prototypes for helper functions. */
static int write_initial_runs (struct external_sort *, struct casereader *);
static int merge (struct external_sort *);
static void destroy_external_sort (struct external_sort *);

/* Performs an external sort of the active file according to the
   specification in SCP.  Forms initial runs using a heap as a
   reservoir.  Determines the optimum merge pattern via Huffman's
   method (see Knuth vol. 3, 2nd edition, p. 365-366), and merges
   according to that pattern. */
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
  xsrt->initial_runs = xmalloc (sizeof *xsrt->initial_runs * xsrt->run_cap);
  if (write_initial_runs (xsrt, reader) && merge (xsrt))
    {
      struct casefile *output = xsrt->initial_runs[0];
      xsrt->initial_runs[0] = NULL;
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
        casefile_destroy (xsrt->initial_runs[i]);
      free (xsrt->initial_runs);
      free (xsrt);
    }
}

/* Replacement selection. */

/* Pairs a record with a run number. */
struct record_run
  {
    int run;                    /* Run number of case. */
    struct ccase record;        /* Case data. */
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
static void process_case (struct initial_run_state *, const struct ccase *);
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
write_initial_runs (struct external_sort *xsrt, struct casereader *reader)
{
  struct initial_run_state *irs;
  struct ccase c;
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
    process_case (irs, &c);
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
process_case (struct initial_run_state *irs, const struct ccase *c)
{
  struct record_run *new_record_run;

  /* Compose record_run for this run and add to heap. */
  assert (irs->record_cnt < irs->record_cap - 1);
  new_record_run = irs->records + irs->record_cnt++;
  case_copy (&new_record_run->record, 0, c, 0, irs->xsrt->value_cnt);
  new_record_run->run = irs->run;
  if (!case_is_null (&irs->last_output)
      && compare_record (c, &irs->last_output, irs->xsrt->criteria) < 0)
    new_record_run->run = irs->run + 1;
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
  max_cases = get_max_workspace() / approx_case_cost;
  irs->records = malloc (sizeof *irs->records * max_cases);
  for (i = 0; i < max_cases; i++)
    if (!case_try_create (&irs->records[i].record, irs->xsrt->value_cnt))
      {
        max_cases = i;
        break;
      }
  irs->record_cap = max_cases;

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
   on the current record according to SCP. */
static int
compare_record_run (const struct record_run *a,
                    const struct record_run *b,
                    struct initial_run_state *irs)
{
  if (a->run != b->run)
    return a->run > b->run ? 1 : -1;
  else
    return compare_record (&a->record, &b->record, irs->xsrt->criteria);
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
      if (xsrt->run_cnt >= xsrt->run_cap) 
        {
          xsrt->run_cap *= 2;
          xsrt->initial_runs
            = xrealloc (xsrt->initial_runs,
                        sizeof *xsrt->initial_runs * xsrt->run_cap);
        }
      xsrt->initial_runs[xsrt->run_cnt++] = irs->casefile;
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

/* State of merging initial runs. */
struct merge_state 
  {
    struct external_sort *xsrt; /* External sort state. */
    struct ccase *cases;        /* Buffers. */
    size_t case_cnt;            /* Number of buffers. */
  };

struct run;
static struct casefile *merge_once (struct merge_state *,
                                    struct casefile *[], size_t);
static int mod (int, int);

/* Performs a series of P-way merges of initial runs. */
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
                      + xsrt->value_cnt * sizeof (union value)
                      + 4 * sizeof (void *));
  mrg.case_cnt = get_max_workspace() / approx_case_cost;
  mrg.cases = malloc (sizeof *mrg.cases * mrg.case_cnt);
  if (mrg.cases == NULL)
    goto done;
  for (i = 0; i < mrg.case_cnt; i++) 
    if (!case_try_create (&mrg.cases[i], xsrt->value_cnt)) 
      {
        mrg.case_cnt = i;
        break;
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
  else if (mrg.case_cnt / max_order * xsrt->value_cnt * sizeof (union value)
           < MIN_BUFFER_SIZE_BYTES)
    max_order = mrg.case_cnt / (MIN_BUFFER_SIZE_BYTES
                                / (xsrt->value_cnt * sizeof (union value)));
  if (max_order < 2)
    max_order = 2;
  if (max_order > xsrt->run_cnt)
    max_order = xsrt->run_cnt;

  /* Repeatedly merge the P shortest existing runs until only one run
     is left. */
  make_heap (xsrt->initial_runs, xsrt->run_cnt, sizeof *xsrt->initial_runs,
             compare_initial_runs, NULL);
  dummy_run_cnt = mod (1 - (int) xsrt->run_cnt, max_order - 1);
  assert (max_order == 2
          || (xsrt->run_cnt + dummy_run_cnt) % (max_order - 1) == 1);
  while (xsrt->run_cnt > 1)
    {
      struct casefile *output_run;
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
      output_run = merge_once (&mrg,
                               xsrt->initial_runs + xsrt->run_cnt, order);
      if (output_run == NULL)
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
    case_destroy (&mrg.cases[i]);
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

  abort ();
}

/* Merges the RUN_CNT initial runs specified in INPUT_RUNS into a
   new run.  Returns nonzero only if successful.  Adds an entry
   to MRG->xsrt->runs for the output file if and only if the
   output file is actually created.  Always deletes all the input
   files. */
static struct casefile *
merge_once (struct merge_state *mrg,
            struct casefile *input_runs[],
            size_t run_cnt)
{
  struct casereader *input_readers[MAX_MERGE_ORDER];
  struct ccase input_cases[MAX_MERGE_ORDER];
  struct casefile *output_casefile = NULL;
  int i;

  for (i = 0; i < run_cnt; i++) 
    {
      input_readers[i] = casefile_get_reader (input_runs[i]);
      if (!casereader_read_xfer (input_readers[i], &input_cases[i]))
        {
          run_cnt--;
          i--;
        }
    }
  
  output_casefile = casefile_create (mrg->xsrt->value_cnt);
  casefile_to_disk (output_casefile);

  /* Merge. */
  while (run_cnt > 0) 
    {
      size_t min_idx;

      /* Find minimum. */
      min_idx = 0;
      for (i = 1; i < run_cnt; i++)
	if (compare_record (&input_cases[i], &input_cases[min_idx],
                            mrg->xsrt->criteria) < 0)
          min_idx = i;

      /* Write minimum to output file. */
      casefile_append_xfer (output_casefile, &input_cases[i]);

      if (!casereader_read_xfer (input_readers[i], &input_cases[i]))
        {
          casereader_destroy (input_readers[i]);
          casefile_destroy (input_runs[i]);

          run_cnt--;
          input_runs[i] = input_runs[run_cnt--];
          input_readers[i] = input_readers[run_cnt--];
          input_cases[i] = input_cases[run_cnt--];
        } 
    }

  casefile_sleep (output_casefile);

  return output_casefile;
}
