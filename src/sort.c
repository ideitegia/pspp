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
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "alloc.h"
#include "approx.h"
#include "command.h"
#include "error.h"
#include "expr.h"
#include "heap.h"
#include "lexer.h"
#include "misc.h"
#include "sort.h"
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

/* Variables to sort. */
struct variable **v_sort;
int nv_sort;

/* Used when internal-sorting to a separate file. */
static struct case_list **separate_case_tab;

/* Other prototypes. */
static int compare_case_lists (const void *, const void *);
static int do_internal_sort (int separate);
static int do_external_sort (int separate);
int parse_sort_variables (void);
void read_sort_output (int (*write_case) (void));

/* Performs the SORT CASES procedures. */
int
cmd_sort_cases (void)
{
  /* First, just parse the command. */
  lex_match_id ("SORT");
  lex_match_id ("CASES");
  lex_match (T_BY);

  if (!parse_sort_variables ())
    return CMD_FAILURE;
      
  cancel_temporary ();

  /* Then it's time to do the actual work.  There are two cases:

     (internal sort) All the data is in memory.  In this case, we
     perform an EXECUTE to get the data into the desired form, then
     sort the cases in place, if it is still all in memory.

     (external sort) The data is not in memory.  It may be coming from
     a system file or other data file, etc.  In any case, it is now
     time to perform an external sort.  This is better explained in
     do_external_sort(). */

  /* Do all this dirty work. */
  {
    int success = sort_cases (0);
    free (v_sort);
    if (success)
      return lex_end_of_command ();
    else
      return CMD_FAILURE;
  }
}

/* Parses a list of sort variables into v_sort and nv_sort.  */
int
parse_sort_variables (void)
{
  v_sort = NULL;
  nv_sort = 0;
  do
    {
      int prev_nv_sort = nv_sort;
      int order = SRT_ASCEND;

      if (!parse_variables (&default_dict, &v_sort, &nv_sort,
			    PV_NO_DUPLICATE | PV_APPEND | PV_NO_SCRATCH))
	return 0;
      if (lex_match ('('))
	{
	  if (lex_match_id ("D") || lex_match_id ("DOWN"))
	    order = SRT_DESCEND;
	  else if (!lex_match_id ("A") && !lex_match_id ("UP"))
	    {
	      free (v_sort);
	      msg (SE, _("`A' or `D' expected inside parentheses."));
	      return 0;
	    }
	  if (!lex_match (')'))
	    {
	      free (v_sort);
	      msg (SE, _("`)' expected."));
	      return 0;
	    }
	}
      for (; prev_nv_sort < nv_sort; prev_nv_sort++)
	v_sort[prev_nv_sort]->p.srt.order = order;
    }
  while (token != '.' && token != '/');
  
  return 1;
}

/* Sorts the active file based on the key variables specified in
   global variables v_sort and nv_sort.  The output is either to the
   active file, if SEPARATE is zero, or to a separate file, if
   SEPARATE is nonzero.  In the latter case the output cases can be
   read with a call to read_sort_output().  (In the former case the
   output cases should be dealt with through the usual vfm interface.)

   The caller is responsible for freeing v_sort[]. */
int
sort_cases (int separate)
{
  assert (separate_case_tab == NULL);

  /* Not sure this is necessary but it's good to be safe. */
  if (separate && vfm_source == &sort_stream)
    procedure (NULL, NULL, NULL);
  
  /* SORT CASES cancels PROCESS IF. */
  expr_free (process_if_expr);
  process_if_expr = NULL;

  if (do_internal_sort (separate))
    return 1;

  page_to_disk ();
  return do_external_sort (separate);
}

/* If a reasonable situation is set up, do an internal sort of the
   data.  Return success. */
static int
do_internal_sort (int separate)
{
  if (vfm_source != &vfm_disk_stream)
    {
      if (vfm_source != &vfm_memory_stream)
	procedure (NULL, NULL, NULL);
      if (vfm_source == &vfm_memory_stream)
	{
	  struct case_list **case_tab = malloc (sizeof *case_tab
					 * (vfm_source_info.ncases + 1));
	  if (vfm_source_info.ncases == 0)
	    {
	      free (case_tab);
	      return 1;
	    }
	  if (case_tab != NULL)
	    {
	      struct case_list *clp = memory_source_cases;
	      struct case_list **ctp = case_tab;
	      int i;

	      for (; clp; clp = clp->next)
		*ctp++ = clp;
	      qsort (case_tab, vfm_source_info.ncases, sizeof *case_tab,
		     compare_case_lists);

	      if (!separate)
		{
		  memory_source_cases = case_tab[0];
		  for (i = 1; i < vfm_source_info.ncases; i++)
		    case_tab[i - 1]->next = case_tab[i];
		  case_tab[vfm_source_info.ncases - 1]->next = NULL;
		  free (case_tab);
		} else {
		  case_tab[vfm_source_info.ncases] = NULL;
		  separate_case_tab = case_tab;
		}
	      
	      return 1;
	    }
	}
    }
  return 0;
}

/* Compares the NV_SORT variables in V_SORT[] between the
   `case_list's at A and B, and returns a strcmp()-type
   result. */
static int
compare_case_lists (const void *a_, const void *b_)
{
  struct case_list *const *pa = a_;
  struct case_list *const *pb = b_;
  struct case_list *a = *pa;
  struct case_list *b = *pb;
  struct variable *v;
  int result = 0;
  int i;

  for (i = 0; i < nv_sort; i++)
    {
      v = v_sort[i];
      
      if (v->type == NUMERIC)
	{
          double af = a->c.data[v->fv].f;
          double bf = b->c.data[v->fv].f;

          result = af < bf ? -1 : af > bf;
	}
      else
        result = memcmp (a->c.data[v->fv].s, b->c.data[v->fv].s, v->width);

      if (result != 0)
        break;
    }

  if (v->p.srt.order == SRT_DESCEND)
    result = -result;
  return result;
}

/* External sort. */

/* Maximum number of input + output file handles. */
#if defined FOPEN_MAX && (FOPEN_MAX - 5 < 18)
#define MAX_FILE_HANDLES	(FOPEN_MAX - 5)
#else
#define MAX_FILE_HANDLES	18
#endif

#if MAX_FILE_HANDLES < 3
#error At least 3 file handles must be available for sorting.
#endif

/* Number of input buffers. */
#define N_INPUT_BUFFERS		(MAX_FILE_HANDLES - 1)

/* Maximum order of merge.  This is the value suggested by Knuth;
   specifically, he said to use tree selection, which we don't
   implement, for larger orders of merge. */
#define MAX_MERGE_ORDER		7

/* Minimum total number of records for buffers. */
#define MIN_BUFFER_TOTAL_SIZE_RECS	64

/* Minimum single input or output buffer size, in bytes and records. */
#define MIN_BUFFER_SIZE_BYTES	4096
#define MIN_BUFFER_SIZE_RECS	16

/* Structure for replacement selection tree. */
struct repl_sel_tree
  {
    struct repl_sel_tree *loser;/* Loser associated w/this internal node. */
    int rn;			/* Run number of `loser'. */
    struct repl_sel_tree *fe;	/* Internal node above this external node. */
    struct repl_sel_tree *fi;	/* Internal node above this internal node. */
    union value record[1];	/* The case proper. */
  };

/* Static variables used for sorting. */
static struct repl_sel_tree **x; /* Buffers. */
static int x_max;		/* Size of buffers, in records. */
static int records_per_buffer;	/* Number of records in each buffer. */

/* In the merge phase, the first N_INPUT_BUFFERS handle[] elements are
   input files and the last element is the output file.  Before that,
   they're all used as output files, although the last one is
   segregated. */
static FILE *handle[MAX_FILE_HANDLES];	/* File handles. */

/* Now, MAX_FILE_HANDLES is the maximum number of files we will *try*
   to open.  But if we can't open that many, max_handles will be set
   to the number we apparently can open. */
static int max_handles;		/* Maximum number of handles. */

/* When we create temporary files, they are all put in the same
   directory and numbered sequentially from zero.  tmp_basename is the
   drive/directory, etc., and tmp_extname can be sprintf() with "%08x"
   to the file number, then tmp_basename used to open the file. */
static char *tmp_basename;	/* Temporary file basename. */
static char *tmp_extname;	/* Temporary file extension name. */

/* We use Huffman's method to determine the merge pattern.  This means
   that we need to know which runs are the shortest at any given time.
   Priority queues as implemented by heap.c are a natural for this
   task (probably because I wrote the code specifically for it). */
static struct heap *huffman_queue;	/* Huffman priority queue. */

/* Prototypes for helper functions. */
static void sort_stream_write (void);
static int write_initial_runs (int separate);
static int allocate_cases (void);
static int allocate_file_handles (void);
static int merge (void);
static void rmdir_temp_dir (void);

/* Performs an external sort of the active file.  A description of the
   procedure follows.  All page references refer to Knuth's _Art of
   Computer Programming, Vol. 3: Sorting and Searching_, which is the
   canonical resource for sorting.

   1. The data is read and S initial runs are formed through the
   action of algorithm 5.4.1R (replacement selection).

   2. Huffman's method (p. 365-366) is used to determine the optimum
   merge pattern.

   3. If an OS that supports overlapped reading, writing, and
   computing is being run, we should use 5.4.6F for forecasting.
   Otherwise, buffers are filled only when they run out of data.
   FIXME: Since the author of PSPP uses GNU/Linux, which does not
   yet implement overlapped r/w/c, 5.4.6F is not used.

   4. We perform P-way merges:

   (a) The desired P is the smallest P such that ceil(ln(S)/ln(P))
   is minimized.  (FIXME: Since I don't have an algorithm for
   minimizing this, it's just set to MAX_MERGE_ORDER.)

   (b) P is reduced if the selected value would make input buffers
   less than 4096 bytes each, or 16 records, whichever is larger.

   (c) P is reduced if we run out of available file handles or space
   for file handles.

   (d) P is reduced if we don't have space for one or two output
   buffers, which have the same minimum size as input buffers.  (We
   need two output buffers if 5.4.6F is in use for forecasting.)  */
static int
do_external_sort (int separate)
{
  int success = 0;

  assert (MAX_FILE_HANDLES >= 3);

  x = NULL;
  tmp_basename = NULL;

  huffman_queue = heap_create (512);
  if (huffman_queue == NULL)
    return 0;

  if (!allocate_cases ())
    goto lossage;

  if (!allocate_file_handles ())
    goto lossage;

  if (!write_initial_runs (separate))
    goto lossage;

  merge ();

  success = 1;

  /* Despite the name, flow of control comes here regardless of
     whether or not the sort is successful. */
lossage:
  heap_destroy (huffman_queue);

  if (x)
    {
      int i;

      for (i = 0; i <= x_max; i++)
	free (x[i]);
      free (x);
    }

  if (!success)
    rmdir_temp_dir ();

  return success;
}

#if !HAVE_GETPID
#define getpid() (0)
#endif

/* Sets up to open temporary files. */
/* PORTME: This creates a directory for temporary files.  Some OSes
   might not have that concept... */
static int
allocate_file_handles (void)
{
  const char *dir;		/* Directory prefix. */
  char *buf;			/* String buffer. */
  char *cp;			/* Pointer into buf. */

  dir = getenv ("SPSSTMPDIR");
  if (dir == NULL)
    dir = getenv ("SPSSXTMPDIR");
  if (dir == NULL)
    dir = getenv ("TMPDIR");
#ifdef P_tmpdir
  if (dir == NULL)
    dir = P_tmpdir;
#endif
#if __unix__
  if (dir == NULL)
    dir = "/tmp";
#elif __MSDOS__
  if (dir == NULL)
    dir = getenv ("TEMP");
  if (dir == NULL)
    dir = getenv ("TMP");
  if (dir == NULL)
    dir = "\\";
#else
  dir = "";
#endif

  buf = xmalloc (strlen (dir) + 1 + 4 + 8 + 4 + 1 + INT_DIGITS + 1);
  cp = spprintf (buf, "%s%c%04lX%04lXpspp", dir, DIR_SEPARATOR,
		 ((long) time (0)) & 0xffff, ((long) getpid ()) & 0xffff);
  if (-1 == mkdir (buf, S_IRWXU))
    {
      free (buf);
      msg (SE, _("%s: Cannot create temporary directory: %s."),
	   buf, strerror (errno));
      return 0;
    }
  *cp++ = DIR_SEPARATOR;

  tmp_basename = buf;
  tmp_extname = cp;

  max_handles = MAX_FILE_HANDLES;

  return 1;
}

/* Removes the directory created for temporary files, if one exists.
   Also frees tmp_basename. */
static void
rmdir_temp_dir (void)
{
  if (NULL == tmp_basename)
    return;

  tmp_extname[-1] = '\0';
  if (rmdir (tmp_basename) == -1)
    msg (SE, _("%s: Error removing directory for temporary files: %s."),
	 tmp_basename, strerror (errno));

  free (tmp_basename);
}

/* Allocates room for lots of cases as a buffer. */
static int
allocate_cases (void)
{
  /* This is the size of one case. */
  const int case_size = (sizeof (struct repl_sel_tree)
			 + sizeof (union value) * (default_dict.nval - 1)
			 + sizeof (struct repl_sel_tree *));

  x = NULL;

  /* Allocate as many cases as we can, assuming a space of four
     void pointers for malloc()'s internal bookkeeping. */
  x_max = MAX_WORKSPACE / (case_size + 4 * sizeof (void *));
  x = malloc (sizeof (struct repl_sel_tree *) * x_max);
  if (x != NULL)
    {
      int i;

      for (i = 0; i < x_max; i++)
	{
	  x[i] = malloc (sizeof (struct repl_sel_tree)
			 + sizeof (union value) * (default_dict.nval - 1));
	  if (x[i] == NULL)
	    break;
	}
      x_max = i;
    }
  if (x == NULL || x_max < MIN_BUFFER_TOTAL_SIZE_RECS)
    {
      if (x != NULL)
	{
	  int i;
	  
	  for (i = 0; i < x_max; i++)
	    free (x[i]);
	}
      free (x);
      msg (SE, _("Out of memory.  Could not allocate room for minimum of %d "
		 "cases of %d bytes each.  (PSPP workspace is currently "
		 "restricted to a maximum of %d KB.)"),
	   MIN_BUFFER_TOTAL_SIZE_RECS, case_size, MAX_WORKSPACE / 1024);
      x_max = 0;
      x = NULL;
      return 0;
    }

  /* The last element of the array is used to store lastkey. */
  x_max--;

  debug_printf ((_("allocated %d cases == %d bytes\n"),
		 x_max, x_max * case_size));
  return 1;
}

/* Replacement selection. */

static int rmax, rc, rq;
static struct repl_sel_tree *q;
static union value *lastkey;
static int run_no, file_index;
static int deferred_abort;
static int run_length;

static int compare_record (union value *, union value *);

static inline void
output_record (union value *v)
{
  union value *src_case;
  
  if (deferred_abort)
    return;

  if (compaction_necessary)
    {
      compact_case (compaction_case, (struct ccase *) v);
      src_case = (union value *) compaction_case;
    }
  else
    src_case = (union value *) v;

  if ((int) fwrite (src_case, sizeof *src_case, compaction_nval,
		    handle[file_index])
      != compaction_nval)
    {
      deferred_abort = 1;
      sprintf (tmp_extname, "%08x", run_no);
      msg (SE, _("%s: Error writing temporary file: %s."),
	   tmp_basename, strerror (errno));
      return;
    }

  run_length++;
}

static int
close_handle (int i)
{
  int result = fclose (handle[i]);
  msg (VM (2), _("SORT: Closing handle %d."), i);
  
  handle[i] = NULL;
  if (EOF == result)
    {
      sprintf (tmp_extname, "%08x", i);
      msg (SE, _("%s: Error closing temporary file: %s."),
	   tmp_basename, strerror (errno));
      return 0;
    }
  return 1;
}

static int
close_handles (int beg, int end)
{
  int success = 1;
  int i;

  for (i = beg; i < end; i++)
    success &= close_handle (i);
  return success;
}

static int
open_handle_w (int handle_no, int run_no)
{
  sprintf (tmp_extname, "%08x", run_no);
  msg (VM (1), _("SORT: %s: Opening for writing as run %d."),
       tmp_basename, run_no);

  /* The `x' modifier causes the GNU C library to insist on creating a
     new file--if the file already exists, an error is signaled.  The
     ANSI C standard says that other libraries should ignore anything
     after the `w+b', so it shouldn't be a problem. */
  return NULL != (handle[handle_no] = fopen (tmp_basename, "w+bx"));
}

static int
open_handle_r (int handle_no, int run_no)
{
  FILE *f;

  sprintf (tmp_extname, "%08x", run_no);
  msg (VM (1), _("SORT: %s: Opening for writing as run %d."),
       tmp_basename, run_no);
  f = handle[handle_no] = fopen (tmp_basename, "rb");

  if (f == NULL)
    {
      msg (SE, _("%s: Error opening temporary file for reading: %s."),
	   tmp_basename, strerror (errno));
      return 0;
    }
  
  return 1;
}

/* Begins a new initial run, specifically its output file. */
static void
begin_run (void)
{
  /* Decide which handle[] to use.  If run_no is max_handles or
     greater, then we've run out of handles so it's time to just do
     one file at a time, which by default is handle 0. */
  file_index = (run_no < max_handles ? run_no : 0);
  run_length = 0;

  /* Alright, now create the temporary file. */
  if (open_handle_w (file_index, run_no) == 0)
    {
      /* Failure to create the temporary file.  Check if there are
         unacceptably few files already open. */
      if (file_index < 3)
	{
	  deferred_abort = 1;
	  msg (SE, _("%s: Error creating temporary file: %s."),
	       tmp_basename, strerror (errno));
	  return;
	}

      /* Close all the open temporary files. */
      if (!close_handles (0, file_index))
	return;

      /* Now try again to create the temporary file. */
      max_handles = file_index;
      file_index = 0;
      if (open_handle_w (0, run_no) == 0)
	{
	  /* It still failed, report it this time. */
	  deferred_abort = 1;
	  msg (SE, _("%s: Error creating temporary file: %s."),
	       tmp_basename, strerror (errno));
	  return;
	}
    }
}

/* Ends the current initial run.  Just increments run_no if no initial
   run has been started yet. */
static void
end_run (void)
{
  /* Close file handles if necessary. */
  {
    int result;

    if (run_no == max_handles - 1)
      result = close_handles (0, max_handles);
    else if (run_no >= max_handles)
      result = close_handle (0);
    else
      result = 1;
    if (!result)
      deferred_abort = 1;
  }

  /* Advance to next run. */
  run_no++;
  if (run_no)
    heap_insert (huffman_queue, run_no - 1, run_length);
}

/* Performs 5.4.1R. */
static int
write_initial_runs (int separate)
{
  run_no = -1;
  deferred_abort = 0;

  /* Steps R1, R2, R3. */
  rmax = 0;
  rc = 0;
  lastkey = NULL;
  q = x[0];
  rq = 0;
  {
    int j;

    for (j = 0; j < x_max; j++)
      {
	struct repl_sel_tree *J = x[j];

	J->loser = J;
	J->rn = 0;
	J->fe = x[(x_max + j) / 2];
	J->fi = x[j / 2];
	memset (J->record, 0, default_dict.nval * sizeof (union value));
      }
  }

  /* Most of the iterations of steps R4, R5, R6, R7, R2, R3, ... */
  if (!separate)
    {
      if (vfm_sink)
	vfm_sink->destroy_sink ();
      vfm_sink = &sort_stream;
    }
  procedure (NULL, NULL, NULL);

  /* Final iterations of steps R4, R5, R6, R7, R2, R3, ... */
  for (;;)
    {
      struct repl_sel_tree *t;

      /* R4. */
      rq = rmax + 1;

      /* R5. */
      t = q->fe;

      /* R6 and R7. */
      for (;;)
	{
	  /* R6. */
	  if (t->rn < rq
	      || (t->rn == rq
		  && compare_record (t->loser->record, q->record) < 0))
	    {
	      struct repl_sel_tree *temp_tree;
	      int temp_int;

	      temp_tree = t->loser;
	      t->loser = q;
	      q = temp_tree;

	      temp_int = t->rn;
	      t->rn = rq;
	      rq = temp_int;
	    }

	  /* R7. */
	  if (t == x[1])
	    break;
	  t = t->fi;
	}

      /* R2. */
      if (rq != rc)
	{
	  end_run ();
	  if (rq > rmax)
	    break;
	  begin_run ();
	  rc = rq;
	}

      /* R3. */
      if (rq != 0)
	{
	  output_record (q->record);
	  lastkey = x[x_max]->record;
	  memcpy (lastkey, q->record, sizeof (union value) * vfm_sink_info.nval);
	}
    }
  assert (run_no == rmax);

  /* If an unrecoverable error occurred somewhere in the above code,
     then the `deferred_abort' flag would have been set.  */
  if (deferred_abort)
    {
      int i;

      for (i = 0; i < max_handles; i++)
	if (handle[i] != NULL)
	  {
	    sprintf (tmp_extname, "%08x", i);

	    if (fclose (handle[i]) == EOF)
	      msg (SE, _("%s: Error closing temporary file: %s."),
		   tmp_basename, strerror (errno));

	    if (remove (tmp_basename) != 0)
	      msg (SE, _("%s: Error removing temporary file: %s."),
		   tmp_basename, strerror (errno));

	    handle[i] = NULL;
	  }
      return 0;
    }

  return 1;
}

/* Compares the NV_SORT variables in V_SORT[] between the `value's at
   A and B, and returns a strcmp()-type result. */
static int
compare_record (union value * a, union value * b)
{
  int i;
  int result = 0;
  struct variable *v;

  assert (a != NULL);
  if (b == NULL)		/* Sort NULLs after everything else. */
    return -1;

  for (i = 0; i < nv_sort; i++)
    {
      v = v_sort[i];

      if (v->type == NUMERIC)
	{
	  if (approx_ne (a[v->fv].f, b[v->fv].f))
	    {
	      result = (a[v->fv].f > b[v->fv].f) ? 1 : -1;
	      break;
	    }
	}
      else
	{
	  result = memcmp (a[v->fv].s, b[v->fv].s, v->width);
	  if (result != 0)
	    break;
	}
    }

  if (v->p.srt.order == SRT_ASCEND)
    return result;
  else
    {
      assert (v->p.srt.order == SRT_DESCEND);
      return -result;
    }
}

/* Merging. */

static int merge_once (int run_index[], int run_length[], int n_runs);

/* Modula function as defined by Knuth. */
static int
mod (int x, int y)
{
  int result;

  if (y == 0)
    return x;
  result = abs (x) % abs (y);
  if (y < 0)
    result = -result;
  return result;
}

/* Performs a series of P-way merges of initial runs using Huffman's
   method. */
static int
merge (void)
{
  /* Order of merge. */
  int order;

  /* Idiot check. */
  assert (MIN_BUFFER_SIZE_RECS * 2 <= MIN_BUFFER_TOTAL_SIZE_RECS - 1);

  /* Close all the input files.  I hope that the boundary conditions
     are correct on this but I'm not sure. */
  if (run_no < max_handles)
    {
      int i;

      for (i = 0; i < run_no; )
	if (!close_handle (i++))
	  {
	    for (; i < run_no; i++)
	      close_handle (i);
	    return 0;
	  }
    }

  /* Determine order of merge. */
  order = MAX_MERGE_ORDER;
  if (x_max / order < MIN_BUFFER_SIZE_RECS)
    order = x_max / MIN_BUFFER_SIZE_RECS;
  else if (x_max / order * sizeof (union value) * default_dict.nval
	   < MIN_BUFFER_SIZE_BYTES)
    order = x_max / (MIN_BUFFER_SIZE_BYTES
		     / (sizeof (union value) * (default_dict.nval - 1)));

  /* Make sure the order of merge is bounded. */
  if (order < 2)
    order = 2;
  if (order > rmax)
    order = rmax;
  assert (x_max / order > 0);

  /* Calculate number of records per buffer. */
  records_per_buffer = x_max / order;

  /* Add (1 - S) mod (P - 1) dummy runs of length 0. */
  {
    int n_dummy_runs = mod (1 - rmax, order - 1);
    debug_printf (("rmax=%d, order=%d, n_dummy_runs=%d\n",
		   rmax, order, n_dummy_runs));
    assert (n_dummy_runs >= 0);
    while (n_dummy_runs--)
      {
	heap_insert (huffman_queue, -2, 0);
	rmax++;
      }
  }

  /* Repeatedly merge the P shortest existing runs until only one run
     is left. */
  while (rmax > 1)
    {
      int run_index[MAX_MERGE_ORDER];
      int run_length[MAX_MERGE_ORDER];
      int total_run_length = 0;
      int i;

      assert (rmax >= order);

      /* Find the shortest runs; put them in runs[] in reverse order
         of length, to force dummy runs of length 0 to the end of the
         list. */
      debug_printf ((_("merging runs")));
      for (i = order - 1; i >= 0; i--)
	{
	  run_index[i] = heap_delete (huffman_queue, &run_length[i]);
	  assert (run_index[i] != -1);
	  total_run_length += run_length[i];
	  debug_printf ((" %d(%d)", run_index[i], run_length[i]));
	}
      debug_printf ((_(" into run %d(%d)\n"), run_no, total_run_length));

      if (!merge_once (run_index, run_length, order))
	{
	  int index;

	  while (-1 != (index = heap_delete (huffman_queue, NULL)))
	    {
	      sprintf (tmp_extname, "%08x", index);
	      if (remove (tmp_basename) != 0)
		msg (SE, _("%s: Error removing temporary file: %s."),
		     tmp_basename, strerror (errno));
	    }

	  return 0;
	}

      if (!heap_insert (huffman_queue, run_no++, total_run_length))
	{
	  msg (SE, _("Out of memory expanding Huffman priority queue."));
	  return 0;
	}

      rmax -= order - 1;
    }

  /* There should be exactly one element in the priority queue after
     all that merging.  This represents the entire sorted active file.
     So we could find a total case count by deleting this element from
     the queue. */
  assert (heap_size (huffman_queue) == 1);

  return 1;
}

/* Merges N_RUNS initial runs into a new run.  The jth run for 0 <= j
   < N_RUNS is taken from temporary file RUN_INDEX[j]; it is composed
   of RUN_LENGTH[j] cases. */
static int
merge_once (int run_index[], int run_length[], int n_runs)
{
  /* For each run, the number of records remaining in its buffer. */
  int buffered[MAX_MERGE_ORDER];

  /* For each run, the index of the next record in the buffer. */
  int buffer_ptr[MAX_MERGE_ORDER];

  /* Open input files. */
  {
    int i;

    for (i = 0; i < n_runs; i++)
      if (run_index[i] != -2 && !open_handle_r (i, run_index[i]))
	{
	  /* Close and remove temporary files. */
	  while (i--)
	    {
	      close_handle (i);
	      sprintf (tmp_extname, "%08x", i);
	      if (remove (tmp_basename) != 0)
		msg (SE, _("%s: Error removing temporary file: %s."),
		     tmp_basename, strerror (errno));
	    }

	  return 0;
	}
  }

  /* Create output file. */
  if (!open_handle_w (N_INPUT_BUFFERS, run_no))
    {
      msg (SE, _("%s: Error creating temporary file for merge: %s."),
	   tmp_basename, strerror (errno));
      goto lossage;
    }

  /* Prime each buffer. */
  {
    int i;

    for (i = 0; i < n_runs; i++)
      if (run_index[i] == -2)
	{
	  n_runs = i;
	  break;
	}
      else
	{
	  int j;
	  int ofs = records_per_buffer * i;

	  buffered[i] = min (records_per_buffer, run_length[i]);
	  for (j = 0; j < buffered[i]; j++)
	    if ((int) fread (x[j + ofs]->record, sizeof (union value),
			     default_dict.nval, handle[i])
		!= default_dict.nval)
	      {
		sprintf (tmp_extname, "%08x", run_index[i]);
		if (ferror (handle[i]))
		  msg (SE, _("%s: Error reading temporary file in merge: %s."),
		       tmp_basename, strerror (errno));
		else
		  msg (SE, _("%s: Unexpected end of temporary file in merge."),
		       tmp_basename);
		goto lossage;
	      }
	  buffer_ptr[i] = ofs;
	  run_length[i] -= buffered[i];
	}
  }

  /* Perform the merge proper. */
  while (n_runs)		/* Loop while some data is left. */
    {
      int i;
      int min = 0;

      for (i = 1; i < n_runs; i++)
	if (compare_record (x[buffer_ptr[min]]->record,
			    x[buffer_ptr[i]]->record) > 0)
	  min = i;

      if ((int) fwrite (x[buffer_ptr[min]]->record, sizeof (union value),
			default_dict.nval, handle[N_INPUT_BUFFERS])
	  != default_dict.nval)
	{
	  sprintf (tmp_extname, "%08x", run_index[i]);
	  msg (SE, _("%s: Error writing temporary file in "
	       "merge: %s."), tmp_basename, strerror (errno));
	  goto lossage;
	}

      /* Remove one case from the buffer for this input file. */
      if (--buffered[min] == 0)
	{
	  /* The input buffer is empty.  Do any cases remain in the
	     initial run on disk? */
	  if (run_length[min])
	    {
	      /* Yes.  Read them in. */

	      int j;
	      int ofs;

	      /* Reset the buffer pointer.  Note that we can't simply
	         set it to (i * records_per_buffer) since the run
	         order might have changed. */
	      ofs = buffer_ptr[min] -= buffer_ptr[min] % records_per_buffer;

	      buffered[min] = min (records_per_buffer, run_length[min]);
	      for (j = 0; j < buffered[min]; j++)
		if ((int) fread (x[j + ofs]->record, sizeof (union value),
				 default_dict.nval, handle[min])
		    != default_dict.nval)
		  {
		    sprintf (tmp_extname, "%08x", run_index[min]);
		    if (ferror (handle[min]))
		      msg (SE, _("%s: Error reading temporary file in "
				 "merge: %s."),
			   tmp_basename, strerror (errno));
		    else
		      msg (SE, _("%s: Unexpected end of temporary file "
				 "in merge."),
			   tmp_basename);
		    goto lossage;
		  }
	      run_length[min] -= buffered[min];
	    }
	  else
	    {
	      /* No.  Delete this run. */

	      /* Close the file. */
	      FILE *f = handle[min];
	      handle[min] = NULL;
	      sprintf (tmp_extname, "%08x", run_index[min]);
	      if (fclose (f) == EOF)
		msg (SE, _("%s: Error closing temporary file in merge: "
		     "%s."), tmp_basename, strerror (errno));

	      /* Delete the file. */
	      if (remove (tmp_basename) != 0)
		msg (SE, _("%s: Error removing temporary file in merge: "
		     "%s."), tmp_basename, strerror (errno));

	      n_runs--;
	      if (min != n_runs)
		{
		  /* Since this isn't the last run, we move the last
		     run into its spot to force all the runs to be
		     contiguous. */
		  run_index[min] = run_index[n_runs];
		  run_length[min] = run_length[n_runs];
		  buffer_ptr[min] = buffer_ptr[n_runs];
		  buffered[min] = buffered[n_runs];
		  handle[min] = handle[n_runs];
		}
	    }
	}
      else
	buffer_ptr[min]++;
    }

  /* Close output file. */
  {
    FILE *f = handle[N_INPUT_BUFFERS];
    handle[N_INPUT_BUFFERS] = NULL;
    if (fclose (f) == EOF)
      {
	sprintf (tmp_extname, "%08x", run_no);
	msg (SE, _("%s: Error closing temporary file in merge: "
		   "%s."),
	     tmp_basename, strerror (errno));
	return 0;
      }
  }

  return 1;

lossage:
  /* Close all the input and output files. */
  {
    int i;

    for (i = 0; i < n_runs; i++)
      if (run_length[i] != 0)
	{
	  close_handle (i);
	  sprintf (tmp_basename, "%08x", run_index[i]);
	  if (remove (tmp_basename) != 0)
	    msg (SE, _("%s: Error removing temporary file: %s."),
		 tmp_basename, strerror (errno));
	}
  }
  close_handle (N_INPUT_BUFFERS);
  sprintf (tmp_basename, "%08x", run_no);
  if (remove (tmp_basename) != 0)
    msg (SE, _("%s: Error removing temporary file: %s."),
	 tmp_basename, strerror (errno));
  return 0;
}

/* External sort input program. */

/* Reads all the records from the source stream and passes them
   to write_case(). */
void
sort_stream_read (void)
{
  read_sort_output (write_case);
}

/* Reads all the records from the output stream and passes them to the
   function provided, which must have an interface identical to
   write_case(). */
void
read_sort_output (int (*write_case) (void))
{
  int i;
  FILE *f;

  if (separate_case_tab)
    {
      struct ccase *save_temp_case = temp_case;
      struct case_list **p;

      for (p = separate_case_tab; *p; p++)
	{
	  temp_case = &(*p)->c;
	  write_case ();
	}
      
      free (separate_case_tab);
      separate_case_tab = NULL;
	    
      temp_case = save_temp_case;
    } else {
      sprintf (tmp_extname, "%08x", run_no - 1);
      f = fopen (tmp_basename, "rb");
      if (!f)
	{
	  msg (ME, _("%s: Cannot open sort result file: %s."), tmp_basename,
	       strerror (errno));
	  err_failure ();
	  return;
	}

      for (i = 0; i < vfm_source_info.ncases; i++)
	{
	  if (!fread (temp_case, vfm_source_info.case_size, 1, f))
	    {
	      if (ferror (f))
		msg (ME, _("%s: Error reading sort result file: %s."),
		     tmp_basename, strerror (errno));
	      else
		msg (ME, _("%s: Unexpected end of sort result file: %s."),
		     tmp_basename, strerror (errno));
	      err_failure ();
	      break;
	    }

	  if (!write_case ())
	    break;
	}

      if (fclose (f) == EOF)
	msg (ME, _("%s: Error closing sort result file: %s."), tmp_basename,
	     strerror (errno));

      if (remove (tmp_basename) != 0)
	msg (ME, _("%s: Error removing sort result file: %s."), tmp_basename,
	     strerror (errno));
      else
	rmdir_temp_dir ();
    }
}

#if 0 /* dead code */
/* Alternate interface to sort_stream_write used for external sorting
   when SEPARATE is true. */
static int
write_separate (struct ccase *c)
{
  assert (c == temp_case);

  sort_stream_write ();
  return 1;
}
#endif

/* Performs one iteration of 5.4.1R steps R4, R5, R6, R7, R2, and
   R3. */
static void
sort_stream_write (void)
{
  struct repl_sel_tree *t;

  /* R4. */
  memcpy (q->record, temp_case->data, vfm_sink_info.case_size);
  if (compare_record (q->record, lastkey) < 0)
    if (++rq > rmax)
      rmax = rq;

  /* R5. */
  t = q->fe;

  /* R6 and R7. */
  for (;;)
    {
      /* R6. */
      if (t->rn < rq
	  || (t->rn == rq && compare_record (t->loser->record, q->record) < 0))
	{
	  struct repl_sel_tree *temp_tree;
	  int temp_int;

	  temp_tree = t->loser;
	  t->loser = q;
	  q = temp_tree;

	  temp_int = t->rn;
	  t->rn = rq;
	  rq = temp_int;
	}

      /* R7. */
      if (t == x[1])
	break;
      t = t->fi;
    }

  /* R2. */
  if (rq != rc)
    {
      end_run ();
      begin_run ();
      assert (rq <= rmax);
      rc = rq;
    }

  /* R3. */
  if (rq != 0)
    {
      output_record (q->record);
      lastkey = x[x_max]->record;
      memcpy (lastkey, q->record, vfm_sink_info.case_size);
    }
}

/* Switches mode from sink to source. */
void
sort_stream_mode (void)
{
  /* If this is not done, then we get the following source/sink pairs:
     source=memory/disk/DATA LIST/etc., sink=SORT; source=SORT,
     sink=SORT; which is not good. */
  vfm_sink = NULL;
}

struct case_stream sort_stream =
  {
    NULL,
    sort_stream_read,
    sort_stream_write,
    sort_stream_mode,
    NULL,
    NULL,
    "SORT",
  };
