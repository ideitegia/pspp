/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2012, 2013, 2014 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

/* FIXME:

   - Pearson's R (but not Spearman!) is off a little.
   - T values for Spearman's R and Pearson's R are wrong.
   - How to calculate significance of symmetric and directional measures?
   - Asymmetric ASEs and T values for lambda are wrong.
   - ASE of Goodman and Kruskal's tau is not calculated.
   - ASE of symmetric somers' d is wrong.
   - Approx. T of uncertainty coefficient is wrong.

*/

#include <config.h>

#include <ctype.h>
#include <float.h>
#include <gsl/gsl_cdf.h>
#include <stdlib.h>
#include <stdio.h>

#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/data-out.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/dictionary/split-file.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmap.h"
#include "libpspp/hmapx.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "output/tab.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"
#include "gl/xsize.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* (headers) */

/* (specification)
   crosstabs (crs_):
     *^tables=custom;
     +variables=custom;
     missing=miss:!table/include/report;
     +write[wr_]=none,cells,all;
     +format=val:!avalue/dvalue,
	     indx:!noindex/index,
	     tabl:!tables/notables,
	     box:!box/nobox,
	     pivot:!pivot/nopivot;
     +cells[cl_]=count,expected,row,column,total,residual,sresidual,
		 asresidual,all,none;
     +statistics[st_]=chisq,phi,cc,lambda,uc,none,btau,ctau,risk,gamma,d,
		      kappa,eta,corr,all.
*/
/* (declarations) */
/* (functions) */

/* Number of chi-square statistics. */
#define N_CHISQ 5

/* Number of symmetric statistics. */
#define N_SYMMETRIC 9

/* Number of directional statistics. */
#define N_DIRECTIONAL 13

/* A single table entry for general mode. */
struct table_entry
  {
    struct hmap_node node;      /* Entry in hash table. */
    double freq;                /* Frequency count. */
    union value values[1];	/* Values. */
  };

static size_t
table_entry_size (size_t n_values)
{
  return (offsetof (struct table_entry, values)
          + n_values * sizeof (union value));
}

/* Indexes into the 'vars' member of struct pivot_table and
   struct crosstab member. */
enum
  {
    ROW_VAR = 0,                /* Row variable. */
    COL_VAR = 1                 /* Column variable. */
    /* Higher indexes cause multiple tables to be output. */
  };

/* A crosstabulation of 2 or more variables. */
struct pivot_table
  {
    struct crosstabs_proc *proc;
    struct fmt_spec weight_format; /* Format for weight variable. */
    double missing;             /* Weight of missing cases. */

    /* Variables (2 or more). */
    int n_vars;
    const struct variable **vars;

    /* Constants (0 or more). */
    int n_consts;
    const struct variable **const_vars;
    union value *const_values;

    /* Data. */
    struct hmap data;
    struct table_entry **entries;
    size_t n_entries;

    /* Column values, number of columns. */
    union value *cols;
    int n_cols;

    /* Row values, number of rows. */
    union value *rows;
    int n_rows;

    /* Number of statistically interesting columns/rows
       (columns/rows with data in them). */
    int ns_cols, ns_rows;

    /* Matrix contents. */
    double *mat;		/* Matrix proper. */
    double *row_tot;		/* Row totals. */
    double *col_tot;		/* Column totals. */
    double total;		/* Grand total. */
  };

/* Integer mode variable info. */
struct var_range
  {
    struct hmap_node hmap_node; /* In struct crosstabs_proc var_ranges map. */
    const struct variable *var; /* The variable. */
    int min;			/* Minimum value. */
    int max;			/* Maximum value + 1. */
    int count;			/* max - min. */
  };

struct crosstabs_proc
  {
    const struct dictionary *dict;
    enum { INTEGER, GENERAL } mode;
    enum mv_class exclude;
    bool pivot;
    bool bad_warn;
    struct fmt_spec weight_format;

    /* Variables specifies on VARIABLES. */
    const struct variable **variables;
    size_t n_variables;
    struct hmap var_ranges;

    /* TABLES. */
    struct pivot_table *pivots;
    int n_pivots;

    /* CELLS. */
    int n_cells;		/* Number of cells requested. */
    unsigned int cells;         /* Bit k is 1 if cell k is requested. */
    int a_cells[CRS_CL_count];  /* 0...n_cells-1 are the requested cells. */

    /* STATISTICS. */
    unsigned int statistics;    /* Bit k is 1 if statistic k is requested. */

    bool descending;            /* True if descending sort order is requested. */
  };

const struct var_range *get_var_range (const struct crosstabs_proc *,
                                       const struct variable *);

static bool should_tabulate_case (const struct pivot_table *,
                                  const struct ccase *, enum mv_class exclude);
static void tabulate_general_case (struct pivot_table *, const struct ccase *,
                                   double weight);
static void tabulate_integer_case (struct pivot_table *, const struct ccase *,
                                   double weight);
static void postcalc (struct crosstabs_proc *);
static void submit (struct pivot_table *, struct tab_table *);

/* Parses and executes the CROSSTABS procedure. */
int
cmd_crosstabs (struct lexer *lexer, struct dataset *ds)
{
  const struct variable *wv = dict_get_weight (dataset_dict (ds));
  struct var_range *range, *next_range;
  struct crosstabs_proc proc;
  struct casegrouper *grouper;
  struct casereader *input, *group;
  struct cmd_crosstabs cmd;
  struct pivot_table *pt;
  int result;
  bool ok;
  int i;

  proc.dict = dataset_dict (ds);
  proc.bad_warn = true;
  proc.variables = NULL;
  proc.n_variables = 0;
  hmap_init (&proc.var_ranges);
  proc.pivots = NULL;
  proc.n_pivots = 0;
  proc.descending = false;
  proc.weight_format = wv ? *var_get_print_format (wv) : F_8_0;

  if (!parse_crosstabs (lexer, ds, &cmd, &proc))
    {
      result = CMD_FAILURE;
      goto exit;
    }

  proc.mode = proc.n_variables ? INTEGER : GENERAL;


  proc.descending = cmd.val == CRS_DVALUE;

  /* CELLS. */
  if (!cmd.sbc_cells)
    proc.cells = 1u << CRS_CL_COUNT;
  else if (cmd.a_cells[CRS_CL_ALL])
    proc.cells = UINT_MAX;
  else
    {
      proc.cells = 0;
      for (i = 0; i < CRS_CL_count; i++)
	if (cmd.a_cells[i])
	  proc.cells |= 1u << i;
      if (proc.cells == 0)
        proc.cells = ((1u << CRS_CL_COUNT)
                       | (1u << CRS_CL_ROW)
                       | (1u << CRS_CL_COLUMN)
                       | (1u << CRS_CL_TOTAL));
    }
  proc.cells &= ((1u << CRS_CL_count) - 1);
  proc.cells &= ~((1u << CRS_CL_NONE) | (1u << CRS_CL_ALL));
  proc.n_cells = 0;
  for (i = 0; i < CRS_CL_count; i++)
    if (proc.cells & (1u << i))
      proc.a_cells[proc.n_cells++] = i;

  /* STATISTICS. */
  if (cmd.a_statistics[CRS_ST_ALL])
    proc.statistics = UINT_MAX;
  else if (cmd.sbc_statistics)
    {
      int i;

      proc.statistics = 0;
      for (i = 0; i < CRS_ST_count; i++)
	if (cmd.a_statistics[i])
	  proc.statistics |= 1u << i;
      if (proc.statistics == 0)
        proc.statistics |= 1u << CRS_ST_CHISQ;
    }
  else
    proc.statistics = 0;

  /* MISSING. */
  proc.exclude = (cmd.miss == CRS_TABLE ? MV_ANY
                   : cmd.miss == CRS_INCLUDE ? MV_SYSTEM
                   : MV_NEVER);
  if (proc.mode == GENERAL && proc.exclude == MV_NEVER)
    {
      msg (SE, _("Missing mode %s not allowed in general mode.  "
		 "Assuming %s."), "REPORT", "MISSING=TABLE");
      proc.exclude = MV_ANY;
    }

  /* PIVOT. */
  proc.pivot = cmd.pivot == CRS_PIVOT;

  input = casereader_create_filter_weight (proc_open (ds), dataset_dict (ds),
                                           NULL, NULL);
  grouper = casegrouper_create_splits (input, dataset_dict (ds));
  while (casegrouper_get_next_group (grouper, &group))
    {
      struct ccase *c;

      /* Output SPLIT FILE variables. */
      c = casereader_peek (group, 0);
      if (c != NULL)
        {
          output_split_file_values (ds, c);
          case_unref (c);
        }

      /* Initialize hash tables. */
      for (pt = &proc.pivots[0]; pt < &proc.pivots[proc.n_pivots]; pt++)
        hmap_init (&pt->data);

      /* Tabulate. */
      for (; (c = casereader_read (group)) != NULL; case_unref (c))
        for (pt = &proc.pivots[0]; pt < &proc.pivots[proc.n_pivots]; pt++)
          {
            double weight = dict_get_case_weight (dataset_dict (ds), c,
                                                  &proc.bad_warn);
            if (should_tabulate_case (pt, c, proc.exclude))
              {
                if (proc.mode == GENERAL)
                  tabulate_general_case (pt, c, weight);
                else
                  tabulate_integer_case (pt, c, weight);
              }
            else
              pt->missing += weight;
          }
      casereader_destroy (group);

      /* Output. */
      postcalc (&proc);
    }
  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  result = ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;

exit:
  free (proc.variables);
  HMAP_FOR_EACH_SAFE (range, next_range, struct var_range, hmap_node,
                      &proc.var_ranges)
    {
      hmap_delete (&proc.var_ranges, &range->hmap_node);
      free (range);
    }
  for (pt = &proc.pivots[0]; pt < &proc.pivots[proc.n_pivots]; pt++)
    {
      free (pt->vars);
      free (pt->const_vars);
      /* We must not call value_destroy on const_values because
         it is a wild pointer; it never pointed to anything owned
         by the pivot_table.

         The rest of the data was allocated and destroyed at a
         lower level already. */
    }
  free (proc.pivots);

  return result;
}

/* Parses the TABLES subcommand. */
static int
crs_custom_tables (struct lexer *lexer, struct dataset *ds,
                   struct cmd_crosstabs *cmd UNUSED, void *proc_)
{
  struct crosstabs_proc *proc = proc_;
  struct const_var_set *var_set;
  int n_by;
  const struct variable ***by = NULL;
  int *by_iter;
  size_t *by_nvar = NULL;
  size_t nx = 1;
  bool ok = false;
  int i;

  /* Ensure that this is a TABLES subcommand. */
  if (!lex_match_id (lexer, "TABLES")
      && (lex_token (lexer) != T_ID ||
	  dict_lookup_var (dataset_dict (ds), lex_tokcstr (lexer)) == NULL)
      && lex_token (lexer) != T_ALL)
    return 2;
  lex_match (lexer, T_EQUALS);

  if (proc->variables != NULL)
    var_set = const_var_set_create_from_array (proc->variables,
                                               proc->n_variables);
  else
    var_set = const_var_set_create_from_dict (dataset_dict (ds));
  assert (var_set != NULL);

  for (n_by = 0; ;)
    {
      by = xnrealloc (by, n_by + 1, sizeof *by);
      by_nvar = xnrealloc (by_nvar, n_by + 1, sizeof *by_nvar);
      if (!parse_const_var_set_vars (lexer, var_set, &by[n_by], &by_nvar[n_by],
                                     PV_NO_DUPLICATE | PV_NO_SCRATCH))
	goto done;
      if (xalloc_oversized (nx, by_nvar[n_by]))
        {
          msg (SE, _("Too many cross-tabulation variables or dimensions."));
          goto done;
        }
      nx *= by_nvar[n_by];
      n_by++;

      if (!lex_match (lexer, T_BY))
	{
	  if (n_by < 2)
	    {
              lex_force_match (lexer, T_BY);
	      goto done;
	    }
	  else
	    break;
	}
    }

  by_iter = xcalloc (n_by, sizeof *by_iter);
  proc->pivots = xnrealloc (proc->pivots,
                            proc->n_pivots + nx, sizeof *proc->pivots);
  for (i = 0; i < nx; i++)
    {
      struct pivot_table *pt = &proc->pivots[proc->n_pivots++];
      int j;

      pt->proc = proc;
      pt->weight_format = proc->weight_format;
      pt->missing = 0.;
      pt->n_vars = n_by;
      pt->vars = xmalloc (n_by * sizeof *pt->vars);
      pt->n_consts = 0;
      pt->const_vars = NULL;
      pt->const_values = NULL;

      for (j = 0; j < n_by; j++)
        pt->vars[j] = by[j][by_iter[j]];

      for (j = n_by - 1; j >= 0; j--)
        {
          if (++by_iter[j] < by_nvar[j])
            break;
          by_iter[j] = 0;
        }
    }
  free (by_iter);
  ok = true;

done:
  /* All return paths lead here. */
  for (i = 0; i < n_by; i++)
    free (by[i]);
  free (by);
  free (by_nvar);

  const_var_set_destroy (var_set);

  return ok;
}

/* Parses the VARIABLES subcommand. */
static int
crs_custom_variables (struct lexer *lexer, struct dataset *ds,
                      struct cmd_crosstabs *cmd UNUSED, void *proc_)
{
  struct crosstabs_proc *proc = proc_;
  if (proc->n_pivots)
    {
      msg (SE, _("%s must be specified before %s."), "VARIABLES", "TABLES");
      return 0;
    }

  lex_match (lexer, T_EQUALS);

  for (;;)
    {
      size_t orig_nv = proc->n_variables;
      size_t i;

      long min, max;

      if (!parse_variables_const (lexer, dataset_dict (ds),
                                  &proc->variables, &proc->n_variables,
                                  (PV_APPEND | PV_NUMERIC
                                   | PV_NO_DUPLICATE | PV_NO_SCRATCH)))
	return 0;

      if (!lex_force_match (lexer, T_LPAREN))
	  goto lossage;

      if (!lex_force_int (lexer))
	goto lossage;
      min = lex_integer (lexer);
      lex_get (lexer);

      lex_match (lexer, T_COMMA);

      if (!lex_force_int (lexer))
	goto lossage;
      max = lex_integer (lexer);
      if (max < min)
	{
	  msg (SE, _("Maximum value (%ld) less than minimum value (%ld)."),
	       max, min);
	  goto lossage;
	}
      lex_get (lexer);

      if (!lex_force_match (lexer, T_RPAREN))
        goto lossage;

      for (i = orig_nv; i < proc->n_variables; i++)
        {
          const struct variable *var = proc->variables[i];
          struct var_range *vr = xmalloc (sizeof *vr);

          vr->var = var;
          vr->min = min;
	  vr->max = max + 1.;
	  vr->count = max - min + 1;
          hmap_insert (&proc->var_ranges, &vr->hmap_node,
                       hash_pointer (var, 0));
	}

      if (lex_token (lexer) == T_SLASH)
	break;
    }

  return 1;

 lossage:
  free (proc->variables);
  proc->variables = NULL;
  proc->n_variables = 0;
  return 0;
}

/* Data file processing. */

const struct var_range *
get_var_range (const struct crosstabs_proc *proc, const struct variable *var)
{
  if (!hmap_is_empty (&proc->var_ranges))
    {
      const struct var_range *range;

      HMAP_FOR_EACH_IN_BUCKET (range, struct var_range, hmap_node,
                               hash_pointer (var, 0), &proc->var_ranges)
        if (range->var == var)
          return range;
    }

  return NULL;
}

static bool
should_tabulate_case (const struct pivot_table *pt, const struct ccase *c,
                      enum mv_class exclude)
{
  int j;
  for (j = 0; j < pt->n_vars; j++)
    {
      const struct variable *var = pt->vars[j];
      const struct var_range *range = get_var_range (pt->proc, var);

      if (var_is_value_missing (var, case_data (c, var), exclude))
        return false;

      if (range != NULL)
        {
          double num = case_num (c, var);
          if (num < range->min || num > range->max)
            return false;
        }
    }
  return true;
}

static void
tabulate_integer_case (struct pivot_table *pt, const struct ccase *c,
                       double weight)
{
  struct table_entry *te;
  size_t hash;
  int j;

  hash = 0;
  for (j = 0; j < pt->n_vars; j++)
    {
      /* Throw away fractional parts of values. */
      hash = hash_int (case_num (c, pt->vars[j]), hash);
    }

  HMAP_FOR_EACH_WITH_HASH (te, struct table_entry, node, hash, &pt->data)
    {
      for (j = 0; j < pt->n_vars; j++)
        if ((int) case_num (c, pt->vars[j]) != (int) te->values[j].f)
          goto no_match;

      /* Found an existing entry. */
      te->freq += weight;
      return;

    no_match: ;
    }

  /* No existing entry.  Create a new one. */
  te = xmalloc (table_entry_size (pt->n_vars));
  te->freq = weight;
  for (j = 0; j < pt->n_vars; j++)
    te->values[j].f = (int) case_num (c, pt->vars[j]);
  hmap_insert (&pt->data, &te->node, hash);
}

static void
tabulate_general_case (struct pivot_table *pt, const struct ccase *c,
                       double weight)
{
  struct table_entry *te;
  size_t hash;
  int j;

  hash = 0;
  for (j = 0; j < pt->n_vars; j++)
    {
      const struct variable *var = pt->vars[j];
      hash = value_hash (case_data (c, var), var_get_width (var), hash);
    }

  HMAP_FOR_EACH_WITH_HASH (te, struct table_entry, node, hash, &pt->data)
    {
      for (j = 0; j < pt->n_vars; j++)
        {
          const struct variable *var = pt->vars[j];
          if (!value_equal (case_data (c, var), &te->values[j],
                            var_get_width (var)))
            goto no_match;
        }

      /* Found an existing entry. */
      te->freq += weight;
      return;

    no_match: ;
    }

  /* No existing entry.  Create a new one. */
  te = xmalloc (table_entry_size (pt->n_vars));
  te->freq = weight;
  for (j = 0; j < pt->n_vars; j++)
    {
      const struct variable *var = pt->vars[j];
      value_clone (&te->values[j], case_data (c, var), var_get_width (var));
    }
  hmap_insert (&pt->data, &te->node, hash);
}

/* Post-data reading calculations. */

static int compare_table_entry_vars_3way (const struct table_entry *a,
                                          const struct table_entry *b,
                                          const struct pivot_table *pt,
                                          int idx0, int idx1);
static int compare_table_entry_3way (const void *ap_, const void *bp_,
                                     const void *pt_);
static int compare_table_entry_3way_inv (const void *ap_, const void *bp_,
                                     const void *pt_);

static void enum_var_values (const struct pivot_table *, int var_idx,
                             union value **valuesp, int *n_values, bool descending);
static void output_pivot_table (struct crosstabs_proc *,
                                struct pivot_table *);
static void make_pivot_table_subset (struct pivot_table *pt,
                                     size_t row0, size_t row1,
                                     struct pivot_table *subset);
static void make_summary_table (struct crosstabs_proc *);
static bool find_crosstab (struct pivot_table *, size_t *row0p, size_t *row1p);

static void
postcalc (struct crosstabs_proc *proc)
{
  struct pivot_table *pt;

  /* Convert hash tables into sorted arrays of entries. */
  for (pt = &proc->pivots[0]; pt < &proc->pivots[proc->n_pivots]; pt++)
    {
      struct table_entry *e;
      size_t i;

      pt->n_entries = hmap_count (&pt->data);
      pt->entries = xnmalloc (pt->n_entries, sizeof *pt->entries);
      i = 0;
      HMAP_FOR_EACH (e, struct table_entry, node, &pt->data)
        pt->entries[i++] = e;
      hmap_destroy (&pt->data);

      sort (pt->entries, pt->n_entries, sizeof *pt->entries,
            proc->descending ? compare_table_entry_3way_inv : compare_table_entry_3way,
	    pt);
    }

  make_summary_table (proc);

  /* Output each pivot table. */
  for (pt = &proc->pivots[0]; pt < &proc->pivots[proc->n_pivots]; pt++)
    {
      if (proc->pivot || pt->n_vars == 2)
        output_pivot_table (proc, pt);
      else
        {
          size_t row0 = 0, row1 = 0;
          while (find_crosstab (pt, &row0, &row1))
            {
              struct pivot_table subset;
              make_pivot_table_subset (pt, row0, row1, &subset);
              output_pivot_table (proc, &subset);
            }
        }
    }

  /* Free output and prepare for next split file. */
  for (pt = &proc->pivots[0]; pt < &proc->pivots[proc->n_pivots]; pt++)
    {
      size_t i;

      pt->missing = 0.0;

      /* Free the members that were allocated in this function(and the values
         owned by the entries.

         The other pointer members are either both allocated and destroyed at a
         lower level (in output_pivot_table), or both allocated and destroyed
         at a higher level (in crs_custom_tables and free_proc,
         respectively). */
      for (i = 0; i < pt->n_vars; i++)
        {
          int width = var_get_width (pt->vars[i]);
          if (value_needs_init (width))
            {
              size_t j;

              for (j = 0; j < pt->n_entries; j++)
                value_destroy (&pt->entries[j]->values[i], width);
            }
        }

      for (i = 0; i < pt->n_entries; i++)
        free (pt->entries[i]);
      free (pt->entries);
    }
}

static void
make_pivot_table_subset (struct pivot_table *pt, size_t row0, size_t row1,
                         struct pivot_table *subset)
{
  *subset = *pt;
  if (pt->n_vars > 2)
    {
      assert (pt->n_consts == 0);
      subset->missing = pt->missing;
      subset->n_vars = 2;
      subset->vars = pt->vars;
      subset->n_consts = pt->n_vars - 2;
      subset->const_vars = pt->vars + 2;
      subset->const_values = &pt->entries[row0]->values[2];
    }
  subset->entries = &pt->entries[row0];
  subset->n_entries = row1 - row0;
}

static int
compare_table_entry_var_3way (const struct table_entry *a,
                              const struct table_entry *b,
                              const struct pivot_table *pt,
                              int idx)
{
  return value_compare_3way (&a->values[idx], &b->values[idx],
                             var_get_width (pt->vars[idx]));
}

static int
compare_table_entry_vars_3way (const struct table_entry *a,
                               const struct table_entry *b,
                               const struct pivot_table *pt,
                               int idx0, int idx1)
{
  int i;

  for (i = idx1 - 1; i >= idx0; i--)
    {
      int cmp = compare_table_entry_var_3way (a, b, pt, i);
      if (cmp != 0)
        return cmp;
    }
  return 0;
}

/* Compare the struct table_entry at *AP to the one at *BP and
   return a strcmp()-type result. */
static int
compare_table_entry_3way (const void *ap_, const void *bp_, const void *pt_)
{
  const struct table_entry *const *ap = ap_;
  const struct table_entry *const *bp = bp_;
  const struct table_entry *a = *ap;
  const struct table_entry *b = *bp;
  const struct pivot_table *pt = pt_;
  int cmp;

  cmp = compare_table_entry_vars_3way (a, b, pt, 2, pt->n_vars);
  if (cmp != 0)
    return cmp;

  cmp = compare_table_entry_var_3way (a, b, pt, ROW_VAR);
  if (cmp != 0)
    return cmp;

  return compare_table_entry_var_3way (a, b, pt, COL_VAR);
}

/* Inverted version of compare_table_entry_3way */
static int
compare_table_entry_3way_inv (const void *ap_, const void *bp_, const void *pt_)
{
  return -compare_table_entry_3way (ap_, bp_, pt_);
}

static int
find_first_difference (const struct pivot_table *pt, size_t row)
{
  if (row == 0)
    return pt->n_vars - 1;
  else
    {
      const struct table_entry *a = pt->entries[row];
      const struct table_entry *b = pt->entries[row - 1];
      int col;

      for (col = pt->n_vars - 1; col >= 0; col--)
        if (compare_table_entry_var_3way (a, b, pt, col))
          return col;
      NOT_REACHED ();
    }
}

/* Output a table summarizing the cases processed. */
static void
make_summary_table (struct crosstabs_proc *proc)
{
  struct tab_table *summary;
  struct pivot_table *pt;
  struct string name;
  int i;

  summary = tab_create (7, 3 + proc->n_pivots);
  tab_title (summary, _("Summary."));
  tab_headers (summary, 1, 0, 3, 0);
  tab_joint_text (summary, 1, 0, 6, 0, TAB_CENTER, _("Cases"));
  tab_joint_text (summary, 1, 1, 2, 1, TAB_CENTER, _("Valid"));
  tab_joint_text (summary, 3, 1, 4, 1, TAB_CENTER, _("Missing"));
  tab_joint_text (summary, 5, 1, 6, 1, TAB_CENTER, _("Total"));
  tab_hline (summary, TAL_1, 1, 6, 1);
  tab_hline (summary, TAL_1, 1, 6, 2);
  tab_vline (summary, TAL_1, 3, 1, 1);
  tab_vline (summary, TAL_1, 5, 1, 1);
  for (i = 0; i < 3; i++)
    {
      tab_text (summary, 1 + i * 2, 2, TAB_RIGHT, _("N"));
      tab_text (summary, 2 + i * 2, 2, TAB_RIGHT, _("Percent"));
    }
  tab_offset (summary, 0, 3);

  ds_init_empty (&name);
  for (pt = &proc->pivots[0]; pt < &proc->pivots[proc->n_pivots]; pt++)
    {
      double valid;
      double n[3];
      size_t i;

      tab_hline (summary, TAL_1, 0, 6, 0);

      ds_clear (&name);
      for (i = 0; i < pt->n_vars; i++)
        {
          if (i > 0)
            ds_put_cstr (&name, " * ");
          ds_put_cstr (&name, var_to_string (pt->vars[i]));
        }
      tab_text (summary, 0, 0, TAB_LEFT, ds_cstr (&name));

      valid = 0.;
      for (i = 0; i < pt->n_entries; i++)
        valid += pt->entries[i]->freq;

      n[0] = valid;
      n[1] = pt->missing;
      n[2] = n[0] + n[1];
      for (i = 0; i < 3; i++)
        {
          tab_double (summary, i * 2 + 1, 0, TAB_RIGHT, n[i],
                      &proc->weight_format);
          tab_text_format (summary, i * 2 + 2, 0, TAB_RIGHT, "%.1f%%",
                           n[i] / n[2] * 100.);
        }

      tab_next_row (summary);
    }
  ds_destroy (&name);

  submit (NULL, summary);
}

/* Output. */

static struct tab_table *create_crosstab_table (struct crosstabs_proc *,
                                                struct pivot_table *);
static struct tab_table *create_chisq_table (struct pivot_table *);
static struct tab_table *create_sym_table (struct pivot_table *);
static struct tab_table *create_risk_table (struct pivot_table *);
static struct tab_table *create_direct_table (struct pivot_table *);
static void display_dimensions (struct crosstabs_proc *, struct pivot_table *,
                                struct tab_table *, int first_difference);
static void display_crosstabulation (struct crosstabs_proc *,
                                     struct pivot_table *,
                                     struct tab_table *);
static void display_chisq (struct pivot_table *, struct tab_table *,
                           bool *showed_fisher);
static void display_symmetric (struct crosstabs_proc *, struct pivot_table *,
                               struct tab_table *);
static void display_risk (struct pivot_table *, struct tab_table *);
static void display_directional (struct crosstabs_proc *, struct pivot_table *,
                                 struct tab_table *);
static void table_value_missing (struct crosstabs_proc *proc,
                                 struct tab_table *table, int c, int r,
				 unsigned char opt, const union value *v,
				 const struct variable *var);
static void delete_missing (struct pivot_table *);
static void build_matrix (struct pivot_table *);

/* Output pivot table PT in the context of PROC. */
static void
output_pivot_table (struct crosstabs_proc *proc, struct pivot_table *pt)
{
  struct tab_table *table = NULL; /* Crosstabulation table. */
  struct tab_table *chisq = NULL; /* Chi-square table. */
  bool showed_fisher = false;
  struct tab_table *sym = NULL;   /* Symmetric measures table. */
  struct tab_table *risk = NULL;  /* Risk estimate table. */
  struct tab_table *direct = NULL; /* Directional measures table. */
  size_t row0, row1;

  enum_var_values (pt, COL_VAR, &pt->cols, &pt->n_cols, proc->descending);

  if (pt->n_cols == 0)
    {
      struct string vars;
      int i;

      ds_init_cstr (&vars, var_to_string (pt->vars[0]));
      for (i = 1; i < pt->n_vars; i++)
        ds_put_format (&vars, " * %s", var_to_string (pt->vars[i]));

      /* TRANSLATORS: The %s here describes a crosstabulation.  It takes the
         form "var1 * var2 * var3 * ...".  */
      msg (SW, _("Crosstabulation %s contained no non-missing cases."),
           ds_cstr (&vars));

      ds_destroy (&vars);
      free (pt->cols);
      return;
    }

  if (proc->cells)
    table = create_crosstab_table (proc, pt);
  if (proc->statistics & (1u << CRS_ST_CHISQ))
    chisq = create_chisq_table (pt);
  if (proc->statistics & ((1u << CRS_ST_PHI) | (1u << CRS_ST_CC)
                          | (1u << CRS_ST_BTAU) | (1u << CRS_ST_CTAU)
                          | (1u << CRS_ST_GAMMA) | (1u << CRS_ST_CORR)
                          | (1u << CRS_ST_KAPPA)))
    sym = create_sym_table (pt);
  if (proc->statistics & (1u << CRS_ST_RISK))
    risk = create_risk_table (pt);
  if (proc->statistics & ((1u << CRS_ST_LAMBDA) | (1u << CRS_ST_UC)
                          | (1u << CRS_ST_D) | (1u << CRS_ST_ETA)))
    direct = create_direct_table (pt);

  row0 = row1 = 0;
  while (find_crosstab (pt, &row0, &row1))
    {
      struct pivot_table x;
      int first_difference;

      make_pivot_table_subset (pt, row0, row1, &x);

      /* Find all the row variable values. */
      enum_var_values (&x, ROW_VAR, &x.rows, &x.n_rows, proc->descending);

      if (size_overflow_p (xtimes (xtimes (x.n_rows, x.n_cols),
                                   sizeof (double))))
        xalloc_die ();
      x.row_tot = xmalloc (x.n_rows * sizeof *x.row_tot);
      x.col_tot = xmalloc (x.n_cols * sizeof *x.col_tot);
      x.mat = xmalloc (x.n_rows * x.n_cols * sizeof *x.mat);

      /* Allocate table space for the matrix. */
      if (table
          && tab_row (table) + (x.n_rows + 1) * proc->n_cells > tab_nr (table))
	tab_realloc (table, -1,
		     MAX (tab_nr (table) + (x.n_rows + 1) * proc->n_cells,
			  tab_nr (table) * pt->n_entries / x.n_entries));

      build_matrix (&x);

      /* Find the first variable that differs from the last subtable. */
      first_difference = find_first_difference (pt, row0);
      if (table)
        {
          display_dimensions (proc, &x, table, first_difference);
          display_crosstabulation (proc, &x, table);
        }

      if (proc->exclude == MV_NEVER)
	delete_missing (&x);

      if (chisq)
        {
          display_dimensions (proc, &x, chisq, first_difference);
          display_chisq (&x, chisq, &showed_fisher);
        }
      if (sym)
        {
          display_dimensions (proc, &x, sym, first_difference);
          display_symmetric (proc, &x, sym);
        }
      if (risk)
        {
          display_dimensions (proc, &x, risk, first_difference);
          display_risk (&x, risk);
        }
      if (direct)
        {
          display_dimensions (proc, &x, direct, first_difference);
          display_directional (proc, &x, direct);
        }

      /* Free the parts of x that are not owned by pt.  In
         particular we must not free x.cols, which is the same as
         pt->cols, which is freed at the end of this function. */
      free (x.rows);

      free (x.mat);
      free (x.row_tot);
      free (x.col_tot);
    }

  submit (NULL, table);

  if (chisq)
    {
      if (!showed_fisher)
	tab_resize (chisq, 4 + (pt->n_vars - 2), -1);
      submit (pt, chisq);
    }

  submit (pt, sym);
  submit (pt, risk);
  submit (pt, direct);

  free (pt->cols);
}

static void
build_matrix (struct pivot_table *x)
{
  const int col_var_width = var_get_width (x->vars[COL_VAR]);
  const int row_var_width = var_get_width (x->vars[ROW_VAR]);
  int col, row;
  double *mp;
  struct table_entry **p;

  mp = x->mat;
  col = row = 0;
  for (p = x->entries; p < &x->entries[x->n_entries]; p++)
    {
      const struct table_entry *te = *p;

      while (!value_equal (&x->rows[row], &te->values[ROW_VAR], row_var_width))
        {
          for (; col < x->n_cols; col++)
            *mp++ = 0.0;
          col = 0;
          row++;
        }

      while (!value_equal (&x->cols[col], &te->values[COL_VAR], col_var_width))
        {
          *mp++ = 0.0;
          col++;
        }

      *mp++ = te->freq;
      if (++col >= x->n_cols)
        {
          col = 0;
          row++;
        }
    }
  while (mp < &x->mat[x->n_cols * x->n_rows])
    *mp++ = 0.0;
  assert (mp == &x->mat[x->n_cols * x->n_rows]);

  /* Column totals, row totals, ns_rows. */
  mp = x->mat;
  for (col = 0; col < x->n_cols; col++)
    x->col_tot[col] = 0.0;
  for (row = 0; row < x->n_rows; row++)
    x->row_tot[row] = 0.0;
  x->ns_rows = 0;
  for (row = 0; row < x->n_rows; row++)
    {
      bool row_is_empty = true;
      for (col = 0; col < x->n_cols; col++)
        {
          if (*mp != 0.0)
            {
              row_is_empty = false;
              x->col_tot[col] += *mp;
              x->row_tot[row] += *mp;
            }
          mp++;
        }
      if (!row_is_empty)
        x->ns_rows++;
    }
  assert (mp == &x->mat[x->n_cols * x->n_rows]);

  /* ns_cols. */
  x->ns_cols = 0;
  for (col = 0; col < x->n_cols; col++)
    for (row = 0; row < x->n_rows; row++)
      if (x->mat[col + row * x->n_cols] != 0.0)
        {
          x->ns_cols++;
          break;
        }

  /* Grand total. */
  x->total = 0.0;
  for (col = 0; col < x->n_cols; col++)
    x->total += x->col_tot[col];
}

static struct tab_table *
create_crosstab_table (struct crosstabs_proc *proc, struct pivot_table *pt)
{
  struct tuple
    {
      int value;
      const char *name;
    };
  static const struct tuple names[] =
    {
      {CRS_CL_COUNT, N_("count")},
      {CRS_CL_ROW, N_("row %")},
      {CRS_CL_COLUMN, N_("column %")},
      {CRS_CL_TOTAL, N_("total %")},
      {CRS_CL_EXPECTED, N_("expected")},
      {CRS_CL_RESIDUAL, N_("residual")},
      {CRS_CL_SRESIDUAL, N_("std. resid.")},
      {CRS_CL_ASRESIDUAL, N_("adj. resid.")},
    };
  const int n_names = sizeof names / sizeof *names;
  const struct tuple *t;

  struct tab_table *table;
  struct string title;
  struct pivot_table x;

  int i;

  make_pivot_table_subset (pt, 0, 0, &x);

  table = tab_create (x.n_consts + 1 + x.n_cols + 1,
                      (x.n_entries / x.n_cols) * 3 / 2 * proc->n_cells + 10);
  tab_headers (table, x.n_consts + 1, 0, 2, 0);

  /* First header line. */
  tab_joint_text (table, x.n_consts + 1, 0,
                  (x.n_consts + 1) + (x.n_cols - 1), 0,
                  TAB_CENTER | TAT_TITLE, var_to_string (x.vars[COL_VAR]));

  tab_hline (table, TAL_1, x.n_consts + 1,
             x.n_consts + 2 + x.n_cols - 2, 1);

  /* Second header line. */
  for (i = 2; i < x.n_consts + 2; i++)
    tab_joint_text (table, x.n_consts + 2 - i - 1, 0,
                    x.n_consts + 2 - i - 1, 1,
                    TAB_RIGHT | TAT_TITLE, var_to_string (x.vars[i]));
  tab_text (table, x.n_consts + 2 - 2, 1, TAB_RIGHT | TAT_TITLE,
            var_to_string (x.vars[ROW_VAR]));
  for (i = 0; i < x.n_cols; i++)
    table_value_missing (proc, table, x.n_consts + 2 + i - 1, 1, TAB_RIGHT,
                         &x.cols[i], x.vars[COL_VAR]);
  tab_text (table, x.n_consts + 2 + x.n_cols - 1, 1, TAB_CENTER, _("Total"));

  tab_hline (table, TAL_1, 0, x.n_consts + 2 + x.n_cols - 1, 2);
  tab_vline (table, TAL_1, x.n_consts + 2 + x.n_cols - 1, 0, 1);

  /* Title. */
  ds_init_empty (&title);
  for (i = 0; i < x.n_consts + 2; i++)
    {
      if (i)
        ds_put_cstr (&title, " * ");
      ds_put_cstr (&title, var_to_string (x.vars[i]));
    }
  for (i = 0; i < pt->n_consts; i++)
    {
      const struct variable *var = pt->const_vars[i];
      char *s;

      ds_put_format (&title, ", %s=", var_to_string (var));

      /* Insert the formatted value of VAR without any leading spaces. */
      s = data_out (&pt->const_values[i], var_get_encoding (var),
                    var_get_print_format (var));
      ds_put_cstr (&title, s + strspn (s, " "));
      free (s);
    }

  ds_put_cstr (&title, " [");
  i = 0;
  for (t = names; t < &names[n_names]; t++)
    if (proc->cells & (1u << t->value))
      {
        if (i++)
          ds_put_cstr (&title, ", ");
        ds_put_cstr (&title, gettext (t->name));
      }
  ds_put_cstr (&title, "].");

  tab_title (table, "%s", ds_cstr (&title));
  ds_destroy (&title);

  tab_offset (table, 0, 2);
  return table;
}

static struct tab_table *
create_chisq_table (struct pivot_table *pt)
{
  struct tab_table *chisq;

  chisq = tab_create (6 + (pt->n_vars - 2),
                      pt->n_entries / pt->n_cols * 3 / 2 * N_CHISQ + 10);
  tab_headers (chisq, 1 + (pt->n_vars - 2), 0, 1, 0);

  tab_title (chisq, _("Chi-square tests."));

  tab_offset (chisq, pt->n_vars - 2, 0);
  tab_text (chisq, 0, 0, TAB_LEFT | TAT_TITLE, _("Statistic"));
  tab_text (chisq, 1, 0, TAB_RIGHT | TAT_TITLE, _("Value"));
  tab_text (chisq, 2, 0, TAB_RIGHT | TAT_TITLE, _("df"));
  tab_text (chisq, 3, 0, TAB_RIGHT | TAT_TITLE,
            _("Asymp. Sig. (2-tailed)"));
  tab_text_format (chisq, 4, 0, TAB_RIGHT | TAT_TITLE,
            _("Exact Sig. (%d-tailed)"), 2);
  tab_text_format (chisq, 5, 0, TAB_RIGHT | TAT_TITLE,
            _("Exact Sig. (%d-tailed)"), 1);
  tab_offset (chisq, 0, 1);

  return chisq;
}

/* Symmetric measures. */
static struct tab_table *
create_sym_table (struct pivot_table *pt)
{
  struct tab_table *sym;

  sym = tab_create (6 + (pt->n_vars - 2),
                    pt->n_entries / pt->n_cols * 7 + 10);
  tab_headers (sym, 2 + (pt->n_vars - 2), 0, 1, 0);
  tab_title (sym, _("Symmetric measures."));

  tab_offset (sym, pt->n_vars - 2, 0);
  tab_text (sym, 0, 0, TAB_LEFT | TAT_TITLE, _("Category"));
  tab_text (sym, 1, 0, TAB_LEFT | TAT_TITLE, _("Statistic"));
  tab_text (sym, 2, 0, TAB_RIGHT | TAT_TITLE, _("Value"));
  tab_text (sym, 3, 0, TAB_RIGHT | TAT_TITLE, _("Asymp. Std. Error"));
  tab_text (sym, 4, 0, TAB_RIGHT | TAT_TITLE, _("Approx. T"));
  tab_text (sym, 5, 0, TAB_RIGHT | TAT_TITLE, _("Approx. Sig."));
  tab_offset (sym, 0, 1);

  return sym;
}

/* Risk estimate. */
static struct tab_table *
create_risk_table (struct pivot_table *pt)
{
  struct tab_table *risk;

  risk = tab_create (4 + (pt->n_vars - 2), pt->n_entries / pt->n_cols * 4 + 10);
  tab_headers (risk, 1 + pt->n_vars - 2, 0, 2, 0);
  tab_title (risk, _("Risk estimate."));

  tab_offset (risk, pt->n_vars - 2, 0);
  tab_joint_text_format (risk, 2, 0, 3, 0, TAB_CENTER | TAT_TITLE,
                         _("95%% Confidence Interval"));
  tab_text (risk, 0, 1, TAB_LEFT | TAT_TITLE, _("Statistic"));
  tab_text (risk, 1, 1, TAB_RIGHT | TAT_TITLE, _("Value"));
  tab_text (risk, 2, 1, TAB_RIGHT | TAT_TITLE, _("Lower"));
  tab_text (risk, 3, 1, TAB_RIGHT | TAT_TITLE, _("Upper"));
  tab_hline (risk, TAL_1, 2, 3, 1);
  tab_vline (risk, TAL_1, 2, 0, 1);
  tab_offset (risk, 0, 2);

  return risk;
}

/* Directional measures. */
static struct tab_table *
create_direct_table (struct pivot_table *pt)
{
  struct tab_table *direct;

  direct = tab_create (7 + (pt->n_vars - 2),
                       pt->n_entries / pt->n_cols * 7 + 10);
  tab_headers (direct, 3 + (pt->n_vars - 2), 0, 1, 0);
  tab_title (direct, _("Directional measures."));

  tab_offset (direct, pt->n_vars - 2, 0);
  tab_text (direct, 0, 0, TAB_LEFT | TAT_TITLE, _("Category"));
  tab_text (direct, 1, 0, TAB_LEFT | TAT_TITLE, _("Statistic"));
  tab_text (direct, 2, 0, TAB_LEFT | TAT_TITLE, _("Type"));
  tab_text (direct, 3, 0, TAB_RIGHT | TAT_TITLE, _("Value"));
  tab_text (direct, 4, 0, TAB_RIGHT | TAT_TITLE, _("Asymp. Std. Error"));
  tab_text (direct, 5, 0, TAB_RIGHT | TAT_TITLE, _("Approx. T"));
  tab_text (direct, 6, 0, TAB_RIGHT | TAT_TITLE, _("Approx. Sig."));
  tab_offset (direct, 0, 1);

  return direct;
}


/* Delete missing rows and columns for statistical analysis when
   /MISSING=REPORT. */
static void
delete_missing (struct pivot_table *pt)
{
  int r, c;

  for (r = 0; r < pt->n_rows; r++)
    if (var_is_num_missing (pt->vars[ROW_VAR], pt->rows[r].f, MV_USER))
      {
        for (c = 0; c < pt->n_cols; c++)
          pt->mat[c + r * pt->n_cols] = 0.;
        pt->ns_rows--;
      }


  for (c = 0; c < pt->n_cols; c++)
    if (var_is_num_missing (pt->vars[COL_VAR], pt->cols[c].f, MV_USER))
      {
        for (r = 0; r < pt->n_rows; r++)
          pt->mat[c + r * pt->n_cols] = 0.;
        pt->ns_cols--;
      }
}

/* Prepare table T for submission, and submit it. */
static void
submit (struct pivot_table *pt, struct tab_table *t)
{
  int i;

  if (t == NULL)
    return;

  tab_resize (t, -1, 0);
  if (tab_nr (t) == tab_t (t))
    {
      table_unref (&t->table);
      return;
    }
  tab_offset (t, 0, 0);
  if (pt != NULL)
    for (i = 2; i < pt->n_vars; i++)
      tab_text (t, pt->n_vars - i - 1, 0, TAB_RIGHT | TAT_TITLE,
                var_to_string (pt->vars[i]));
  tab_box (t, TAL_2, TAL_2, -1, -1, 0, 0, tab_nc (t) - 1, tab_nr (t) - 1);
  tab_box (t, -1, -1, -1, TAL_1, tab_l (t), tab_t (t) - 1, tab_nc (t) - 1,
	   tab_nr (t) - 1);
  tab_box (t, -1, -1, -1, TAL_GAP, 0, tab_t (t), tab_l (t) - 1,
	   tab_nr (t) - 1);
  tab_vline (t, TAL_2, tab_l (t), 0, tab_nr (t) - 1);

  tab_submit (t);
}

static bool
find_crosstab (struct pivot_table *pt, size_t *row0p, size_t *row1p)
{
  size_t row0 = *row1p;
  size_t row1;

  if (row0 >= pt->n_entries)
    return false;

  for (row1 = row0 + 1; row1 < pt->n_entries; row1++)
    {
      struct table_entry *a = pt->entries[row0];
      struct table_entry *b = pt->entries[row1];
      if (compare_table_entry_vars_3way (a, b, pt, 2, pt->n_vars) != 0)
        break;
    }
  *row0p = row0;
  *row1p = row1;
  return true;
}

/* Compares `union value's A_ and B_ and returns a strcmp()-like
   result.  WIDTH_ points to an int which is either 0 for a
   numeric value or a string width for a string value. */
static int
compare_value_3way (const void *a_, const void *b_, const void *width_)
{
  const union value *a = a_;
  const union value *b = b_;
  const int *width = width_;

  return value_compare_3way (a, b, *width);
}

/* Inverted version of the above */
static int
compare_value_3way_inv (const void *a_, const void *b_, const void *width_)
{
  return -compare_value_3way (a_, b_, width_);
}


/* Given an array of ENTRY_CNT table_entry structures starting at
   ENTRIES, creates a sorted list of the values that the variable
   with index VAR_IDX takes on.  The values are returned as a
   malloc()'d array stored in *VALUES, with the number of values
   stored in *VALUE_CNT.

   The caller must eventually free *VALUES, but each pointer in *VALUES points
   to existing data not owned by *VALUES itself. */
static void
enum_var_values (const struct pivot_table *pt, int var_idx,
                 union value **valuesp, int *n_values, bool descending)
{
  const struct variable *var = pt->vars[var_idx];
  const struct var_range *range = get_var_range (pt->proc, var);
  union value *values;
  size_t i;

  if (range)
    {
      values = *valuesp = xnmalloc (range->count, sizeof *values);
      *n_values = range->count;
      for (i = 0; i < range->count; i++)
        values[i].f = range->min + i;
    }
  else
    {
      int width = var_get_width (var);
      struct hmapx_node *node;
      const union value *iter;
      struct hmapx set;

      hmapx_init (&set);
      for (i = 0; i < pt->n_entries; i++)
        {
          const struct table_entry *te = pt->entries[i];
          const union value *value = &te->values[var_idx];
          size_t hash = value_hash (value, width, 0);

          HMAPX_FOR_EACH_WITH_HASH (iter, node, hash, &set)
            if (value_equal (iter, value, width))
              goto next_entry;

          hmapx_insert (&set, (union value *) value, hash);

        next_entry: ;
        }

      *n_values = hmapx_count (&set);
      values = *valuesp = xnmalloc (*n_values, sizeof *values);
      i = 0;
      HMAPX_FOR_EACH (iter, node, &set)
        values[i++] = *iter;
      hmapx_destroy (&set);

      sort (values, *n_values, sizeof *values,
	    descending ? compare_value_3way_inv : compare_value_3way,
	    &width);
    }
}

/* Sets cell (C,R) in TABLE, with options OPT, to have a value taken
   from V, displayed with print format spec from variable VAR.  When
   in REPORT missing-value mode, missing values have an M appended. */
static void
table_value_missing (struct crosstabs_proc *proc,
                     struct tab_table *table, int c, int r, unsigned char opt,
		     const union value *v, const struct variable *var)
{
  const char *label = var_lookup_value_label (var, v);
  if (label != NULL)
    tab_text (table, c, r, TAB_LEFT, label);
  else
    {
      const struct fmt_spec *print = var_get_print_format (var);
      if (proc->exclude == MV_NEVER && var_is_value_missing (var, v, MV_USER))
        {
          char *s = data_out (v, dict_get_encoding (proc->dict), print);
          tab_text_format (table, c, r, opt, "%sM", s + strspn (s, " "));
          free (s);
        }
      else
        tab_value (table, c, r, opt, v, var, print);
    }
}

/* Draws a line across TABLE at the current row to indicate the most
   major dimension variable with index FIRST_DIFFERENCE out of N_VARS
   that changed, and puts the values that changed into the table.  TB
   and PT must be the corresponding table_entry and crosstab,
   respectively. */
static void
display_dimensions (struct crosstabs_proc *proc, struct pivot_table *pt,
                    struct tab_table *table, int first_difference)
{
  tab_hline (table, TAL_1, pt->n_consts + pt->n_vars - first_difference - 1, tab_nc (table) - 1, 0);

  for (; first_difference >= 2; first_difference--)
    table_value_missing (proc, table, pt->n_consts + pt->n_vars - first_difference - 1, 0,
			 TAB_RIGHT, &pt->entries[0]->values[first_difference],
			 pt->vars[first_difference]);
}

/* Put VALUE into cell (C,R) of TABLE, suffixed with character
   SUFFIX if nonzero.  If MARK_MISSING is true the entry is
   additionally suffixed with a letter `M'. */
static void
format_cell_entry (struct tab_table *table, int c, int r, double value,
                   char suffix, bool mark_missing, const struct dictionary *dict)
{
  union value v;
  char suffixes[3];
  int suffix_len;
  char *s;

  v.f = value;
  s = data_out (&v, dict_get_encoding (dict), settings_get_format ());

  suffix_len = 0;
  if (suffix != 0)
    suffixes[suffix_len++] = suffix;
  if (mark_missing)
    suffixes[suffix_len++] = 'M';
  suffixes[suffix_len] = '\0';

  tab_text_format (table, c, r, TAB_RIGHT, "%s%s",
                   s + strspn (s, " "), suffixes);

  free (s);
}

/* Displays the crosstabulation table. */
static void
display_crosstabulation (struct crosstabs_proc *proc, struct pivot_table *pt,
                         struct tab_table *table)
{
  int last_row;
  int r, c, i;
  double *mp;

  for (r = 0; r < pt->n_rows; r++)
    table_value_missing (proc, table, pt->n_consts + pt->n_vars - 2,
                         r * proc->n_cells, TAB_RIGHT, &pt->rows[r],
                         pt->vars[ROW_VAR]);

  tab_text (table, pt->n_vars - 2, pt->n_rows * proc->n_cells,
	    TAB_LEFT, _("Total"));

  /* Put in the actual cells. */
  mp = pt->mat;
  tab_offset (table, pt->n_consts + pt->n_vars - 1, -1);
  for (r = 0; r < pt->n_rows; r++)
    {
      if (proc->n_cells > 1)
        tab_hline (table, TAL_1, -1, pt->n_cols, 0);
      for (c = 0; c < pt->n_cols; c++)
        {
          bool mark_missing = false;
          double expected_value = pt->row_tot[r] * pt->col_tot[c] / pt->total;
          if (proc->exclude == MV_NEVER
              && (var_is_num_missing (pt->vars[COL_VAR], pt->cols[c].f, MV_USER)
                  || var_is_num_missing (pt->vars[ROW_VAR], pt->rows[r].f,
                                         MV_USER)))
            mark_missing = true;
          for (i = 0; i < proc->n_cells; i++)
            {
              double v;
              int suffix = 0;

              switch (proc->a_cells[i])
                {
                case CRS_CL_COUNT:
                  v = *mp;
                  break;
                case CRS_CL_ROW:
                  v = *mp / pt->row_tot[r] * 100.;
                  suffix = '%';
                  break;
                case CRS_CL_COLUMN:
                  v = *mp / pt->col_tot[c] * 100.;
                  suffix = '%';
                  break;
                case CRS_CL_TOTAL:
                  v = *mp / pt->total * 100.;
                  suffix = '%';
                  break;
                case CRS_CL_EXPECTED:
                  v = expected_value;
                  break;
                case CRS_CL_RESIDUAL:
                  v = *mp - expected_value;
                  break;
                case CRS_CL_SRESIDUAL:
                  v = (*mp - expected_value) / sqrt (expected_value);
                  break;
                case CRS_CL_ASRESIDUAL:
                  v = ((*mp - expected_value)
                       / sqrt (expected_value
                               * (1. - pt->row_tot[r] / pt->total)
                               * (1. - pt->col_tot[c] / pt->total)));
                  break;
                default:
                  NOT_REACHED ();
                }
              format_cell_entry (table, c, i, v, suffix, mark_missing, proc->dict);
            }

          mp++;
        }

      tab_offset (table, -1, tab_row (table) + proc->n_cells);
    }

  /* Row totals. */
  tab_offset (table, -1, tab_row (table) - proc->n_cells * pt->n_rows);
  for (r = 0; r < pt->n_rows; r++)
    {
      bool mark_missing = false;

      if (proc->exclude == MV_NEVER
          && var_is_num_missing (pt->vars[ROW_VAR], pt->rows[r].f, MV_USER))
        mark_missing = true;

      for (i = 0; i < proc->n_cells; i++)
        {
          char suffix = 0;
          double v;

          switch (proc->a_cells[i])
            {
            case CRS_CL_COUNT:
              v = pt->row_tot[r];
              break;
            case CRS_CL_ROW:
              v = 100.0;
              suffix = '%';
              break;
            case CRS_CL_COLUMN:
              v = pt->row_tot[r] / pt->total * 100.;
              suffix = '%';
              break;
            case CRS_CL_TOTAL:
              v = pt->row_tot[r] / pt->total * 100.;
              suffix = '%';
              break;
            case CRS_CL_EXPECTED:
            case CRS_CL_RESIDUAL:
            case CRS_CL_SRESIDUAL:
            case CRS_CL_ASRESIDUAL:
              v = 0.;
              break;
            default:
              NOT_REACHED ();
            }

          format_cell_entry (table, pt->n_cols, 0, v, suffix, mark_missing, proc->dict);
          tab_next_row (table);
        }
    }

  /* Column totals, grand total. */
  last_row = 0;
  if (proc->n_cells > 1)
    tab_hline (table, TAL_1, -1, pt->n_cols, 0);
  for (c = 0; c <= pt->n_cols; c++)
    {
      double ct = c < pt->n_cols ? pt->col_tot[c] : pt->total;
      bool mark_missing = false;
      int i;

      if (proc->exclude == MV_NEVER && c < pt->n_cols
          && var_is_num_missing (pt->vars[COL_VAR], pt->cols[c].f, MV_USER))
        mark_missing = true;

      for (i = 0; i < proc->n_cells; i++)
        {
          char suffix = 0;
          double v;

          switch (proc->a_cells[i])
            {
            case CRS_CL_COUNT:
              v = ct;
              break;
            case CRS_CL_ROW:
              v = ct / pt->total * 100.;
              suffix = '%';
              break;
            case CRS_CL_COLUMN:
              v = 100.;
              suffix = '%';
              break;
            case CRS_CL_TOTAL:
              v = ct / pt->total * 100.;
              suffix = '%';
              break;
            case CRS_CL_EXPECTED:
            case CRS_CL_RESIDUAL:
            case CRS_CL_SRESIDUAL:
            case CRS_CL_ASRESIDUAL:
              continue;
            default:
              NOT_REACHED ();
            }

          format_cell_entry (table, c, i, v, suffix, mark_missing, proc->dict);
        }
      last_row = i;
    }

  tab_offset (table, -1, tab_row (table) + last_row);
  tab_offset (table, 0, -1);
}

static void calc_r (struct pivot_table *,
                    double *PT, double *Y, double *, double *, double *);
static void calc_chisq (struct pivot_table *,
                        double[N_CHISQ], int[N_CHISQ], double *, double *);

/* Display chi-square statistics. */
static void
display_chisq (struct pivot_table *pt, struct tab_table *chisq,
               bool *showed_fisher)
{
  static const char *chisq_stats[N_CHISQ] =
    {
      N_("Pearson Chi-Square"),
      N_("Likelihood Ratio"),
      N_("Fisher's Exact Test"),
      N_("Continuity Correction"),
      N_("Linear-by-Linear Association"),
    };
  double chisq_v[N_CHISQ];
  double fisher1, fisher2;
  int df[N_CHISQ];

  int i;

  calc_chisq (pt, chisq_v, df, &fisher1, &fisher2);

  tab_offset (chisq, pt->n_consts + pt->n_vars - 2, -1);

  for (i = 0; i < N_CHISQ; i++)
    {
      if ((i != 2 && chisq_v[i] == SYSMIS)
	  || (i == 2 && fisher1 == SYSMIS))
	continue;

      tab_text (chisq, 0, 0, TAB_LEFT, gettext (chisq_stats[i]));
      if (i != 2)
	{
	  tab_double (chisq, 1, 0, TAB_RIGHT, chisq_v[i], NULL);
	  tab_double (chisq, 2, 0, TAB_RIGHT, df[i], &pt->weight_format);
	  tab_double (chisq, 3, 0, TAB_RIGHT,
		     gsl_cdf_chisq_Q (chisq_v[i], df[i]), NULL);
	}
      else
	{
	  *showed_fisher = true;
	  tab_double (chisq, 4, 0, TAB_RIGHT, fisher2, NULL);
	  tab_double (chisq, 5, 0, TAB_RIGHT, fisher1, NULL);
	}
      tab_next_row (chisq);
    }

  tab_text (chisq, 0, 0, TAB_LEFT, _("N of Valid Cases"));
  tab_double (chisq, 1, 0, TAB_RIGHT, pt->total, &pt->weight_format);
  tab_next_row (chisq);

  tab_offset (chisq, 0, -1);
}

static int calc_symmetric (struct crosstabs_proc *, struct pivot_table *,
                           double[N_SYMMETRIC], double[N_SYMMETRIC],
			   double[N_SYMMETRIC],
                           double[3], double[3], double[3]);

/* Display symmetric measures. */
static void
display_symmetric (struct crosstabs_proc *proc, struct pivot_table *pt,
                   struct tab_table *sym)
{
  static const char *categories[] =
    {
      N_("Nominal by Nominal"),
      N_("Ordinal by Ordinal"),
      N_("Interval by Interval"),
      N_("Measure of Agreement"),
    };

  static const char *stats[N_SYMMETRIC] =
    {
      N_("Phi"),
      N_("Cramer's V"),
      N_("Contingency Coefficient"),
      N_("Kendall's tau-b"),
      N_("Kendall's tau-c"),
      N_("Gamma"),
      N_("Spearman Correlation"),
      N_("Pearson's R"),
      N_("Kappa"),
    };

  static const int stats_categories[N_SYMMETRIC] =
    {
      0, 0, 0, 1, 1, 1, 1, 2, 3,
    };

  int last_cat = -1;
  double sym_v[N_SYMMETRIC], sym_ase[N_SYMMETRIC], sym_t[N_SYMMETRIC];
  double somers_d_v[3], somers_d_ase[3], somers_d_t[3];
  int i;

  if (!calc_symmetric (proc, pt, sym_v, sym_ase, sym_t,
                       somers_d_v, somers_d_ase, somers_d_t))
    return;

  tab_offset (sym, pt->n_consts + pt->n_vars - 2, -1);

  for (i = 0; i < N_SYMMETRIC; i++)
    {
      if (sym_v[i] == SYSMIS)
	continue;

      if (stats_categories[i] != last_cat)
	{
	  last_cat = stats_categories[i];
	  tab_text (sym, 0, 0, TAB_LEFT, gettext (categories[last_cat]));
	}

      tab_text (sym, 1, 0, TAB_LEFT, gettext (stats[i]));
      tab_double (sym, 2, 0, TAB_RIGHT, sym_v[i], NULL);
      if (sym_ase[i] != SYSMIS)
	tab_double (sym, 3, 0, TAB_RIGHT, sym_ase[i], NULL);
      if (sym_t[i] != SYSMIS)
	tab_double (sym, 4, 0, TAB_RIGHT, sym_t[i], NULL);
      /*tab_double (sym, 5, 0, TAB_RIGHT, normal_sig (sym_v[i]), NULL);*/
      tab_next_row (sym);
    }

  tab_text (sym, 0, 0, TAB_LEFT, _("N of Valid Cases"));
  tab_double (sym, 2, 0, TAB_RIGHT, pt->total, &pt->weight_format);
  tab_next_row (sym);

  tab_offset (sym, 0, -1);
}

static int calc_risk (struct pivot_table *,
                      double[], double[], double[], union value *);

/* Display risk estimate. */
static void
display_risk (struct pivot_table *pt, struct tab_table *risk)
{
  char buf[256];
  double risk_v[3], lower[3], upper[3];
  union value c[2];
  int i;

  if (!calc_risk (pt, risk_v, upper, lower, c))
    return;

  tab_offset (risk, pt->n_consts + pt->n_vars - 2, -1);

  for (i = 0; i < 3; i++)
    {
      const struct variable *cv = pt->vars[COL_VAR];
      const struct variable *rv = pt->vars[ROW_VAR];
      int cvw = var_get_width (cv);
      int rvw = var_get_width (rv);

      if (risk_v[i] == SYSMIS)
	continue;

      switch (i)
	{
	case 0:
	  if (var_is_numeric (cv))
	    sprintf (buf, _("Odds Ratio for %s (%g / %g)"),
		     var_to_string (cv), c[0].f, c[1].f);
	  else
	    sprintf (buf, _("Odds Ratio for %s (%.*s / %.*s)"),
		     var_to_string (cv),
		     cvw, value_str (&c[0], cvw),
		     cvw, value_str (&c[1], cvw));
	  break;
	case 1:
	case 2:
	  if (var_is_numeric (rv))
	    sprintf (buf, _("For cohort %s = %.*g"),
		     var_to_string (rv), DBL_DIG + 1, pt->rows[i - 1].f);
	  else
	    sprintf (buf, _("For cohort %s = %.*s"),
		     var_to_string (rv),
		     rvw, value_str (&pt->rows[i - 1], rvw));
	  break;
	}

      tab_text (risk, 0, 0, TAB_LEFT, buf);
      tab_double (risk, 1, 0, TAB_RIGHT, risk_v[i], NULL);
      tab_double (risk, 2, 0, TAB_RIGHT, lower[i], NULL);
      tab_double (risk, 3, 0, TAB_RIGHT, upper[i], NULL);
      tab_next_row (risk);
    }

  tab_text (risk, 0, 0, TAB_LEFT, _("N of Valid Cases"));
  tab_double (risk, 1, 0, TAB_RIGHT, pt->total, &pt->weight_format);
  tab_next_row (risk);

  tab_offset (risk, 0, -1);
}

static int calc_directional (struct crosstabs_proc *, struct pivot_table *,
                             double[N_DIRECTIONAL], double[N_DIRECTIONAL],
			     double[N_DIRECTIONAL]);

/* Display directional measures. */
static void
display_directional (struct crosstabs_proc *proc, struct pivot_table *pt,
                     struct tab_table *direct)
{
  static const char *categories[] =
    {
      N_("Nominal by Nominal"),
      N_("Ordinal by Ordinal"),
      N_("Nominal by Interval"),
    };

  static const char *stats[] =
    {
      N_("Lambda"),
      N_("Goodman and Kruskal tau"),
      N_("Uncertainty Coefficient"),
      N_("Somers' d"),
      N_("Eta"),
    };

  static const char *types[] =
    {
      N_("Symmetric"),
      N_("%s Dependent"),
      N_("%s Dependent"),
    };

  static const int stats_categories[N_DIRECTIONAL] =
    {
      0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2,
    };

  static const int stats_stats[N_DIRECTIONAL] =
    {
      0, 0, 0, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4,
    };

  static const int stats_types[N_DIRECTIONAL] =
    {
      0, 1, 2, 1, 2, 0, 1, 2, 0, 1, 2, 1, 2,
    };

  static const int *stats_lookup[] =
    {
      stats_categories,
      stats_stats,
      stats_types,
    };

  static const char **stats_names[] =
    {
      categories,
      stats,
      types,
    };

  int last[3] =
    {
      -1, -1, -1,
    };

  double direct_v[N_DIRECTIONAL];
  double direct_ase[N_DIRECTIONAL];
  double direct_t[N_DIRECTIONAL];

  int i;

  if (!calc_directional (proc, pt, direct_v, direct_ase, direct_t))
    return;

  tab_offset (direct, pt->n_consts + pt->n_vars - 2, -1);

  for (i = 0; i < N_DIRECTIONAL; i++)
    {
      if (direct_v[i] == SYSMIS)
	continue;

      {
	int j;

	for (j = 0; j < 3; j++)
	  if (last[j] != stats_lookup[j][i])
	    {
	      if (j < 2)
		tab_hline (direct, TAL_1, j, 6, 0);

	      for (; j < 3; j++)
		{
		  const char *string;
		  int k = last[j] = stats_lookup[j][i];

		  if (k == 0)
		    string = NULL;
		  else if (k == 1)
		    string = var_to_string (pt->vars[0]);
		  else
		    string = var_to_string (pt->vars[1]);

		  tab_text_format (direct, j, 0, TAB_LEFT,
                                   gettext (stats_names[j][k]), string);
		}
	    }
      }

      tab_double (direct, 3, 0, TAB_RIGHT, direct_v[i], NULL);
      if (direct_ase[i] != SYSMIS)
	tab_double (direct, 4, 0, TAB_RIGHT, direct_ase[i], NULL);
      if (direct_t[i] != SYSMIS)
	tab_double (direct, 5, 0, TAB_RIGHT, direct_t[i], NULL);
      /*tab_double (direct, 6, 0, TAB_RIGHT, normal_sig (direct_v[i]), NULL);*/
      tab_next_row (direct);
    }

  tab_offset (direct, 0, -1);
}

/* Statistical calculations. */

/* Returns the value of the logarithm of gamma (factorial) function for an integer
   argument PT. */
static double
log_gamma_int (double pt)
{
  double r = 0;
  int i;

  for (i = 2; i < pt; i++)
    r += log(i);

  return r;
}

/* Calculate P_r as specified in _SPSS Statistical Algorithms_,
   Appendix 5. */
static inline double
Pr (int a, int b, int c, int d)
{
  return exp (log_gamma_int (a + b + 1.) -  log_gamma_int (a + 1.)
	    + log_gamma_int (c + d + 1.) - log_gamma_int (b + 1.)
	    + log_gamma_int (a + c + 1.) - log_gamma_int (c + 1.)
	    + log_gamma_int (b + d + 1.) - log_gamma_int (d + 1.)
	    - log_gamma_int (a + b + c + d + 1.));
}

/* Swap the contents of A and B. */
static inline void
swap (int *a, int *b)
{
  int t = *a;
  *a = *b;
  *b = t;
}

/* Calculate significance for Fisher's exact test as specified in
   _SPSS Statistical Algorithms_, Appendix 5. */
static void
calc_fisher (int a, int b, int c, int d, double *fisher1, double *fisher2)
{
  int pt;
  double pn1;

  if (MIN (c, d) < MIN (a, b))
    swap (&a, &c), swap (&b, &d);
  if (MIN (b, d) < MIN (a, c))
    swap (&a, &b), swap (&c, &d);
  if (b * c < a * d)
    {
      if (b < c)
	swap (&a, &b), swap (&c, &d);
      else
	swap (&a, &c), swap (&b, &d);
    }

  pn1 = Pr (a, b, c, d);
  *fisher1 = pn1;
  for (pt = 1; pt <= a; pt++)
    {
      *fisher1 += Pr (a - pt, b + pt, c + pt, d - pt);
    }

  *fisher2 = *fisher1;

  for (pt = 1; pt <= b; pt++)
    {
      double p = Pr (a + pt, b - pt, c - pt, d + pt);
      if (p < pn1)
	*fisher2 += p;
    }
}

/* Calculates chi-squares into CHISQ.  MAT is a matrix with N_COLS
   columns with values COLS and N_ROWS rows with values ROWS.  Values
   in the matrix sum to pt->total. */
static void
calc_chisq (struct pivot_table *pt,
            double chisq[N_CHISQ], int df[N_CHISQ],
	    double *fisher1, double *fisher2)
{
  int r, c;

  chisq[0] = chisq[1] = 0.;
  chisq[2] = chisq[3] = chisq[4] = SYSMIS;
  *fisher1 = *fisher2 = SYSMIS;

  df[0] = df[1] = (pt->ns_cols - 1) * (pt->ns_rows - 1);

  if (pt->ns_rows <= 1 || pt->ns_cols <= 1)
    {
      chisq[0] = chisq[1] = SYSMIS;
      return;
    }

  for (r = 0; r < pt->n_rows; r++)
    for (c = 0; c < pt->n_cols; c++)
      {
	const double expected = pt->row_tot[r] * pt->col_tot[c] / pt->total;
	const double freq = pt->mat[pt->n_cols * r + c];
	const double residual = freq - expected;

        chisq[0] += residual * residual / expected;
	if (freq)
	  chisq[1] += freq * log (expected / freq);
      }

  if (chisq[0] == 0.)
    chisq[0] = SYSMIS;

  if (chisq[1] != 0.)
    chisq[1] *= -2.;
  else
    chisq[1] = SYSMIS;

  /* Calculate Yates and Fisher exact test. */
  if (pt->ns_cols == 2 && pt->ns_rows == 2)
    {
      double f11, f12, f21, f22;

      {
	int nz_cols[2];
	int i, j;

	for (i = j = 0; i < pt->n_cols; i++)
	  if (pt->col_tot[i] != 0.)
	    {
	      nz_cols[j++] = i;
	      if (j == 2)
		break;
	    }

	assert (j == 2);

	f11 = pt->mat[nz_cols[0]];
	f12 = pt->mat[nz_cols[1]];
	f21 = pt->mat[nz_cols[0] + pt->n_cols];
	f22 = pt->mat[nz_cols[1] + pt->n_cols];
      }

      /* Yates. */
      {
	const double pt_ = fabs (f11 * f22 - f12 * f21) - 0.5 * pt->total;

	if (pt_ > 0.)
	  chisq[3] = (pt->total * pow2 (pt_)
		      / (f11 + f12) / (f21 + f22)
		      / (f11 + f21) / (f12 + f22));
	else
	  chisq[3] = 0.;

	df[3] = 1.;
      }

      /* Fisher. */
      calc_fisher (f11 + .5, f12 + .5, f21 + .5, f22 + .5, fisher1, fisher2);
    }

  /* Calculate Mantel-Haenszel. */
  if (var_is_numeric (pt->vars[ROW_VAR]) && var_is_numeric (pt->vars[COL_VAR]))
    {
      double r, ase_0, ase_1;
      calc_r (pt, (double *) pt->rows, (double *) pt->cols, &r, &ase_0, &ase_1);

      chisq[4] = (pt->total - 1.) * r * r;
      df[4] = 1;
    }
}

/* Calculate the value of Pearson's r.  r is stored into R, ase_1 into
   ASE_1, and ase_0 into ASE_0.  The row and column values must be
   passed in PT and Y. */
static void
calc_r (struct pivot_table *pt,
        double *PT, double *Y, double *r, double *ase_0, double *ase_1)
{
  double SX, SY, S, T;
  double Xbar, Ybar;
  double sum_XYf, sum_X2Y2f;
  double sum_Xr, sum_X2r;
  double sum_Yc, sum_Y2c;
  int i, j;

  for (sum_X2Y2f = sum_XYf = 0., i = 0; i < pt->n_rows; i++)
    for (j = 0; j < pt->n_cols; j++)
      {
	double fij = pt->mat[j + i * pt->n_cols];
	double product = PT[i] * Y[j];
	double temp = fij * product;
	sum_XYf += temp;
	sum_X2Y2f += temp * product;
      }

  for (sum_Xr = sum_X2r = 0., i = 0; i < pt->n_rows; i++)
    {
      sum_Xr += PT[i] * pt->row_tot[i];
      sum_X2r += pow2 (PT[i]) * pt->row_tot[i];
    }
  Xbar = sum_Xr / pt->total;

  for (sum_Yc = sum_Y2c = 0., i = 0; i < pt->n_cols; i++)
    {
      sum_Yc += Y[i] * pt->col_tot[i];
      sum_Y2c += Y[i] * Y[i] * pt->col_tot[i];
    }
  Ybar = sum_Yc / pt->total;

  S = sum_XYf - sum_Xr * sum_Yc / pt->total;
  SX = sum_X2r - pow2 (sum_Xr) / pt->total;
  SY = sum_Y2c - pow2 (sum_Yc) / pt->total;
  T = sqrt (SX * SY);
  *r = S / T;
  *ase_0 = sqrt ((sum_X2Y2f - pow2 (sum_XYf) / pt->total) / (sum_X2r * sum_Y2c));

  {
    double s, c, y, t;

    for (s = c = 0., i = 0; i < pt->n_rows; i++)
      for (j = 0; j < pt->n_cols; j++)
	{
	  double Xresid, Yresid;
	  double temp;

	  Xresid = PT[i] - Xbar;
	  Yresid = Y[j] - Ybar;
	  temp = (T * Xresid * Yresid
		  - ((S / (2. * T))
		     * (Xresid * Xresid * SY + Yresid * Yresid * SX)));
	  y = pt->mat[j + i * pt->n_cols] * temp * temp - c;
	  t = s + y;
	  c = (t - s) - y;
	  s = t;
	}
    *ase_1 = sqrt (s) / (T * T);
  }
}

/* Calculate symmetric statistics and their asymptotic standard
   errors.  Returns 0 if none could be calculated. */
static int
calc_symmetric (struct crosstabs_proc *proc, struct pivot_table *pt,
                double v[N_SYMMETRIC], double ase[N_SYMMETRIC],
		double t[N_SYMMETRIC],
                double somers_d_v[3], double somers_d_ase[3],
                double somers_d_t[3])
{
  int q, i;

  q = MIN (pt->ns_rows, pt->ns_cols);
  if (q <= 1)
    return 0;

  for (i = 0; i < N_SYMMETRIC; i++)
    v[i] = ase[i] = t[i] = SYSMIS;

  /* Phi, Cramer's V, contingency coefficient. */
  if (proc->statistics & ((1u << CRS_ST_PHI) | (1u << CRS_ST_CC)))
    {
      double Xp = 0.;	/* Pearson chi-square. */
      int r, c;

      for (r = 0; r < pt->n_rows; r++)
        for (c = 0; c < pt->n_cols; c++)
          {
            const double expected = pt->row_tot[r] * pt->col_tot[c] / pt->total;
            const double freq = pt->mat[pt->n_cols * r + c];
            const double residual = freq - expected;

            Xp += residual * residual / expected;
          }

      if (proc->statistics & (1u << CRS_ST_PHI))
	{
	  v[0] = sqrt (Xp / pt->total);
	  v[1] = sqrt (Xp / (pt->total * (q - 1)));
	}
      if (proc->statistics & (1u << CRS_ST_CC))
	v[2] = sqrt (Xp / (Xp + pt->total));
    }

  if (proc->statistics & ((1u << CRS_ST_BTAU) | (1u << CRS_ST_CTAU)
                          | (1u << CRS_ST_GAMMA) | (1u << CRS_ST_D)))
    {
      double *cum;
      double Dr, Dc;
      double P, Q;
      double btau_cum, ctau_cum, gamma_cum, d_yx_cum, d_xy_cum;
      double btau_var;
      int r, c;

      Dr = Dc = pow2 (pt->total);
      for (r = 0; r < pt->n_rows; r++)
        Dr -= pow2 (pt->row_tot[r]);
      for (c = 0; c < pt->n_cols; c++)
        Dc -= pow2 (pt->col_tot[c]);

      cum = xnmalloc (pt->n_cols * pt->n_rows, sizeof *cum);
      for (c = 0; c < pt->n_cols; c++)
        {
          double ct = 0.;

          for (r = 0; r < pt->n_rows; r++)
            cum[c + r * pt->n_cols] = ct += pt->mat[c + r * pt->n_cols];
        }

      /* P and Q. */
      {
	int i, j;
	double Cij, Dij;

	P = Q = 0.;
	for (i = 0; i < pt->n_rows; i++)
	  {
	    Cij = Dij = 0.;

	    for (j = 1; j < pt->n_cols; j++)
	      Cij += pt->col_tot[j] - cum[j + i * pt->n_cols];

	    if (i > 0)
	      for (j = 1; j < pt->n_cols; j++)
		Dij += cum[j + (i - 1) * pt->n_cols];

	    for (j = 0;;)
	      {
		double fij = pt->mat[j + i * pt->n_cols];
		P += fij * Cij;
		Q += fij * Dij;

		if (++j == pt->n_cols)
		  break;
		assert (j < pt->n_cols);

		Cij -= pt->col_tot[j] - cum[j + i * pt->n_cols];
		Dij += pt->col_tot[j - 1] - cum[j - 1 + i * pt->n_cols];

		if (i > 0)
		  {
		    Cij += cum[j - 1 + (i - 1) * pt->n_cols];
		    Dij -= cum[j + (i - 1) * pt->n_cols];
		  }
	      }
	  }
      }

      if (proc->statistics & (1u << CRS_ST_BTAU))
	v[3] = (P - Q) / sqrt (Dr * Dc);
      if (proc->statistics & (1u << CRS_ST_CTAU))
	v[4] = (q * (P - Q)) / (pow2 (pt->total) * (q - 1));
      if (proc->statistics & (1u << CRS_ST_GAMMA))
	v[5] = (P - Q) / (P + Q);

      /* ASE for tau-b, tau-c, gamma.  Calculations could be
	 eliminated here, at expense of memory.  */
      {
	int i, j;
	double Cij, Dij;

	btau_cum = ctau_cum = gamma_cum = d_yx_cum = d_xy_cum = 0.;
	for (i = 0; i < pt->n_rows; i++)
	  {
	    Cij = Dij = 0.;

	    for (j = 1; j < pt->n_cols; j++)
	      Cij += pt->col_tot[j] - cum[j + i * pt->n_cols];

	    if (i > 0)
	      for (j = 1; j < pt->n_cols; j++)
		Dij += cum[j + (i - 1) * pt->n_cols];

	    for (j = 0;;)
	      {
		double fij = pt->mat[j + i * pt->n_cols];

		if (proc->statistics & (1u << CRS_ST_BTAU))
		  {
		    const double temp = (2. * sqrt (Dr * Dc) * (Cij - Dij)
					 + v[3] * (pt->row_tot[i] * Dc
						   + pt->col_tot[j] * Dr));
		    btau_cum += fij * temp * temp;
		  }

		{
		  const double temp = Cij - Dij;
		  ctau_cum += fij * temp * temp;
		}

		if (proc->statistics & (1u << CRS_ST_GAMMA))
		  {
		    const double temp = Q * Cij - P * Dij;
		    gamma_cum += fij * temp * temp;
		  }

		if (proc->statistics & (1u << CRS_ST_D))
		  {
		    d_yx_cum += fij * pow2 (Dr * (Cij - Dij)
                                            - (P - Q) * (pt->total - pt->row_tot[i]));
		    d_xy_cum += fij * pow2 (Dc * (Dij - Cij)
                                            - (Q - P) * (pt->total - pt->col_tot[j]));
		  }

		if (++j == pt->n_cols)
		  break;
		assert (j < pt->n_cols);

		Cij -= pt->col_tot[j] - cum[j + i * pt->n_cols];
		Dij += pt->col_tot[j - 1] - cum[j - 1 + i * pt->n_cols];

		if (i > 0)
		  {
		    Cij += cum[j - 1 + (i - 1) * pt->n_cols];
		    Dij -= cum[j + (i - 1) * pt->n_cols];
		  }
	      }
	  }
      }

      btau_var = ((btau_cum
		   - (pt->total * pow2 (pt->total * (P - Q) / sqrt (Dr * Dc) * (Dr + Dc))))
		  / pow2 (Dr * Dc));
      if (proc->statistics & (1u << CRS_ST_BTAU))
	{
	  ase[3] = sqrt (btau_var);
	  t[3] = v[3] / (2 * sqrt ((ctau_cum - (P - Q) * (P - Q) / pt->total)
				   / (Dr * Dc)));
	}
      if (proc->statistics & (1u << CRS_ST_CTAU))
	{
	  ase[4] = ((2 * q / ((q - 1) * pow2 (pt->total)))
		    * sqrt (ctau_cum - (P - Q) * (P - Q) / pt->total));
	  t[4] = v[4] / ase[4];
	}
      if (proc->statistics & (1u << CRS_ST_GAMMA))
	{
	  ase[5] = ((4. / ((P + Q) * (P + Q))) * sqrt (gamma_cum));
	  t[5] = v[5] / (2. / (P + Q)
			 * sqrt (ctau_cum - (P - Q) * (P - Q) / pt->total));
	}
      if (proc->statistics & (1u << CRS_ST_D))
	{
	  somers_d_v[0] = (P - Q) / (.5 * (Dc + Dr));
	  somers_d_ase[0] = 2. * btau_var / (Dr + Dc) * sqrt (Dr * Dc);
	  somers_d_t[0] = (somers_d_v[0]
			   / (4 / (Dc + Dr)
			      * sqrt (ctau_cum - pow2 (P - Q) / pt->total)));
	  somers_d_v[1] = (P - Q) / Dc;
	  somers_d_ase[1] = 2. / pow2 (Dc) * sqrt (d_xy_cum);
	  somers_d_t[1] = (somers_d_v[1]
			   / (2. / Dc
			      * sqrt (ctau_cum - pow2 (P - Q) / pt->total)));
	  somers_d_v[2] = (P - Q) / Dr;
	  somers_d_ase[2] = 2. / pow2 (Dr) * sqrt (d_yx_cum);
	  somers_d_t[2] = (somers_d_v[2]
			   / (2. / Dr
			      * sqrt (ctau_cum - pow2 (P - Q) / pt->total)));
	}

      free (cum);
    }

  /* Spearman correlation, Pearson's r. */
  if (proc->statistics & (1u << CRS_ST_CORR))
    {
      double *R = xmalloc (sizeof *R * pt->n_rows);
      double *C = xmalloc (sizeof *C * pt->n_cols);

      {
	double y, t, c = 0., s = 0.;
	int i = 0;

	for (;;)
	  {
	    R[i] = s + (pt->row_tot[i] + 1.) / 2.;
	    y = pt->row_tot[i] - c;
	    t = s + y;
	    c = (t - s) - y;
	    s = t;
	    if (++i == pt->n_rows)
	      break;
	    assert (i < pt->n_rows);
	  }
      }

      {
	double y, t, c = 0., s = 0.;
	int j = 0;

	for (;;)
	  {
	    C[j] = s + (pt->col_tot[j] + 1.) / 2;
	    y = pt->col_tot[j] - c;
	    t = s + y;
	    c = (t - s) - y;
	    s = t;
	    if (++j == pt->n_cols)
	      break;
	    assert (j < pt->n_cols);
	  }
      }

      calc_r (pt, R, C, &v[6], &t[6], &ase[6]);
      t[6] = v[6] / t[6];

      free (R);
      free (C);

      calc_r (pt, (double *) pt->rows, (double *) pt->cols, &v[7], &t[7], &ase[7]);
      t[7] = v[7] / t[7];
    }

  /* Cohen's kappa. */
  if (proc->statistics & (1u << CRS_ST_KAPPA) && pt->ns_rows == pt->ns_cols)
    {
      double sum_fii, sum_rici, sum_fiiri_ci, sum_fijri_ci2, sum_riciri_ci;
      int i, j;

      for (sum_fii = sum_rici = sum_fiiri_ci = sum_riciri_ci = 0., i = j = 0;
	   i < pt->ns_rows; i++, j++)
	{
	  double prod, sum;

	  while (pt->col_tot[j] == 0.)
	    j++;

	  prod = pt->row_tot[i] * pt->col_tot[j];
	  sum = pt->row_tot[i] + pt->col_tot[j];

	  sum_fii += pt->mat[j + i * pt->n_cols];
	  sum_rici += prod;
	  sum_fiiri_ci += pt->mat[j + i * pt->n_cols] * sum;
	  sum_riciri_ci += prod * sum;
	}
      for (sum_fijri_ci2 = 0., i = 0; i < pt->ns_rows; i++)
	for (j = 0; j < pt->ns_cols; j++)
	  {
	    double sum = pt->row_tot[i] + pt->col_tot[j];
	    sum_fijri_ci2 += pt->mat[j + i * pt->n_cols] * sum * sum;
	  }

      v[8] = (pt->total * sum_fii - sum_rici) / (pow2 (pt->total) - sum_rici);

      ase[8] = sqrt ((pow2 (pt->total) * sum_rici
		      + sum_rici * sum_rici
		      - pt->total * sum_riciri_ci)
		     / (pt->total * (pow2 (pt->total) - sum_rici) * (pow2 (pt->total) - sum_rici)));
#if 0
      t[8] = v[8] / sqrt (pt->total * (((sum_fii * (pt->total - sum_fii))
				/ pow2 (pow2 (pt->total) - sum_rici))
			       + ((2. * (pt->total - sum_fii)
				   * (2. * sum_fii * sum_rici
				      - pt->total * sum_fiiri_ci))
				  / cube (pow2 (pt->total) - sum_rici))
			       + (pow2 (pt->total - sum_fii)
				  * (pt->total * sum_fijri_ci2 - 4.
				     * sum_rici * sum_rici)
				  / pow4 (pow2 (pt->total) - sum_rici))));
#else
      t[8] = v[8] / ase[8];
#endif
    }

  return 1;
}

/* Calculate risk estimate. */
static int
calc_risk (struct pivot_table *pt,
           double *value, double *upper, double *lower, union value *c)
{
  double f11, f12, f21, f22;
  double v;

  {
    int i;

    for (i = 0; i < 3; i++)
      value[i] = upper[i] = lower[i] = SYSMIS;
  }

  if (pt->ns_rows != 2 || pt->ns_cols != 2)
    return 0;

  {
    int nz_cols[2];
    int i, j;

    for (i = j = 0; i < pt->n_cols; i++)
      if (pt->col_tot[i] != 0.)
	{
	  nz_cols[j++] = i;
	  if (j == 2)
	    break;
	}

    assert (j == 2);

    f11 = pt->mat[nz_cols[0]];
    f12 = pt->mat[nz_cols[1]];
    f21 = pt->mat[nz_cols[0] + pt->n_cols];
    f22 = pt->mat[nz_cols[1] + pt->n_cols];

    c[0] = pt->cols[nz_cols[0]];
    c[1] = pt->cols[nz_cols[1]];
  }

  value[0] = (f11 * f22) / (f12 * f21);
  v = sqrt (1. / f11 + 1. / f12 + 1. / f21 + 1. / f22);
  lower[0] = value[0] * exp (-1.960 * v);
  upper[0] = value[0] * exp (1.960 * v);

  value[1] = (f11 * (f21 + f22)) / (f21 * (f11 + f12));
  v = sqrt ((f12 / (f11 * (f11 + f12)))
	    + (f22 / (f21 * (f21 + f22))));
  lower[1] = value[1] * exp (-1.960 * v);
  upper[1] = value[1] * exp (1.960 * v);

  value[2] = (f12 * (f21 + f22)) / (f22 * (f11 + f12));
  v = sqrt ((f11 / (f12 * (f11 + f12)))
	    + (f21 / (f22 * (f21 + f22))));
  lower[2] = value[2] * exp (-1.960 * v);
  upper[2] = value[2] * exp (1.960 * v);

  return 1;
}

/* Calculate directional measures. */
static int
calc_directional (struct crosstabs_proc *proc, struct pivot_table *pt,
                  double v[N_DIRECTIONAL], double ase[N_DIRECTIONAL],
		  double t[N_DIRECTIONAL])
{
  {
    int i;

    for (i = 0; i < N_DIRECTIONAL; i++)
      v[i] = ase[i] = t[i] = SYSMIS;
  }

  /* Lambda. */
  if (proc->statistics & (1u << CRS_ST_LAMBDA))
    {
      double *fim = xnmalloc (pt->n_rows, sizeof *fim);
      int *fim_index = xnmalloc (pt->n_rows, sizeof *fim_index);
      double *fmj = xnmalloc (pt->n_cols, sizeof *fmj);
      int *fmj_index = xnmalloc (pt->n_cols, sizeof *fmj_index);
      double sum_fim, sum_fmj;
      double rm, cm;
      int rm_index, cm_index;
      int i, j;

      /* Find maximum for each row and their sum. */
      for (sum_fim = 0., i = 0; i < pt->n_rows; i++)
	{
	  double max = pt->mat[i * pt->n_cols];
	  int index = 0;

	  for (j = 1; j < pt->n_cols; j++)
	    if (pt->mat[j + i * pt->n_cols] > max)
	      {
		max = pt->mat[j + i * pt->n_cols];
		index = j;
	      }

	  sum_fim += fim[i] = max;
	  fim_index[i] = index;
	}

      /* Find maximum for each column. */
      for (sum_fmj = 0., j = 0; j < pt->n_cols; j++)
	{
	  double max = pt->mat[j];
	  int index = 0;

	  for (i = 1; i < pt->n_rows; i++)
	    if (pt->mat[j + i * pt->n_cols] > max)
	      {
		max = pt->mat[j + i * pt->n_cols];
		index = i;
	      }

	  sum_fmj += fmj[j] = max;
	  fmj_index[j] = index;
	}

      /* Find maximum row total. */
      rm = pt->row_tot[0];
      rm_index = 0;
      for (i = 1; i < pt->n_rows; i++)
	if (pt->row_tot[i] > rm)
	  {
	    rm = pt->row_tot[i];
	    rm_index = i;
	  }

      /* Find maximum column total. */
      cm = pt->col_tot[0];
      cm_index = 0;
      for (j = 1; j < pt->n_cols; j++)
	if (pt->col_tot[j] > cm)
	  {
	    cm = pt->col_tot[j];
	    cm_index = j;
	  }

      v[0] = (sum_fim + sum_fmj - cm - rm) / (2. * pt->total - rm - cm);
      v[1] = (sum_fmj - rm) / (pt->total - rm);
      v[2] = (sum_fim - cm) / (pt->total - cm);

      /* ASE1 for Y given PT. */
      {
	double accum;

	for (accum = 0., i = 0; i < pt->n_rows; i++)
	  for (j = 0; j < pt->n_cols; j++)
	    {
	      const int deltaj = j == cm_index;
	      accum += (pt->mat[j + i * pt->n_cols]
			* pow2 ((j == fim_index[i])
			       - deltaj
			       + v[0] * deltaj));
	    }

	ase[2] = sqrt (accum - pt->total * v[0]) / (pt->total - cm);
      }

      /* ASE0 for Y given PT. */
      {
	double accum;

	for (accum = 0., i = 0; i < pt->n_rows; i++)
	  if (cm_index != fim_index[i])
	    accum += (pt->mat[i * pt->n_cols + fim_index[i]]
		      + pt->mat[i * pt->n_cols + cm_index]);
	t[2] = v[2] / (sqrt (accum - pow2 (sum_fim - cm) / pt->total) / (pt->total - cm));
      }

      /* ASE1 for PT given Y. */
      {
	double accum;

	for (accum = 0., i = 0; i < pt->n_rows; i++)
	  for (j = 0; j < pt->n_cols; j++)
	    {
	      const int deltaj = i == rm_index;
	      accum += (pt->mat[j + i * pt->n_cols]
			* pow2 ((i == fmj_index[j])
			       - deltaj
			       + v[0] * deltaj));
	    }

	ase[1] = sqrt (accum - pt->total * v[0]) / (pt->total - rm);
      }

      /* ASE0 for PT given Y. */
      {
	double accum;

	for (accum = 0., j = 0; j < pt->n_cols; j++)
	  if (rm_index != fmj_index[j])
	    accum += (pt->mat[j + pt->n_cols * fmj_index[j]]
		      + pt->mat[j + pt->n_cols * rm_index]);
	t[1] = v[1] / (sqrt (accum - pow2 (sum_fmj - rm) / pt->total) / (pt->total - rm));
      }

      /* Symmetric ASE0 and ASE1. */
      {
	double accum0;
	double accum1;

	for (accum0 = accum1 = 0., i = 0; i < pt->n_rows; i++)
	  for (j = 0; j < pt->n_cols; j++)
	    {
	      int temp0 = (fmj_index[j] == i) + (fim_index[i] == j);
	      int temp1 = (i == rm_index) + (j == cm_index);
	      accum0 += pt->mat[j + i * pt->n_cols] * pow2 (temp0 - temp1);
	      accum1 += (pt->mat[j + i * pt->n_cols]
			 * pow2 (temp0 + (v[0] - 1.) * temp1));
	    }
	ase[0] = sqrt (accum1 - 4. * pt->total * v[0] * v[0]) / (2. * pt->total - rm - cm);
	t[0] = v[0] / (sqrt (accum0 - pow2 ((sum_fim + sum_fmj - cm - rm) / pt->total))
		       / (2. * pt->total - rm - cm));
      }

      free (fim);
      free (fim_index);
      free (fmj);
      free (fmj_index);

      {
	double sum_fij2_ri, sum_fij2_ci;
	double sum_ri2, sum_cj2;

	for (sum_fij2_ri = sum_fij2_ci = 0., i = 0; i < pt->n_rows; i++)
	  for (j = 0; j < pt->n_cols; j++)
	    {
	      double temp = pow2 (pt->mat[j + i * pt->n_cols]);
	      sum_fij2_ri += temp / pt->row_tot[i];
	      sum_fij2_ci += temp / pt->col_tot[j];
	    }

	for (sum_ri2 = 0., i = 0; i < pt->n_rows; i++)
	  sum_ri2 += pow2 (pt->row_tot[i]);

	for (sum_cj2 = 0., j = 0; j < pt->n_cols; j++)
	  sum_cj2 += pow2 (pt->col_tot[j]);

	v[3] = (pt->total * sum_fij2_ci - sum_ri2) / (pow2 (pt->total) - sum_ri2);
	v[4] = (pt->total * sum_fij2_ri - sum_cj2) / (pow2 (pt->total) - sum_cj2);
      }
    }

  if (proc->statistics & (1u << CRS_ST_UC))
    {
      double UX, UY, UXY, P;
      double ase1_yx, ase1_xy, ase1_sym;
      int i, j;

      for (UX = 0., i = 0; i < pt->n_rows; i++)
	if (pt->row_tot[i] > 0.)
	  UX -= pt->row_tot[i] / pt->total * log (pt->row_tot[i] / pt->total);

      for (UY = 0., j = 0; j < pt->n_cols; j++)
	if (pt->col_tot[j] > 0.)
	  UY -= pt->col_tot[j] / pt->total * log (pt->col_tot[j] / pt->total);

      for (UXY = P = 0., i = 0; i < pt->n_rows; i++)
	for (j = 0; j < pt->n_cols; j++)
	  {
	    double entry = pt->mat[j + i * pt->n_cols];

	    if (entry <= 0.)
	      continue;

	    P += entry * pow2 (log (pt->col_tot[j] * pt->row_tot[i] / (pt->total * entry)));
	    UXY -= entry / pt->total * log (entry / pt->total);
	  }

      for (ase1_yx = ase1_xy = ase1_sym = 0., i = 0; i < pt->n_rows; i++)
	for (j = 0; j < pt->n_cols; j++)
	  {
	    double entry = pt->mat[j + i * pt->n_cols];

	    if (entry <= 0.)
	      continue;

	    ase1_yx += entry * pow2 (UY * log (entry / pt->row_tot[i])
				    + (UX - UXY) * log (pt->col_tot[j] / pt->total));
	    ase1_xy += entry * pow2 (UX * log (entry / pt->col_tot[j])
				    + (UY - UXY) * log (pt->row_tot[i] / pt->total));
	    ase1_sym += entry * pow2 ((UXY
				      * log (pt->row_tot[i] * pt->col_tot[j] / pow2 (pt->total)))
				     - (UX + UY) * log (entry / pt->total));
	  }

      v[5] = 2. * ((UX + UY - UXY) / (UX + UY));
      ase[5] = (2. / (pt->total * pow2 (UX + UY))) * sqrt (ase1_sym);
      t[5] = v[5] / ((2. / (pt->total * (UX + UY)))
		     * sqrt (P - pow2 (UX + UY - UXY) / pt->total));

      v[6] = (UX + UY - UXY) / UX;
      ase[6] = sqrt (ase1_xy) / (pt->total * UX * UX);
      t[6] = v[6] / (sqrt (P - pt->total * pow2 (UX + UY - UXY)) / (pt->total * UX));

      v[7] = (UX + UY - UXY) / UY;
      ase[7] = sqrt (ase1_yx) / (pt->total * UY * UY);
      t[7] = v[7] / (sqrt (P - pt->total * pow2 (UX + UY - UXY)) / (pt->total * UY));
    }

  /* Somers' D. */
  if (proc->statistics & (1u << CRS_ST_D))
    {
      double v_dummy[N_SYMMETRIC];
      double ase_dummy[N_SYMMETRIC];
      double t_dummy[N_SYMMETRIC];
      double somers_d_v[3];
      double somers_d_ase[3];
      double somers_d_t[3];

      if (calc_symmetric (proc, pt, v_dummy, ase_dummy, t_dummy,
                          somers_d_v, somers_d_ase, somers_d_t))
        {
          int i;
          for (i = 0; i < 3; i++)
            {
              v[8 + i] = somers_d_v[i];
              ase[8 + i] = somers_d_ase[i];
              t[8 + i] = somers_d_t[i];
            }
        }
    }

  /* Eta. */
  if (proc->statistics & (1u << CRS_ST_ETA))
    {
      {
	double sum_Xr, sum_X2r;
	double SX, SXW;
	int i, j;

	for (sum_Xr = sum_X2r = 0., i = 0; i < pt->n_rows; i++)
	  {
	    sum_Xr += pt->rows[i].f * pt->row_tot[i];
	    sum_X2r += pow2 (pt->rows[i].f) * pt->row_tot[i];
	  }
	SX = sum_X2r - pow2 (sum_Xr) / pt->total;

	for (SXW = 0., j = 0; j < pt->n_cols; j++)
	  {
	    double cum;

	    for (cum = 0., i = 0; i < pt->n_rows; i++)
	      {
		SXW += pow2 (pt->rows[i].f) * pt->mat[j + i * pt->n_cols];
		cum += pt->rows[i].f * pt->mat[j + i * pt->n_cols];
	      }

	    SXW -= cum * cum / pt->col_tot[j];
	  }
	v[11] = sqrt (1. - SXW / SX);
      }

      {
	double sum_Yc, sum_Y2c;
	double SY, SYW;
	int i, j;

	for (sum_Yc = sum_Y2c = 0., i = 0; i < pt->n_cols; i++)
	  {
	    sum_Yc += pt->cols[i].f * pt->col_tot[i];
	    sum_Y2c += pow2 (pt->cols[i].f) * pt->col_tot[i];
	  }
	SY = sum_Y2c - sum_Yc * sum_Yc / pt->total;

	for (SYW = 0., i = 0; i < pt->n_rows; i++)
	  {
	    double cum;

	    for (cum = 0., j = 0; j < pt->n_cols; j++)
	      {
		SYW += pow2 (pt->cols[j].f) * pt->mat[j + i * pt->n_cols];
		cum += pt->cols[j].f * pt->mat[j + i * pt->n_cols];
	      }

	    SYW -= cum * cum / pt->row_tot[i];
	  }
	v[12] = sqrt (1. - SYW / SY);
      }
    }

  return 1;
}

/*
   Local Variables:
   mode: c
   End:
*/
