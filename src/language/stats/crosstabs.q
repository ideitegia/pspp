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
#include <gsl/gsl_cdf.h>
#include <stdlib.h>
#include <stdio.h>

#include <data/case.h>
#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/value-labels.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/dictionary/split-file.h>
#include <language/lexer/lexer.h>
#include <libpspp/alloc.h>
#include <libpspp/array.h>
#include <libpspp/compiler.h>
#include <libpspp/hash.h>
#include <libpspp/magic.h>
#include <libpspp/message.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/pool.h>
#include <libpspp/str.h>
#include <output/output.h>
#include <output/table.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* (headers) */

/* (specification)
   crosstabs (crs_):
     *^tables=custom;
     +variables=custom;
     +missing=miss:!table/include/report;
     +write[wr_]=none,cells,all;
     +format=fmt:!labels/nolabels/novallabs,
	     val:!avalue/dvalue,
	     indx:!noindex/index,
	     tabl:!tables/notables,
	     box:!box/nobox,
	     pivot:!pivot/nopivot;
     +cells[cl_]=count,none,expected,row,column,total,residual,sresidual,
		 asresidual,all;
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
    int table;		/* Flattened table number. */
    union
      {
	double freq;	/* Frequency count. */
	double *data;	/* Crosstabulation table for integer mode. */
      }
    u;
    union value values[1];	/* Values. */
  };

/* A crosstabulation. */
struct crosstab
  {
    int nvar;			/* Number of variables. */
    double missing;		/* Missing cases count. */
    int ofs;			/* Integer mode: Offset into sorted_tab[]. */
    struct variable *vars[2];	/* At least two variables; sorted by
				   larger indices first. */
  };

/* Integer mode variable info. */
struct var_range
  {
    int min;			/* Minimum value. */
    int max;			/* Maximum value + 1. */
    int count;			/* max - min. */
  };

static inline struct var_range *
get_var_range (struct variable *v) 
{
  assert (v != NULL);
  assert (v->aux != NULL);
  return v->aux;
}

/* Indexes into crosstab.v. */
enum
  {
    ROW_VAR = 0,
    COL_VAR = 1
  };

/* General mode crosstabulation table. */
static struct hsh_table *gen_tab;	/* Hash table. */
static int n_sorted_tab;		/* Number of entries in sorted_tab. */
static struct table_entry **sorted_tab;	/* Sorted table. */

/* Variables specifies on VARIABLES. */
static struct variable **variables;
static size_t variables_cnt;

/* TABLES. */
static struct crosstab **xtab;
static int nxtab;

/* Integer or general mode? */
enum
  {
    INTEGER,
    GENERAL
  };
static int mode;

/* CELLS. */
static int num_cells;		/* Number of cells requested. */
static int cells[8];		/* Cells requested. */

/* WRITE. */
static int write;		/* One of WR_* that specifies the WRITE style. */

/* Command parsing info. */
static struct cmd_crosstabs cmd;

/* Pools. */
static struct pool *pl_tc;	/* For table cells. */
static struct pool *pl_col;	/* For column data. */

static int internal_cmd_crosstabs (void);
static void precalc (const struct ccase *, void *);
static bool calc_general (const struct ccase *, void *);
static bool calc_integer (const struct ccase *, void *);
static void postcalc (void *);
static void submit (struct tab_table *);

static void format_short (char *s, const struct fmt_spec *fp,
                         const union value *v);

/* Parse and execute CROSSTABS, then clean up. */
int
cmd_crosstabs (void)
{
  int result = internal_cmd_crosstabs ();

  free (variables);
  pool_destroy (pl_tc);
  pool_destroy (pl_col);
  
  return result;
}

/* Parses and executes the CROSSTABS procedure. */
static int
internal_cmd_crosstabs (void)
{
  int i;
  bool ok;

  variables = NULL;
  variables_cnt = 0;
  xtab = NULL;
  nxtab = 0;
  pl_tc = pool_create ();
  pl_col = pool_create ();

  if (!parse_crosstabs (&cmd))
    return CMD_FAILURE;

  mode = variables ? INTEGER : GENERAL;

  /* CELLS. */
  if (!cmd.sbc_cells)
    {
      cmd.a_cells[CRS_CL_COUNT] = 1;
    }
  else 
    {
      int count = 0;

      for (i = 0; i < CRS_CL_count; i++)
	if (cmd.a_cells[i])
	  count++;
      if (count == 0)
	{
	  cmd.a_cells[CRS_CL_COUNT] = 1;
	  cmd.a_cells[CRS_CL_ROW] = 1;
	  cmd.a_cells[CRS_CL_COLUMN] = 1;
	  cmd.a_cells[CRS_CL_TOTAL] = 1;
	}
      if (cmd.a_cells[CRS_CL_ALL])
	{
	  for (i = 0; i < CRS_CL_count; i++)
	    cmd.a_cells[i] = 1;
	  cmd.a_cells[CRS_CL_ALL] = 0;
	}
      cmd.a_cells[CRS_CL_NONE] = 0;
    }
  for (num_cells = i = 0; i < CRS_CL_count; i++)
    if (cmd.a_cells[i])
      cells[num_cells++] = i;

  /* STATISTICS. */
  if (cmd.sbc_statistics)
    {
      int i;
      int count = 0;

      for (i = 0; i < CRS_ST_count; i++)
	if (cmd.a_statistics[i])
	  count++;
      if (count == 0)
	cmd.a_statistics[CRS_ST_CHISQ] = 1;
      if (cmd.a_statistics[CRS_ST_ALL])
	for (i = 0; i < CRS_ST_count; i++)
	  cmd.a_statistics[i] = 1;
    }
  
  /* MISSING. */
  if (cmd.miss == CRS_REPORT && mode == GENERAL)
    {
      msg (SE, _("Missing mode REPORT not allowed in general mode.  "
		 "Assuming MISSING=TABLE."));
      cmd.miss = CRS_TABLE;
    }

  /* WRITE. */
  if (cmd.a_write[CRS_WR_ALL] && cmd.a_write[CRS_WR_CELLS])
    cmd.a_write[CRS_WR_ALL] = 0;
  if (cmd.a_write[CRS_WR_ALL] && mode == GENERAL)
    {
      msg (SE, _("Write mode ALL not allowed in general mode.  "
		 "Assuming WRITE=CELLS."));
      cmd.a_write[CRS_WR_CELLS] = 1;
    }
  if (cmd.sbc_write
      && (cmd.a_write[CRS_WR_NONE]
	  + cmd.a_write[CRS_WR_ALL]
	  + cmd.a_write[CRS_WR_CELLS] == 0))
    cmd.a_write[CRS_WR_CELLS] = 1;
  if (cmd.a_write[CRS_WR_CELLS])
    write = CRS_WR_CELLS;
  else if (cmd.a_write[CRS_WR_ALL])
    write = CRS_WR_ALL;
  else
    write = CRS_WR_NONE;

  ok = procedure_with_splits (precalc,
                              mode == GENERAL ? calc_general : calc_integer,
                              postcalc, NULL);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
}

/* Parses the TABLES subcommand. */
static int
crs_custom_tables (struct cmd_crosstabs *cmd UNUSED)
{
  struct var_set *var_set;
  int n_by;
  struct variable ***by = NULL;
  size_t *by_nvar = NULL;
  size_t nx = 1;
  int success = 0;

  /* Ensure that this is a TABLES subcommand. */
  if (!lex_match_id ("TABLES")
      && (token != T_ID || dict_lookup_var (default_dict, tokid) == NULL)
      && token != T_ALL)
    return 2;
  lex_match ('=');

  if (variables != NULL)
    var_set = var_set_create_from_array (variables, variables_cnt);
  else
    var_set = var_set_create_from_dict (default_dict);
  assert (var_set != NULL);
  
  for (n_by = 0; ;)
    {
      by = xnrealloc (by, n_by + 1, sizeof *by);
      by_nvar = xnrealloc (by_nvar, n_by + 1, sizeof *by_nvar);
      if (!parse_var_set_vars (var_set, &by[n_by], &by_nvar[n_by],
                               PV_NO_DUPLICATE | PV_NO_SCRATCH))
	goto done;
      if (xalloc_oversized (nx, by_nvar[n_by])) 
        {
          msg (SE, _("Too many crosstabulation variables or dimensions."));
          goto done;
        }
      nx *= by_nvar[n_by];
      n_by++;

      if (!lex_match (T_BY))
	{
	  if (n_by < 2)
	    {
	      lex_error (_("expecting BY"));
	      goto done;
	    }
	  else 
	    break;
	}
    }
  
  {
    int *by_iter = xcalloc (n_by, sizeof *by_iter);
    int i;

    xtab = xnrealloc (xtab, nxtab + nx, sizeof *xtab);
    for (i = 0; i < nx; i++)
      {
	struct crosstab *x;

	x = xmalloc (sizeof *x + sizeof (struct variable *) * (n_by - 2));
	x->nvar = n_by;
	x->missing = 0.;

	{
	  int i;

          for (i = 0; i < n_by; i++)
            x->vars[i] = by[i][by_iter[i]];
	}
	
	{
	  int i;

	  for (i = n_by - 1; i >= 0; i--)
	    {
	      if (++by_iter[i] < by_nvar[i])
		break;
	      by_iter[i] = 0;
	    }
	}

	xtab[nxtab++] = x;
      }
    free (by_iter);
  }
  success = 1;

 done:
  /* All return paths lead here. */
  {
    int i;

    for (i = 0; i < n_by; i++)
      free (by[i]);
    free (by);
    free (by_nvar);
  }

  var_set_destroy (var_set);

  return success;
}

/* Parses the VARIABLES subcommand. */
static int
crs_custom_variables (struct cmd_crosstabs *cmd UNUSED)
{
  if (nxtab)
    {
      msg (SE, _("VARIABLES must be specified before TABLES."));
      return 0;
    }

  lex_match ('=');
  
  for (;;)
    {
      size_t orig_nv = variables_cnt;
      size_t i;

      long min, max;
      
      if (!parse_variables (default_dict, &variables, &variables_cnt,
			    (PV_APPEND | PV_NUMERIC
			     | PV_NO_DUPLICATE | PV_NO_SCRATCH)))
	return 0;

      if (token != '(')
	{
	  lex_error ("expecting `('");
	  goto lossage;
	}
      lex_get ();

      if (!lex_force_int ())
	goto lossage;
      min = lex_integer ();
      lex_get ();

      lex_match (',');

      if (!lex_force_int ())
	goto lossage;
      max = lex_integer ();
      if (max < min)
	{
	  msg (SE, _("Maximum value (%ld) less than minimum value (%ld)."),
	       max, min);
	  goto lossage;
	}
      lex_get ();

      if (token != ')')
	{
	  lex_error ("expecting `)'");
	  goto lossage;
	}
      lex_get ();
      
      for (i = orig_nv; i < variables_cnt; i++) 
        {
          struct var_range *vr = xmalloc (sizeof *vr);
          vr->min = min;
	  vr->max = max + 1.;
	  vr->count = max - min + 1;
          var_attach_aux (variables[i], vr, var_dtor_free);
	}
      
      if (token == '/')
	break;
    }
  
  return 1;

 lossage:
  free (variables);
  variables = NULL;
  return 0;
}

/* Data file processing. */

static int compare_table_entry (const void *, const void *, void *);
static unsigned hash_table_entry (const void *, void *);

/* Set up the crosstabulation tables for processing. */
static void
precalc (const struct ccase *first, void *aux UNUSED)
{
  output_split_file_values (first);
  if (mode == GENERAL)
    {
      gen_tab = hsh_create (512, compare_table_entry, hash_table_entry,
			    NULL, NULL);
    }
  else 
    {
      int i;

      sorted_tab = NULL;
      n_sorted_tab = 0;

      for (i = 0; i < nxtab; i++)
	{
	  struct crosstab *x = xtab[i];
	  int count = 1;
	  int *v;
	  int j;

	  x->ofs = n_sorted_tab;

	  for (j = 2; j < x->nvar; j++) 
            count *= get_var_range (x->vars[j - 2])->count;
          
	  sorted_tab = xnrealloc (sorted_tab,
                                  n_sorted_tab + count, sizeof *sorted_tab);
	  v = local_alloc (sizeof *v * x->nvar);
	  for (j = 2; j < x->nvar; j++) 
            v[j] = get_var_range (x->vars[j])->min; 
	  for (j = 0; j < count; j++)
	    {
	      struct table_entry *te;
	      int k;

	      te = sorted_tab[n_sorted_tab++]
		= xmalloc (sizeof *te + sizeof (union value) * (x->nvar - 1));
	      te->table = i;
	      
	      {
                int row_cnt = get_var_range (x->vars[0])->count;
                int col_cnt = get_var_range (x->vars[1])->count;
		const int mat_size = row_cnt * col_cnt;
		int m;
		
		te->u.data = xnmalloc (mat_size, sizeof *te->u.data);
		for (m = 0; m < mat_size; m++)
		  te->u.data[m] = 0.;
	      }
	      
	      for (k = 2; k < x->nvar; k++)
		te->values[k].f = v[k];
	      for (k = 2; k < x->nvar; k++) 
                {
                  struct var_range *vr = get_var_range (x->vars[k]);
                  if (++v[k] >= vr->max)
                    v[k] = vr->min;
                  else
                    break; 
                }
	    }
	  local_free (v);
	}

      sorted_tab = xnrealloc (sorted_tab,
                              n_sorted_tab + 1, sizeof *sorted_tab);
      sorted_tab[n_sorted_tab] = NULL;
    }
}

/* Form crosstabulations for general mode. */
static bool
calc_general (const struct ccase *c, void *aux UNUSED)
{
  int bad_warn = 1;

  /* Case weight. */
  double weight = dict_get_case_weight (default_dict, c, &bad_warn);

  /* Flattened current table index. */
  int t;

  for (t = 0; t < nxtab; t++)
    {
      struct crosstab *x = xtab[t];
      const size_t entry_size = (sizeof (struct table_entry)
				 + sizeof (union value) * (x->nvar - 1));
      struct table_entry *te = local_alloc (entry_size);

      /* Construct table entry for the current record and table. */
      te->table = t;
      {
	int j;

	assert (x != NULL);
	for (j = 0; j < x->nvar; j++)
	  {
            const union value *v = case_data (c, x->vars[j]->fv);
            const struct missing_values *mv = &x->vars[j]->miss;
	    if ((cmd.miss == CRS_TABLE && mv_is_value_missing (mv, v))
		|| (cmd.miss == CRS_INCLUDE
		    && mv_is_value_system_missing (mv, v)))
	      {
		x->missing += weight;
		goto next_crosstab;
	      }
	      
	    if (x->vars[j]->type == NUMERIC)
	      te->values[j].f = case_num (c, x->vars[j]->fv);
	    else
	      {
		memcpy (te->values[j].s, case_str (c, x->vars[j]->fv),
                        x->vars[j]->width);
	      
		/* Necessary in order to simplify comparisons. */
		memset (&te->values[j].s[x->vars[j]->width], 0,
			sizeof (union value) - x->vars[j]->width);
	      }
	  }
      }

      /* Add record to hash table. */
      {
	struct table_entry **tepp
          = (struct table_entry **) hsh_probe (gen_tab, te);
	if (*tepp == NULL)
	  {
	    struct table_entry *tep = pool_alloc (pl_tc, entry_size);
	    
	    te->u.freq = weight;
	    memcpy (tep, te, entry_size);
	    
	    *tepp = tep;
	  }
	else
	  (*tepp)->u.freq += weight;
      }

    next_crosstab:
      local_free (te);
    }
  
  return true;
}

static bool
calc_integer (const struct ccase *c, void *aux UNUSED)
{
  int bad_warn = 1;

  /* Case weight. */
  double weight = dict_get_case_weight (default_dict, c, &bad_warn);
  
  /* Flattened current table index. */
  int t;
  
  for (t = 0; t < nxtab; t++)
    {
      struct crosstab *x = xtab[t];
      int i, fact, ofs;
      
      fact = i = 1;
      ofs = x->ofs;
      for (i = 0; i < x->nvar; i++)
	{
	  struct variable *const v = x->vars[i];
          struct var_range *vr = get_var_range (v);
	  double value = case_num (c, v->fv);
	  
	  /* Note that the first test also rules out SYSMIS. */
	  if ((value < vr->min || value >= vr->max)
	      || (cmd.miss == CRS_TABLE
                  && mv_is_num_user_missing (&v->miss, value)))
	    {
	      x->missing += weight;
	      goto next_crosstab;
	    }
	  
	  if (i > 1)
	    {
	      ofs += fact * ((int) value - vr->min);
	      fact *= vr->count;
	    }
	}
      
      {
        struct variable *row_var = x->vars[ROW_VAR];
	const int row = case_num (c, row_var->fv) - get_var_range (row_var)->min;

        struct variable *col_var = x->vars[COL_VAR];
	const int col = case_num (c, col_var->fv) - get_var_range (col_var)->min;

	const int col_dim = get_var_range (col_var)->count;

	sorted_tab[ofs]->u.data[col + row * col_dim] += weight;
      }
      
    next_crosstab: ;
    }
  
  return true;
}

/* Compare the table_entry's at A and B and return a strcmp()-type
   result. */
static int 
compare_table_entry (const void *a_, const void *b_, void *foo UNUSED)
{
  const struct table_entry *a = a_;
  const struct table_entry *b = b_;

  if (a->table > b->table)
    return 1;
  else if (a->table < b->table)
    return -1;
  
  {
    const struct crosstab *x = xtab[a->table];
    int i;

    for (i = x->nvar - 1; i >= 0; i--)
      if (x->vars[i]->type == NUMERIC)
	{
	  const double diffnum = a->values[i].f - b->values[i].f;
	  if (diffnum < 0)
	    return -1;
	  else if (diffnum > 0)
	    return 1;
	}
      else 
	{
	  assert (x->vars[i]->type == ALPHA);
	  {
	    const int diffstr = strncmp (a->values[i].s, b->values[i].s,
                                         x->vars[i]->width);
	    if (diffstr)
	      return diffstr;
	  }
	}
  }
  
  return 0;
}

/* Calculate a hash value from table_entry A. */
static unsigned
hash_table_entry (const void *a_, void *foo UNUSED)
{
  const struct table_entry *a = a_;
  unsigned long hash;
  int i;

  hash = a->table;
  for (i = 0; i < xtab[a->table]->nvar; i++)
    hash ^= hsh_hash_bytes (&a->values[i], sizeof a->values[i]);
  
  return hash;
}

/* Post-data reading calculations. */

static struct table_entry **find_pivot_extent (struct table_entry **,
                                               int *cnt, int pivot);
static void enum_var_values (struct table_entry **entries, int entry_cnt,
                             int var_idx,
                             union value **values, int *value_cnt);
static void output_pivot_table (struct table_entry **, struct table_entry **,
				double **, double **, double **,
				int *, int *, int *);
static void make_summary_table (void);

static void
postcalc (void *aux UNUSED)
{
  if (mode == GENERAL)
    {
      n_sorted_tab = hsh_count (gen_tab);
      sorted_tab = (struct table_entry **) hsh_sort (gen_tab);
    }
  
  make_summary_table ();
  
  /* Identify all the individual crosstabulation tables, and deal with
     them. */
  {
    struct table_entry **pb = sorted_tab, **pe;	/* Pivot begin, pivot end. */
    int pc = n_sorted_tab;			/* Pivot count. */

    double *mat = NULL, *row_tot = NULL, *col_tot = NULL;
    int maxrows = 0, maxcols = 0, maxcells = 0;

    for (;;)
      {
	pe = find_pivot_extent (pb, &pc, cmd.pivot == CRS_PIVOT);
	if (pe == NULL)
	  break;
	
	output_pivot_table (pb, pe, &mat, &row_tot, &col_tot,
			    &maxrows, &maxcols, &maxcells);
	  
	pb = pe;
      }
    free (mat);
    free (row_tot);
    free (col_tot);
  }
  
  hsh_destroy (gen_tab);
}

static void insert_summary (struct tab_table *, int tab_index, double valid);

/* Output a table summarizing the cases processed. */
static void
make_summary_table (void)
{
  struct tab_table *summary;
  
  struct table_entry **pb = sorted_tab, **pe;
  int pc = n_sorted_tab;
  int cur_tab = 0;

  summary = tab_create (7, 3 + nxtab, 1);
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
  {
    int i;

    for (i = 0; i < 3; i++)
      {
	tab_text (summary, 1 + i * 2, 2, TAB_RIGHT, _("N"));
	tab_text (summary, 2 + i * 2, 2, TAB_RIGHT, _("Percent"));
      }
  }
  tab_offset (summary, 0, 3);
		  
  for (;;)
    {
      double valid;
      
      pe = find_pivot_extent (pb, &pc, cmd.pivot == CRS_PIVOT);
      if (pe == NULL)
	break;

      while (cur_tab < (*pb)->table)
	insert_summary (summary, cur_tab++, 0.);

      if (mode == GENERAL)
	for (valid = 0.; pb < pe; pb++)
	  valid += (*pb)->u.freq;
      else
	{
	  const struct crosstab *const x = xtab[(*pb)->table];
	  const int n_cols = get_var_range (x->vars[COL_VAR])->count;
	  const int n_rows = get_var_range (x->vars[ROW_VAR])->count;
	  const int count = n_cols * n_rows;
	    
	  for (valid = 0.; pb < pe; pb++)
	    {
	      const double *data = (*pb)->u.data;
	      int i;
		
	      for (i = 0; i < count; i++)
		valid += *data++;
	    }
	}
      insert_summary (summary, cur_tab++, valid);

      pb = pe;
    }
  
  while (cur_tab < nxtab)
    insert_summary (summary, cur_tab++, 0.);

  submit (summary);
}

/* Inserts a line into T describing the crosstabulation at index
   TAB_INDEX, which has VALID valid observations. */
static void
insert_summary (struct tab_table *t, int tab_index, double valid)
{
  struct crosstab *x = xtab[tab_index];

  tab_hline (t, TAL_1, 0, 6, 0);
  
  /* Crosstabulation name. */
  {
    char *buf = local_alloc (128 * x->nvar);
    char *cp = buf;
    int i;

    for (i = 0; i < x->nvar; i++)
      {
	if (i > 0)
	  cp = stpcpy (cp, " * ");

	cp = stpcpy (cp,
                     x->vars[i]->label ? x->vars[i]->label : x->vars[i]->name);
      }
    tab_text (t, 0, 0, TAB_LEFT, buf);

    local_free (buf);
  }
    
  /* Counts and percentages. */
  {
    double n[3];
    int i;

    n[0] = valid;
    n[1] = x->missing;
    n[2] = n[0] + n[1];


    for (i = 0; i < 3; i++)
      {
	tab_float (t, i * 2 + 1, 0, TAB_RIGHT, n[i], 8, 0);
	tab_text (t, i * 2 + 2, 0, TAB_RIGHT | TAT_PRINTF, "%.1f%%",
		  n[i] / n[2] * 100.);
      }
  }
  
  tab_next_row (t);
}

/* Output. */

/* Tables. */
static struct tab_table *table;	/* Crosstabulation table. */
static struct tab_table *chisq;	/* Chi-square table. */
static struct tab_table *sym;		/* Symmetric measures table. */
static struct tab_table *risk;		/* Risk estimate table. */
static struct tab_table *direct;	/* Directional measures table. */

/* Statistics. */
static int chisq_fisher;	/* Did any rows include Fisher's exact test? */

/* Column values, number of columns. */
static union value *cols;
static int n_cols;

/* Row values, number of rows. */
static union value *rows;
static int n_rows;
	      
/* Number of statistically interesting columns/rows (columns/rows with
   data in them). */
static int ns_cols, ns_rows;

/* Crosstabulation. */
static struct crosstab *x;

/* Number of variables from the crosstabulation to consider.  This is
   either x->nvar, if pivoting is on, or 2, if pivoting is off. */
static int nvar;

/* Matrix contents. */
static double *mat;		/* Matrix proper. */
static double *row_tot;		/* Row totals. */
static double *col_tot;		/* Column totals. */
static double W;		/* Grand total. */

static void display_dimensions (struct tab_table *, int first_difference,
				struct table_entry *);
static void display_crosstabulation (void);
static void display_chisq (void);
static void display_symmetric (void);
static void display_risk (void);
static void display_directional (void);
static void crosstabs_dim (struct tab_table *, struct outp_driver *);
static void table_value_missing (struct tab_table *table, int c, int r,
				 unsigned char opt, const union value *v,
				 const struct variable *var);
static void delete_missing (void);

/* Output pivot table beginning at PB and continuing until PE,
   exclusive.  For efficiency, *MATP is a pointer to a matrix that can
   hold *MAXROWS entries. */
static void
output_pivot_table (struct table_entry **pb, struct table_entry **pe,
		    double **matp, double **row_totp, double **col_totp,
		    int *maxrows, int *maxcols, int *maxcells)
{
  /* Subtable. */
  struct table_entry **tb = pb, **te;	/* Table begin, table end. */
  int tc = pe - pb;		/* Table count. */

  /* Table entry for header comparison. */
  struct table_entry *cmp = NULL;

  x = xtab[(*pb)->table];
  enum_var_values (pb, pe - pb, COL_VAR, &cols, &n_cols);

  nvar = cmd.pivot == CRS_PIVOT ? x->nvar : 2;

  /* Crosstabulation table initialization. */
  if (num_cells)
    {
      table = tab_create (nvar + n_cols,
			  (pe - pb) / n_cols * 3 / 2 * num_cells + 10, 1);
      tab_headers (table, nvar - 1, 0, 2, 0);

      /* First header line. */
      tab_joint_text (table, nvar - 1, 0, (nvar - 1) + (n_cols - 1), 0,
		      TAB_CENTER | TAT_TITLE, x->vars[COL_VAR]->name);
  
      tab_hline (table, TAL_1, nvar - 1, nvar + n_cols - 2, 1);
	     
      /* Second header line. */
      {
	int i;

	for (i = 2; i < nvar; i++)
	  tab_joint_text (table, nvar - i - 1, 0, nvar - i - 1, 1,
			  TAB_RIGHT | TAT_TITLE,
			  (x->vars[i]->label
                           ? x->vars[i]->label : x->vars[i]->name));
	tab_text (table, nvar - 2, 1, TAB_RIGHT | TAT_TITLE,
		  x->vars[ROW_VAR]->name);
	for (i = 0; i < n_cols; i++)
	  table_value_missing (table, nvar + i - 1, 1, TAB_RIGHT, &cols[i],
			       x->vars[COL_VAR]);
	tab_text (table, nvar + n_cols - 1, 1, TAB_CENTER, _("Total"));
      }

      tab_hline (table, TAL_1, 0, nvar + n_cols - 1, 2);
      tab_vline (table, TAL_1, nvar + n_cols - 1, 0, 1);

      /* Title. */
      {
	char *title = local_alloc (x->nvar * 64 + 128);
	char *cp = title;
	int i;
    
	if (cmd.pivot == CRS_PIVOT)
	  for (i = 0; i < nvar; i++)
	    {
	      if (i)
		cp = stpcpy (cp, " by ");
	      cp = stpcpy (cp, x->vars[i]->name);
	    }
	else
	  {
	    cp = spprintf (cp, "%s by %s for",
                           x->vars[0]->name, x->vars[1]->name);
	    for (i = 2; i < nvar; i++)
	      {
		char buf[64], *bufp;

		if (i > 2)
		  *cp++ = ',';
		*cp++ = ' ';
		cp = stpcpy (cp, x->vars[i]->name);
		*cp++ = '=';
		format_short (buf, &x->vars[i]->print, &(*pb)->values[i]);
		for (bufp = buf; isspace ((unsigned char) *bufp); bufp++)
		  ;
		cp = stpcpy (cp, bufp);
	      }
	  }

	cp = stpcpy (cp, " [");
	for (i = 0; i < num_cells; i++)
	  {
	    struct tuple
	      {
		int value;
		const char *name;
	      };
	
	    static const struct tuple cell_names[] = 
	      {
		{CRS_CL_COUNT, N_("count")},
		{CRS_CL_ROW, N_("row %")},
		{CRS_CL_COLUMN, N_("column %")},
		{CRS_CL_TOTAL, N_("total %")},
		{CRS_CL_EXPECTED, N_("expected")},
		{CRS_CL_RESIDUAL, N_("residual")},
		{CRS_CL_SRESIDUAL, N_("std. resid.")},
		{CRS_CL_ASRESIDUAL, N_("adj. resid.")},
		{-1, NULL},
	      };

	    const struct tuple *t;

	    for (t = cell_names; t->value != cells[i]; t++)
	      assert (t->value != -1);
	    if (i)
	      cp = stpcpy (cp, ", ");
	    cp = stpcpy (cp, gettext (t->name));
	  }
	strcpy (cp, "].");

	tab_title (table, "%s", title);
	local_free (title);
      }
      
      tab_offset (table, 0, 2);
    }
  else
    table = NULL;
  
  /* Chi-square table initialization. */
  if (cmd.a_statistics[CRS_ST_CHISQ])
    {
      chisq = tab_create (6 + (nvar - 2),
			  (pe - pb) / n_cols * 3 / 2 * N_CHISQ + 10, 1);
      tab_headers (chisq, 1 + (nvar - 2), 0, 1, 0);

      tab_title (chisq, _("Chi-square tests."));
      
      tab_offset (chisq, nvar - 2, 0);
      tab_text (chisq, 0, 0, TAB_LEFT | TAT_TITLE, _("Statistic"));
      tab_text (chisq, 1, 0, TAB_RIGHT | TAT_TITLE, _("Value"));
      tab_text (chisq, 2, 0, TAB_RIGHT | TAT_TITLE, _("df"));
      tab_text (chisq, 3, 0, TAB_RIGHT | TAT_TITLE,
		_("Asymp. Sig. (2-sided)"));
      tab_text (chisq, 4, 0, TAB_RIGHT | TAT_TITLE,
		_("Exact. Sig. (2-sided)"));
      tab_text (chisq, 5, 0, TAB_RIGHT | TAT_TITLE,
		_("Exact. Sig. (1-sided)"));
      chisq_fisher = 0;
      tab_offset (chisq, 0, 1);
    }
  else
    chisq = NULL;
  
  /* Symmetric measures. */
  if (cmd.a_statistics[CRS_ST_PHI] || cmd.a_statistics[CRS_ST_CC]
      || cmd.a_statistics[CRS_ST_BTAU] || cmd.a_statistics[CRS_ST_CTAU]
      || cmd.a_statistics[CRS_ST_GAMMA] || cmd.a_statistics[CRS_ST_CORR]
      || cmd.a_statistics[CRS_ST_KAPPA])
    {
      sym = tab_create (6 + (nvar - 2), (pe - pb) / n_cols * 7 + 10, 1);
      tab_headers (sym, 2 + (nvar - 2), 0, 1, 0);
      tab_title (sym, _("Symmetric measures."));

      tab_offset (sym, nvar - 2, 0);
      tab_text (sym, 0, 0, TAB_LEFT | TAT_TITLE, _("Category"));
      tab_text (sym, 1, 0, TAB_LEFT | TAT_TITLE, _("Statistic"));
      tab_text (sym, 2, 0, TAB_RIGHT | TAT_TITLE, _("Value"));
      tab_text (sym, 3, 0, TAB_RIGHT | TAT_TITLE, _("Asymp. Std. Error"));
      tab_text (sym, 4, 0, TAB_RIGHT | TAT_TITLE, _("Approx. T"));
      tab_text (sym, 5, 0, TAB_RIGHT | TAT_TITLE, _("Approx. Sig."));
      tab_offset (sym, 0, 1);
    }
  else
    sym = NULL;

  /* Risk estimate. */
  if (cmd.a_statistics[CRS_ST_RISK])
    {
      risk = tab_create (4 + (nvar - 2), (pe - pb) / n_cols * 4 + 10, 1);
      tab_headers (risk, 1 + nvar - 2, 0, 2, 0);
      tab_title (risk, _("Risk estimate."));

      tab_offset (risk, nvar - 2, 0);
      tab_joint_text (risk, 2, 0, 3, 0, TAB_CENTER | TAT_TITLE | TAT_PRINTF,
		      _("95%% Confidence Interval"));
      tab_text (risk, 0, 1, TAB_LEFT | TAT_TITLE, _("Statistic"));
      tab_text (risk, 1, 1, TAB_RIGHT | TAT_TITLE, _("Value"));
      tab_text (risk, 2, 1, TAB_RIGHT | TAT_TITLE, _("Lower"));
      tab_text (risk, 3, 1, TAB_RIGHT | TAT_TITLE, _("Upper"));
      tab_hline (risk, TAL_1, 2, 3, 1);
      tab_vline (risk, TAL_1, 2, 0, 1);
      tab_offset (risk, 0, 2);
    }
  else
    risk = NULL;

  /* Directional measures. */
  if (cmd.a_statistics[CRS_ST_LAMBDA] || cmd.a_statistics[CRS_ST_UC]
      || cmd.a_statistics[CRS_ST_D] || cmd.a_statistics[CRS_ST_ETA])
    {
      direct = tab_create (7 + (nvar - 2), (pe - pb) / n_cols * 7 + 10, 1);
      tab_headers (direct, 3 + (nvar - 2), 0, 1, 0);
      tab_title (direct, _("Directional measures."));

      tab_offset (direct, nvar - 2, 0);
      tab_text (direct, 0, 0, TAB_LEFT | TAT_TITLE, _("Category"));
      tab_text (direct, 1, 0, TAB_LEFT | TAT_TITLE, _("Statistic"));
      tab_text (direct, 2, 0, TAB_LEFT | TAT_TITLE, _("Type"));
      tab_text (direct, 3, 0, TAB_RIGHT | TAT_TITLE, _("Value"));
      tab_text (direct, 4, 0, TAB_RIGHT | TAT_TITLE, _("Asymp. Std. Error"));
      tab_text (direct, 5, 0, TAB_RIGHT | TAT_TITLE, _("Approx. T"));
      tab_text (direct, 6, 0, TAB_RIGHT | TAT_TITLE, _("Approx. Sig."));
      tab_offset (direct, 0, 1);
    }
  else
    direct = NULL;

  for (;;)
    {
      /* Find pivot subtable if applicable. */
      te = find_pivot_extent (tb, &tc, 0);
      if (te == NULL)
	break;

      /* Find all the row variable values. */
      enum_var_values (tb, te - tb, ROW_VAR, &rows, &n_rows);

      /* Allocate memory space for the column and row totals. */
      if (n_rows > *maxrows)
	{
	  *row_totp = xnrealloc (*row_totp, n_rows, sizeof **row_totp);
	  row_tot = *row_totp;
	  *maxrows = n_rows;
	}
      if (n_cols > *maxcols)
	{
	  *col_totp = xnrealloc (*col_totp, n_cols, sizeof **col_totp);
	  col_tot = *col_totp;
	  *maxcols = n_cols;
	}
      
      /* Allocate table space for the matrix. */
      if (table && tab_row (table) + (n_rows + 1) * num_cells > tab_nr (table))
	tab_realloc (table, -1,
		     max (tab_nr (table) + (n_rows + 1) * num_cells,
			  tab_nr (table) * (pe - pb) / (te - tb)));

      if (mode == GENERAL)
	{
	  /* Allocate memory space for the matrix. */
	  if (n_cols * n_rows > *maxcells)
	    {
	      *matp = xnrealloc (*matp, n_cols * n_rows, sizeof **matp);
	      *maxcells = n_cols * n_rows;
	    }
	  
	  mat = *matp;

	  /* Build the matrix and calculate column totals. */
	  {
	    union value *cur_col = cols;
	    union value *cur_row = rows;
	    double *mp = mat;
	    double *cp = col_tot;
	    struct table_entry **p;

	    *cp = 0.;
	    for (p = &tb[0]; p < te; p++)
	      {
		for (; memcmp (cur_col, &(*p)->values[COL_VAR], sizeof *cur_col);
		     cur_row = rows)
		  {
		    *++cp = 0.;
		    for (; cur_row < &rows[n_rows]; cur_row++)
		      {
			*mp = 0.;
			mp += n_cols;
		      }
		    cur_col++;
		    mp = &mat[cur_col - cols];
		  }

		for (; memcmp (cur_row, &(*p)->values[ROW_VAR], sizeof *cur_row);
		     cur_row++)
		  {
		    *mp = 0.;
		    mp += n_cols;
		  }

		*cp += *mp = (*p)->u.freq;
		mp += n_cols;
		cur_row++;
	      }

	    /* Zero out the rest of the matrix. */
	    for (; cur_row < &rows[n_rows]; cur_row++)
	      {
		*mp = 0.;
		mp += n_cols;
	      }
	    cur_col++;
	    if (cur_col < &cols[n_cols])
	      {
		const int rem_cols = n_cols - (cur_col - cols);
		int c, r;

		for (c = 0; c < rem_cols; c++)
		  *++cp = 0.;
		mp = &mat[cur_col - cols];
		for (r = 0; r < n_rows; r++)
		  {
		    for (c = 0; c < rem_cols; c++)
		      *mp++ = 0.;
		    mp += n_cols - rem_cols;
		  }
	      }
	  }
	}
      else
	{
	  int r, c;
	  double *tp = col_tot;
	  
	  assert (mode == INTEGER);
	  mat = (*tb)->u.data;
	  ns_cols = n_cols;

	  /* Calculate column totals. */
	  for (c = 0; c < n_cols; c++)
	    {
	      double cum = 0.;
	      double *cp = &mat[c];
	      
	      for (r = 0; r < n_rows; r++)
		cum += cp[r * n_cols];
	      *tp++ = cum;
	    }
	}
      
      {
	double *cp;
	
	for (ns_cols = 0, cp = col_tot; cp < &col_tot[n_cols]; cp++)
	  ns_cols += *cp != 0.;
      }

      /* Calculate row totals. */
      {
	double *mp = mat;
	double *rp = row_tot;
	int r, c;
		
	for (ns_rows = 0, r = n_rows; r--; )
	  {
	    double cum = 0.;
	    for (c = n_cols; c--; )
	      cum += *mp++;
	    *rp++ = cum;
	    if (cum != 0.)
	      ns_rows++;
	  }
      }

      /* Calculate grand total. */
      {
	double *tp;
	double cum = 0.;
	int n;

	if (n_rows < n_cols)
	  tp = row_tot, n = n_rows;
	else
	  tp = col_tot, n = n_cols;
	while (n--)
	  cum += *tp++;
	W = cum;
      }
      
      /* Find the first variable that differs from the last subtable,
	 then display the values of the dimensioning variables for
	 each table that needs it. */
      {
	int first_difference = nvar - 1;
	
	if (tb != pb)
	  for (; ; first_difference--)
	    {
	      assert (first_difference >= 2);
	      if (memcmp (&cmp->values[first_difference],
			  &(*tb)->values[first_difference],
                          sizeof *cmp->values))
		break;
	    }
	cmp = *tb;
	    
	if (table)
	  display_dimensions (table, first_difference, *tb);
	if (chisq)
	  display_dimensions (chisq, first_difference, *tb);
	if (sym)
	  display_dimensions (sym, first_difference, *tb);
	if (risk)
	  display_dimensions (risk, first_difference, *tb);
	if (direct)
	  display_dimensions (direct, first_difference, *tb);
      }

      if (table)
	display_crosstabulation ();
      if (cmd.miss == CRS_REPORT)
	delete_missing ();
      if (chisq)
	display_chisq ();
      if (sym)
	display_symmetric ();
      if (risk)
	display_risk ();
      if (direct)
	display_directional ();
		
      tb = te;
      free (rows);
    }

  submit (table);
  
  if (chisq)
    {
      if (!chisq_fisher)
	tab_resize (chisq, 4 + (nvar - 2), -1);
      submit (chisq);
    }

  submit (sym);
  submit (risk);
  submit (direct);

  free (cols);
}

/* Delete missing rows and columns for statistical analysis when
   /MISSING=REPORT. */
static void
delete_missing (void)
{
  {
    int r;

    for (r = 0; r < n_rows; r++)
      if (mv_is_num_user_missing (&x->vars[ROW_VAR]->miss, rows[r].f))
	{
	  int c;

	  for (c = 0; c < n_cols; c++)
	    mat[c + r * n_cols] = 0.;
	  ns_rows--;
	}
  }
  
  {
    int c;

    for (c = 0; c < n_cols; c++)
      if (mv_is_num_user_missing (&x->vars[COL_VAR]->miss, cols[c].f))
	{
	  int r;

	  for (r = 0; r < n_rows; r++)
	    mat[c + r * n_cols] = 0.;
	  ns_cols--;
	}
  }
}

/* Prepare table T for submission, and submit it. */
static void
submit (struct tab_table *t)
{
  int i;
  
  if (t == NULL)
    return;
  
  tab_resize (t, -1, 0);
  if (tab_nr (t) == tab_t (t))
    {
      tab_destroy (t);
      return;
    }
  tab_offset (t, 0, 0);
  if (t != table)
    for (i = 2; i < nvar; i++)
      tab_text (t, nvar - i - 1, 0, TAB_RIGHT | TAT_TITLE,
		x->vars[i]->label ? x->vars[i]->label : x->vars[i]->name);
  tab_box (t, TAL_2, TAL_2, -1, -1, 0, 0, tab_nc (t) - 1, tab_nr (t) - 1);
  tab_box (t, -1, -1, -1, TAL_1, tab_l (t), tab_t (t) - 1, tab_nc (t) - 1,
	   tab_nr (t) - 1);
  tab_box (t, -1, -1, -1, TAL_GAP, 0, tab_t (t), tab_l (t) - 1,
	   tab_nr (t) - 1);
  tab_vline (t, TAL_2, tab_l (t), 0, tab_nr (t) - 1);
  tab_dim (t, crosstabs_dim);
  tab_submit (t);
}

/* Sets the widths of all the columns and heights of all the rows in
   table T for driver D. */
static void
crosstabs_dim (struct tab_table *t, struct outp_driver *d)
{
  int i;
  
  /* Width of a numerical column. */
  int c = outp_string_width (d, "0.000000", OUTP_PROPORTIONAL);
  if (cmd.miss == CRS_REPORT)
    c += outp_string_width (d, "M", OUTP_PROPORTIONAL);

  /* Set width for header columns. */
  if (t->l != 0)
    {
      size_t i;
      int w;

      w = d->width - c * (t->nc - t->l);
      for (i = 0; i <= t->nc; i++)
        w -= t->wrv[i];
      w /= t->l;
      
      if (w < d->prop_em_width * 8)
	w = d->prop_em_width * 8;

      if (w > d->prop_em_width * 15)
	w = d->prop_em_width * 15;

      for (i = 0; i < t->l; i++)
	t->w[i] = w;
    }

  for (i = t->l; i < t->nc; i++)
    t->w[i] = c;

  for (i = 0; i < t->nr; i++)
    t->h[i] = tab_natural_height (t, d, i);
}

static struct table_entry **find_pivot_extent_general (struct table_entry **tp,
						int *cnt, int pivot);
static struct table_entry **find_pivot_extent_integer (struct table_entry **tp,
						int *cnt, int pivot);

/* Calls find_pivot_extent_general or find_pivot_extent_integer, as
   appropriate. */
static struct table_entry **
find_pivot_extent (struct table_entry **tp, int *cnt, int pivot)
{
  return (mode == GENERAL
	  ? find_pivot_extent_general (tp, cnt, pivot)
	  : find_pivot_extent_integer (tp, cnt, pivot));
}

/* Find the extent of a region in TP that contains one table.  If
   PIVOT != 0 that means a set of table entries with identical table
   number; otherwise they also have to have the same values for every
   dimension after the row and column dimensions.  The table that is
   searched starts at TP and has length CNT.  Returns the first entry
   after the last one in the table; sets *CNT to the number of
   remaining values.  If there are no entries in TP at all, returns
   NULL.  A yucky interface, admittedly, but it works. */
static struct table_entry **
find_pivot_extent_general (struct table_entry **tp, int *cnt, int pivot)
{
  struct table_entry *fp = *tp;
  struct crosstab *x;

  if (*cnt == 0)
    return NULL;
  x = xtab[(*tp)->table];
  for (;;)
    {
      tp++;
      if (--*cnt == 0)
	break;
      assert (*cnt > 0);

      if ((*tp)->table != fp->table)
	break;
      if (pivot)
	continue;

      if (memcmp (&(*tp)->values[2], &fp->values[2], sizeof (union value) * (x->nvar - 2)))
	break;
    }

  return tp;
}

/* Integer mode correspondent to find_pivot_extent_general().  This
   could be optimized somewhat, but I just don't give a crap about
   CROSSTABS performance in integer mode, which is just a
   CROSSTABS wart as far as I'm concerned.

   That said, feel free to send optimization patches to me. */
static struct table_entry **
find_pivot_extent_integer (struct table_entry **tp, int *cnt, int pivot)
{
  struct table_entry *fp = *tp;
  struct crosstab *x;

  if (*cnt == 0)
    return NULL;
  x = xtab[(*tp)->table];
  for (;;)
    {
      tp++;
      if (--*cnt == 0)
	break;
      assert (*cnt > 0);

      if ((*tp)->table != fp->table)
	break;
      if (pivot)
	continue;
      
      if (memcmp (&(*tp)->values[2], &fp->values[2],
                  sizeof (union value) * (x->nvar - 2)))
	break;
    }

  return tp;
}

/* Compares `union value's A_ and B_ and returns a strcmp()-like
   result.  WIDTH_ points to an int which is either 0 for a
   numeric value or a string width for a string value. */
static int
compare_value (const void *a_, const void *b_, void *width_)
{
  const union value *a = a_;
  const union value *b = b_;
  const int *pwidth = width_;
  const int width = *pwidth;

  if (width == 0)
    return (a->f < b->f) ? -1 : (a->f > b->f);
  else
    return strncmp (a->s, b->s, width);
}

/* Given an array of ENTRY_CNT table_entry structures starting at
   ENTRIES, creates a sorted list of the values that the variable
   with index VAR_IDX takes on.  The values are returned as a
   malloc()'darray stored in *VALUES, with the number of values
   stored in *VALUE_CNT.
   */
static void 
enum_var_values (struct table_entry **entries, int entry_cnt, int var_idx,
                 union value **values, int *value_cnt)
{
  struct variable *v = xtab[(*entries)->table]->vars[var_idx];

  if (mode == GENERAL)
    {
      int width = v->width;
      int i;

      *values = xnmalloc (entry_cnt, sizeof **values);
      for (i = 0; i < entry_cnt; i++)
        (*values)[i] = entries[i]->values[var_idx];
      *value_cnt = sort_unique (*values, entry_cnt, sizeof **values,
                                compare_value, &width);
    }
  else
    {
      struct var_range *vr = get_var_range (v);
      int i;
      
      assert (mode == INTEGER);
      *values = xnmalloc (vr->count, sizeof **values);
      for (i = 0; i < vr->count; i++)
	(*values)[i].f = i + vr->min;
      *value_cnt = vr->count;
    }
}

/* Sets cell (C,R) in TABLE, with options OPT, to have a value taken
   from V, displayed with print format spec from variable VAR.  When
   in REPORT missing-value mode, missing values have an M appended. */
static void
table_value_missing (struct tab_table *table, int c, int r, unsigned char opt,
		     const union value *v, const struct variable *var)
{
  struct substring s;

  const char *label = val_labs_find (var->val_labs, *v);
  if (label) 
    {
      tab_text (table, c, r, TAB_LEFT, label);
      return;
    }

  s.string = tab_alloc (table, var->print.w);
  format_short (s.string, &var->print, v);
  s.length = strlen (s.string);
  if (cmd.miss == CRS_REPORT && mv_is_num_user_missing (&var->miss, v->f))
    s.string[s.length++] = 'M';
  while (s.length && *s.string == ' ')
    {
      s.length--;
      s.string++;
    }
  tab_raw (table, c, r, opt, &s);
}

/* Draws a line across TABLE at the current row to indicate the most
   major dimension variable with index FIRST_DIFFERENCE out of NVAR
   that changed, and puts the values that changed into the table.  TB
   and X must be the corresponding table_entry and crosstab,
   respectively. */
static void
display_dimensions (struct tab_table *table, int first_difference, struct table_entry *tb)
{
  tab_hline (table, TAL_1, nvar - first_difference - 1, tab_nc (table) - 1, 0);

  for (; first_difference >= 2; first_difference--)
    table_value_missing (table, nvar - first_difference - 1, 0,
			 TAB_RIGHT, &tb->values[first_difference],
			 x->vars[first_difference]);
}

/* Put VALUE into cell (C,R) of TABLE, suffixed with character
   SUFFIX if nonzero.  If MARK_MISSING is nonzero the entry is
   additionally suffixed with a letter `M'. */
static void
format_cell_entry (struct tab_table *table, int c, int r, double value,
                   char suffix, int mark_missing)
{
  const struct fmt_spec f = {FMT_F, 10, 1};
  union value v;
  struct substring s;
  
  s.length = 10;
  s.string = tab_alloc (table, 16);
  v.f = value;
  data_out (s.string, &f, &v);
  while (*s.string == ' ')
    {
      s.length--;
      s.string++;
    }
  if (suffix != 0)
    s.string[s.length++] = suffix;
  if (mark_missing)
    s.string[s.length++] = 'M';

  tab_raw (table, c, r, TAB_RIGHT, &s);
}

/* Displays the crosstabulation table. */
static void
display_crosstabulation (void)
{
  {
    int r;
	
    for (r = 0; r < n_rows; r++)
      table_value_missing (table, nvar - 2, r * num_cells,
			   TAB_RIGHT, &rows[r], x->vars[ROW_VAR]);
  }
  tab_text (table, nvar - 2, n_rows * num_cells,
	    TAB_LEFT, _("Total"));
      
  /* Put in the actual cells. */
  {
    double *mp = mat;
    int r, c, i;

    tab_offset (table, nvar - 1, -1);
    for (r = 0; r < n_rows; r++)
      {
	if (num_cells > 1)
	  tab_hline (table, TAL_1, -1, n_cols, 0);
	for (c = 0; c < n_cols; c++)
	  {
            int mark_missing = 0;
            double expected_value = row_tot[r] * col_tot[c] / W;
            if (cmd.miss == CRS_REPORT
                && (mv_is_num_user_missing (&x->vars[COL_VAR]->miss, cols[c].f)
                    || mv_is_num_user_missing (&x->vars[ROW_VAR]->miss,
                                               rows[r].f)))
              mark_missing = 1;
	    for (i = 0; i < num_cells; i++)
	      {
		double v;
                int suffix = 0;

		switch (cells[i])
		  {
		  case CRS_CL_COUNT:
		    v = *mp;
		    break;
		  case CRS_CL_ROW:
		    v = *mp / row_tot[r] * 100.;
                    suffix = '%';
		    break;
		  case CRS_CL_COLUMN:
		    v = *mp / col_tot[c] * 100.;
                    suffix = '%';
		    break;
		  case CRS_CL_TOTAL:
		    v = *mp / W * 100.;
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
				 * (1. - row_tot[r] / W)
				 * (1. - col_tot[c] / W)));
		    break;
		  default:
		    assert (0);
                    abort ();
		  }

                format_cell_entry (table, c, i, v, suffix, mark_missing);
	      }

	    mp++;
	  }

	tab_offset (table, -1, tab_row (table) + num_cells);
      }
  }

  /* Row totals. */
  {
    int r, i;

    tab_offset (table, -1, tab_row (table) - num_cells * n_rows);
    for (r = 0; r < n_rows; r++) 
      {
        char suffix = 0;
        int mark_missing = 0;

        if (cmd.miss == CRS_REPORT
            && mv_is_num_user_missing (&x->vars[ROW_VAR]->miss, rows[r].f))
          mark_missing = 1;

        for (i = 0; i < num_cells; i++)
          {
            double v;

            switch (cells[i])
              {
              case CRS_CL_COUNT:
                v = row_tot[r];
                break;
              case CRS_CL_ROW:
                v = 100.;
                suffix = '%';
                break;
              case CRS_CL_COLUMN:
                v = row_tot[r] / W * 100.;
                suffix = '%';
                break;
              case CRS_CL_TOTAL:
                v = row_tot[r] / W * 100.;
                suffix = '%';
                break;
              case CRS_CL_EXPECTED:
              case CRS_CL_RESIDUAL:
              case CRS_CL_SRESIDUAL:
              case CRS_CL_ASRESIDUAL:
                v = 0.;
                break;
              default:
                assert (0);
                abort ();
              }

            format_cell_entry (table, n_cols, 0, v, suffix, mark_missing);
            tab_next_row (table);
          } 
      }
  }

  /* Column totals, grand total. */
  {
    int c;
    int last_row = 0;

    if (num_cells > 1)
      tab_hline (table, TAL_1, -1, n_cols, 0);
    for (c = 0; c <= n_cols; c++)
      {
	double ct = c < n_cols ? col_tot[c] : W;
        int mark_missing = 0;
        char suffix = 0;
        int i;
	    
        if (cmd.miss == CRS_REPORT && c < n_cols 
            && mv_is_num_user_missing (&x->vars[COL_VAR]->miss, cols[c].f))
          mark_missing = 1;

        for (i = 0; i < num_cells; i++)
	  {
	    double v;

	    switch (cells[i])
	      {
	      case CRS_CL_COUNT:
		v = ct;
                suffix = '%';
		break;
	      case CRS_CL_ROW:
		v = ct / W * 100.;
                suffix = '%';
		break;
	      case CRS_CL_COLUMN:
		v = 100.;
                suffix = '%';
		break;
	      case CRS_CL_TOTAL:
		v = ct / W * 100.;
                suffix = '%';
		break;
	      case CRS_CL_EXPECTED:
	      case CRS_CL_RESIDUAL:
	      case CRS_CL_SRESIDUAL:
	      case CRS_CL_ASRESIDUAL:
		continue;
	      default:
		assert (0);
                abort ();
	      }

            format_cell_entry (table, c, i, v, suffix, mark_missing);
	  }
        last_row = i;
      }

    tab_offset (table, -1, tab_row (table) + last_row);
  }
  
  tab_offset (table, 0, -1);
}

static void calc_r (double *X, double *Y, double *, double *, double *);
static void calc_chisq (double[N_CHISQ], int[N_CHISQ], double *, double *);

/* Display chi-square statistics. */
static void
display_chisq (void)
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
  int s = 0;

  int i;
	      
  calc_chisq (chisq_v, df, &fisher1, &fisher2);

  tab_offset (chisq, nvar - 2, -1);
  
  for (i = 0; i < N_CHISQ; i++)
    {
      if ((i != 2 && chisq_v[i] == SYSMIS)
	  || (i == 2 && fisher1 == SYSMIS))
	continue;
      s = 1;
      
      tab_text (chisq, 0, 0, TAB_LEFT, gettext (chisq_stats[i]));
      if (i != 2)
	{
	  tab_float (chisq, 1, 0, TAB_RIGHT, chisq_v[i], 8, 3);
	  tab_float (chisq, 2, 0, TAB_RIGHT, df[i], 8, 0);
	  tab_float (chisq, 3, 0, TAB_RIGHT,
		     gsl_cdf_chisq_Q (chisq_v[i], df[i]), 8, 3);
	}
      else
	{
	  chisq_fisher = 1;
	  tab_float (chisq, 4, 0, TAB_RIGHT, fisher2, 8, 3);
	  tab_float (chisq, 5, 0, TAB_RIGHT, fisher1, 8, 3);
	}
      tab_next_row (chisq);
    }

  tab_text (chisq, 0, 0, TAB_LEFT, _("N of Valid Cases"));
  tab_float (chisq, 1, 0, TAB_RIGHT, W, 8, 0);
  tab_next_row (chisq);
    
  tab_offset (chisq, 0, -1);
}

static int calc_symmetric (double[N_SYMMETRIC], double[N_SYMMETRIC],
			   double[N_SYMMETRIC]);

/* Display symmetric measures. */
static void
display_symmetric (void)
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
  int i;

  if (!calc_symmetric (sym_v, sym_ase, sym_t))
    return;

  tab_offset (sym, nvar - 2, -1);
  
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
      tab_float (sym, 2, 0, TAB_RIGHT, sym_v[i], 8, 3);
      if (sym_ase[i] != SYSMIS)
	tab_float (sym, 3, 0, TAB_RIGHT, sym_ase[i], 8, 3);
      if (sym_t[i] != SYSMIS)
	tab_float (sym, 4, 0, TAB_RIGHT, sym_t[i], 8, 3);
      /*tab_float (sym, 5, 0, TAB_RIGHT, normal_sig (sym_v[i]), 8, 3);*/
      tab_next_row (sym);
    }

  tab_text (sym, 0, 0, TAB_LEFT, _("N of Valid Cases"));
  tab_float (sym, 2, 0, TAB_RIGHT, W, 8, 0);
  tab_next_row (sym);
    
  tab_offset (sym, 0, -1);
}

static int calc_risk (double[], double[], double[], union value *);

/* Display risk estimate. */
static void
display_risk (void)
{
  char buf[256];
  double risk_v[3], lower[3], upper[3];
  union value c[2];
  int i;
  
  if (!calc_risk (risk_v, upper, lower, c))
    return;
  
  tab_offset (risk, nvar - 2, -1);
  
  for (i = 0; i < 3; i++)
    {
      if (risk_v[i] == SYSMIS)
	continue;

      switch (i)
	{
	case 0:
	  if (x->vars[COL_VAR]->type == NUMERIC)
	    sprintf (buf, _("Odds Ratio for %s (%g / %g)"),
		     x->vars[COL_VAR]->name, c[0].f, c[1].f);
	  else
	    sprintf (buf, _("Odds Ratio for %s (%.*s / %.*s)"),
		     x->vars[COL_VAR]->name,
		     x->vars[COL_VAR]->width, c[0].s,
		     x->vars[COL_VAR]->width, c[1].s);
	  break;
	case 1:
	case 2:
	  if (x->vars[ROW_VAR]->type == NUMERIC)
	    sprintf (buf, _("For cohort %s = %g"),
		     x->vars[ROW_VAR]->name, rows[i - 1].f);
	  else
	    sprintf (buf, _("For cohort %s = %.*s"),
		     x->vars[ROW_VAR]->name,
		     x->vars[ROW_VAR]->width, rows[i - 1].s);
	  break;
	}
		   
      tab_text (risk, 0, 0, TAB_LEFT, buf);
      tab_float (risk, 1, 0, TAB_RIGHT, risk_v[i], 8, 3);
      tab_float (risk, 2, 0, TAB_RIGHT, lower[i], 8, 3);
      tab_float (risk, 3, 0, TAB_RIGHT, upper[i], 8, 3);
      tab_next_row (risk);
    }

  tab_text (risk, 0, 0, TAB_LEFT, _("N of Valid Cases"));
  tab_float (risk, 1, 0, TAB_RIGHT, W, 8, 0);
  tab_next_row (risk);
    
  tab_offset (risk, 0, -1);
}

static int calc_directional (double[N_DIRECTIONAL], double[N_DIRECTIONAL],
			     double[N_DIRECTIONAL]);

/* Display directional measures. */
static void
display_directional (void)
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

  if (!calc_directional (direct_v, direct_ase, direct_t))
    return;

  tab_offset (direct, nvar - 2, -1);
  
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
		  char *string;
		  int k = last[j] = stats_lookup[j][i];

		  if (k == 0)
		    string = NULL;
		  else if (k == 1)
		    string = x->vars[0]->name;
		  else
		    string = x->vars[1]->name;
		  
		  tab_text (direct, j, 0, TAB_LEFT | TAT_PRINTF,
			    gettext (stats_names[j][k]), string);
		}
	    }
      }
      
      tab_float (direct, 3, 0, TAB_RIGHT, direct_v[i], 8, 3);
      if (direct_ase[i] != SYSMIS)
	tab_float (direct, 4, 0, TAB_RIGHT, direct_ase[i], 8, 3);
      if (direct_t[i] != SYSMIS)
	tab_float (direct, 5, 0, TAB_RIGHT, direct_t[i], 8, 3);
      /*tab_float (direct, 6, 0, TAB_RIGHT, normal_sig (direct_v[i]), 8, 3);*/
      tab_next_row (direct);
    }

  tab_offset (direct, 0, -1);
}

/* Statistical calculations. */

/* Returns the value of the gamma (factorial) function for an integer
   argument X. */
static double
gamma_int (double x)
{
  double r = 1;
  int i;
  
  for (i = 2; i < x; i++)
    r *= i;
  return r;
}

/* Calculate P_r as specified in _SPSS Statistical Algorithms_,
   Appendix 5. */
static inline double
Pr (int a, int b, int c, int d)
{
  return (gamma_int (a + b + 1.) / gamma_int (a + 1.)
	  * gamma_int (c + d + 1.) / gamma_int (b + 1.)
	  * gamma_int (a + c + 1.) / gamma_int (c + 1.)
	  * gamma_int (b + d + 1.) / gamma_int (d + 1.)
	  / gamma_int (a + b + c + d + 1.));
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
  int x;
  
  if (min (c, d) < min (a, b))
    swap (&a, &c), swap (&b, &d);
  if (min (b, d) < min (a, c))
    swap (&a, &b), swap (&c, &d);
  if (b * c < a * d)
    {
      if (b < c)
	swap (&a, &b), swap (&c, &d);
      else
	swap (&a, &c), swap (&b, &d);
    }

  *fisher1 = 0.;
  for (x = 0; x <= a; x++)
    *fisher1 += Pr (a - x, b + x, c + x, d - x);

  *fisher2 = *fisher1;
  for (x = 1; x <= b; x++)
    *fisher2 += Pr (a + x, b - x, c - x, d + x);
}

/* Calculates chi-squares into CHISQ.  MAT is a matrix with N_COLS
   columns with values COLS and N_ROWS rows with values ROWS.  Values
   in the matrix sum to W. */
static void
calc_chisq (double chisq[N_CHISQ], int df[N_CHISQ],
	    double *fisher1, double *fisher2)
{
  int r, c;

  chisq[0] = chisq[1] = 0.;
  chisq[2] = chisq[3] = chisq[4] = SYSMIS;
  *fisher1 = *fisher2 = SYSMIS;

  df[0] = df[1] = (ns_cols - 1) * (ns_rows - 1);

  if (ns_rows <= 1 || ns_cols <= 1)
    {
      chisq[0] = chisq[1] = SYSMIS;
      return;
    }

  for (r = 0; r < n_rows; r++)
    for (c = 0; c < n_cols; c++)
      {
	const double expected = row_tot[r] * col_tot[c] / W;
	const double freq = mat[n_cols * r + c];
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
  if (ns_cols == 2 && ns_rows == 2)
    {
      double f11, f12, f21, f22;
      
      {
	int nz_cols[2];
	int i, j;

	for (i = j = 0; i < n_cols; i++)
	  if (col_tot[i] != 0.)
	    {
	      nz_cols[j++] = i;
	      if (j == 2)
		break;
	    }

	assert (j == 2);

	f11 = mat[nz_cols[0]];
	f12 = mat[nz_cols[1]];
	f21 = mat[nz_cols[0] + n_cols];
	f22 = mat[nz_cols[1] + n_cols];
      }

      /* Yates. */
      {
	const double x = fabs (f11 * f22 - f12 * f21) - 0.5 * W;

	if (x > 0.)
	  chisq[3] = (W * x * x
		      / (f11 + f12) / (f21 + f22)
		      / (f11 + f21) / (f12 + f22));
	else
	  chisq[3] = 0.;

	df[3] = 1.;
      }

      /* Fisher. */
      if (f11 < 5. || f12 < 5. || f21 < 5. || f22 < 5.)
	calc_fisher (f11 + .5, f12 + .5, f21 + .5, f22 + .5, fisher1, fisher2);
    }

  /* Calculate Mantel-Haenszel. */
  if (x->vars[ROW_VAR]->type == NUMERIC && x->vars[COL_VAR]->type == NUMERIC)
    {
      double r, ase_0, ase_1;
      calc_r ((double *) rows, (double *) cols, &r, &ase_0, &ase_1);
    
      chisq[4] = (W - 1.) * r * r;
      df[4] = 1;
    }
}

/* Calculate the value of Pearson's r.  r is stored into R, ase_1 into
   ASE_1, and ase_0 into ASE_0.  The row and column values must be
   passed in X and Y. */
static void
calc_r (double *X, double *Y, double *r, double *ase_0, double *ase_1)
{
  double SX, SY, S, T;
  double Xbar, Ybar;
  double sum_XYf, sum_X2Y2f;
  double sum_Xr, sum_X2r;
  double sum_Yc, sum_Y2c;
  int i, j;

  for (sum_X2Y2f = sum_XYf = 0., i = 0; i < n_rows; i++)
    for (j = 0; j < n_cols; j++)
      {
	double fij = mat[j + i * n_cols];
	double product = X[i] * Y[j];
	double temp = fij * product;
	sum_XYf += temp;
	sum_X2Y2f += temp * product;
      }

  for (sum_Xr = sum_X2r = 0., i = 0; i < n_rows; i++)
    {
      sum_Xr += X[i] * row_tot[i];
      sum_X2r += X[i] * X[i] * row_tot[i];
    }
  Xbar = sum_Xr / W;

  for (sum_Yc = sum_Y2c = 0., i = 0; i < n_cols; i++)
    {
      sum_Yc += Y[i] * col_tot[i];
      sum_Y2c += Y[i] * Y[i] * col_tot[i];
    }
  Ybar = sum_Yc / W;

  S = sum_XYf - sum_Xr * sum_Yc / W;
  SX = sum_X2r - sum_Xr * sum_Xr / W;
  SY = sum_Y2c - sum_Yc * sum_Yc / W;
  T = sqrt (SX * SY);
  *r = S / T;
  *ase_0 = sqrt ((sum_X2Y2f - (sum_XYf * sum_XYf) / W) / (sum_X2r * sum_Y2c));
  
  {
    double s, c, y, t;
    
    for (s = c = 0., i = 0; i < n_rows; i++)
      for (j = 0; j < n_cols; j++)
	{
	  double Xresid, Yresid;
	  double temp;

	  Xresid = X[i] - Xbar;
	  Yresid = Y[j] - Ybar;
	  temp = (T * Xresid * Yresid
		  - ((S / (2. * T))
		     * (Xresid * Xresid * SY + Yresid * Yresid * SX)));
	  y = mat[j + i * n_cols] * temp * temp - c;
	  t = s + y;
	  c = (t - s) - y;
	  s = t;
	}
    *ase_1 = sqrt (s) / (T * T);
  }
}

static double somers_d_v[3];
static double somers_d_ase[3];
static double somers_d_t[3];

/* Calculate symmetric statistics and their asymptotic standard
   errors.  Returns 0 if none could be calculated. */
static int
calc_symmetric (double v[N_SYMMETRIC], double ase[N_SYMMETRIC],
		double t[N_SYMMETRIC])
{
  int q = min (ns_rows, ns_cols);
  
  if (q <= 1)
    return 0;
  
  {
    int i;

    if (v) 
      for (i = 0; i < N_SYMMETRIC; i++)
	v[i] = ase[i] = t[i] = SYSMIS;
  }

  /* Phi, Cramer's V, contingency coefficient. */
  if (cmd.a_statistics[CRS_ST_PHI] || cmd.a_statistics[CRS_ST_CC])
    {
      double Xp = 0.;	/* Pearson chi-square. */

      {
	int r, c;
    
	for (r = 0; r < n_rows; r++)
	  for (c = 0; c < n_cols; c++)
	    {
	      const double expected = row_tot[r] * col_tot[c] / W;
	      const double freq = mat[n_cols * r + c];
	      const double residual = freq - expected;
    
              Xp += residual * residual / expected;
	    }
      }

      if (cmd.a_statistics[CRS_ST_PHI])
	{
	  v[0] = sqrt (Xp / W);
	  v[1] = sqrt (Xp / (W * (q - 1)));
	}
      if (cmd.a_statistics[CRS_ST_CC])
	v[2] = sqrt (Xp / (Xp + W));
    }
  
  if (cmd.a_statistics[CRS_ST_BTAU] || cmd.a_statistics[CRS_ST_CTAU]
      || cmd.a_statistics[CRS_ST_GAMMA] || cmd.a_statistics[CRS_ST_D])
    {
      double *cum;
      double Dr, Dc;
      double P, Q;
      double btau_cum, ctau_cum, gamma_cum, d_yx_cum, d_xy_cum;
      double btau_var;
      
      {
	int r, c;
	
	Dr = Dc = W * W;
	for (r = 0; r < n_rows; r++)
	  Dr -= row_tot[r] * row_tot[r];
	for (c = 0; c < n_cols; c++)
	  Dc -= col_tot[c] * col_tot[c];
      }
      
      {
	int r, c;

	cum = xnmalloc (n_cols * n_rows, sizeof *cum);
	for (c = 0; c < n_cols; c++)
	  {
	    double ct = 0.;
	    
	    for (r = 0; r < n_rows; r++)
	      cum[c + r * n_cols] = ct += mat[c + r * n_cols];
	  }
      }
      
      /* P and Q. */
      {
	int i, j;
	double Cij, Dij;

	P = Q = 0.;
	for (i = 0; i < n_rows; i++)
	  {
	    Cij = Dij = 0.;

	    for (j = 1; j < n_cols; j++)
	      Cij += col_tot[j] - cum[j + i * n_cols];

	    if (i > 0)
	      for (j = 1; j < n_cols; j++)
		Dij += cum[j + (i - 1) * n_cols];

	    for (j = 0;;)
	      {
		double fij = mat[j + i * n_cols];
		P += fij * Cij;
		Q += fij * Dij;
		
		if (++j == n_cols)
		  break;
		assert (j < n_cols);

		Cij -= col_tot[j] - cum[j + i * n_cols];
		Dij += col_tot[j - 1] - cum[j - 1 + i * n_cols];
		
		if (i > 0)
		  {
		    Cij += cum[j - 1 + (i - 1) * n_cols];
		    Dij -= cum[j + (i - 1) * n_cols];
		  }
	      }
	  }
      }

      if (cmd.a_statistics[CRS_ST_BTAU])
	v[3] = (P - Q) / sqrt (Dr * Dc);
      if (cmd.a_statistics[CRS_ST_CTAU])
	v[4] = (q * (P - Q)) / ((W * W) * (q - 1));
      if (cmd.a_statistics[CRS_ST_GAMMA])
	v[5] = (P - Q) / (P + Q);

      /* ASE for tau-b, tau-c, gamma.  Calculations could be
	 eliminated here, at expense of memory.  */
      {
	int i, j;
	double Cij, Dij;

	btau_cum = ctau_cum = gamma_cum = d_yx_cum = d_xy_cum = 0.;
	for (i = 0; i < n_rows; i++)
	  {
	    Cij = Dij = 0.;

	    for (j = 1; j < n_cols; j++)
	      Cij += col_tot[j] - cum[j + i * n_cols];

	    if (i > 0)
	      for (j = 1; j < n_cols; j++)
		Dij += cum[j + (i - 1) * n_cols];

	    for (j = 0;;)
	      {
		double fij = mat[j + i * n_cols];

		if (cmd.a_statistics[CRS_ST_BTAU])
		  {
		    const double temp = (2. * sqrt (Dr * Dc) * (Cij - Dij)
					 + v[3] * (row_tot[i] * Dc
						   + col_tot[j] * Dr));
		    btau_cum += fij * temp * temp;
		  }
		
		{
		  const double temp = Cij - Dij;
		  ctau_cum += fij * temp * temp;
		}

		if (cmd.a_statistics[CRS_ST_GAMMA])
		  {
		    const double temp = Q * Cij - P * Dij;
		    gamma_cum += fij * temp * temp;
		  }

		if (cmd.a_statistics[CRS_ST_D])
		  {
		    d_yx_cum += fij * pow2 (Dr * (Cij - Dij)
                                            - (P - Q) * (W - row_tot[i]));
		    d_xy_cum += fij * pow2 (Dc * (Dij - Cij)
                                            - (Q - P) * (W - col_tot[j]));
		  }
		
		if (++j == n_cols)
		  break;
		assert (j < n_cols);

		Cij -= col_tot[j] - cum[j + i * n_cols];
		Dij += col_tot[j - 1] - cum[j - 1 + i * n_cols];
		
		if (i > 0)
		  {
		    Cij += cum[j - 1 + (i - 1) * n_cols];
		    Dij -= cum[j + (i - 1) * n_cols];
		  }
	      }
	  }
      }

      btau_var = ((btau_cum
		   - (W * pow2 (W * (P - Q) / sqrt (Dr * Dc) * (Dr + Dc))))
		  / pow2 (Dr * Dc));
      if (cmd.a_statistics[CRS_ST_BTAU])
	{
	  ase[3] = sqrt (btau_var);
	  t[3] = v[3] / (2 * sqrt ((ctau_cum - (P - Q) * (P - Q) / W)
				   / (Dr * Dc)));
	}
      if (cmd.a_statistics[CRS_ST_CTAU])
	{
	  ase[4] = ((2 * q / ((q - 1) * W * W))
		    * sqrt (ctau_cum - (P - Q) * (P - Q) / W));
	  t[4] = v[4] / ase[4];
	}
      if (cmd.a_statistics[CRS_ST_GAMMA])
	{
	  ase[5] = ((4. / ((P + Q) * (P + Q))) * sqrt (gamma_cum));
	  t[5] = v[5] / (2. / (P + Q)
			 * sqrt (ctau_cum - (P - Q) * (P - Q) / W));
	}
      if (cmd.a_statistics[CRS_ST_D])
	{
	  somers_d_v[0] = (P - Q) / (.5 * (Dc + Dr));
	  somers_d_ase[0] = 2. * btau_var / (Dr + Dc) * sqrt (Dr * Dc);
	  somers_d_t[0] = (somers_d_v[0]
			   / (4 / (Dc + Dr)
			      * sqrt (ctau_cum - pow2 (P - Q) / W)));
	  somers_d_v[1] = (P - Q) / Dc;
	  somers_d_ase[1] = 2. / pow2 (Dc) * sqrt (d_xy_cum);
	  somers_d_t[1] = (somers_d_v[1]
			   / (2. / Dc
			      * sqrt (ctau_cum - pow2 (P - Q) / W)));
	  somers_d_v[2] = (P - Q) / Dr;
	  somers_d_ase[2] = 2. / pow2 (Dr) * sqrt (d_yx_cum);
	  somers_d_t[2] = (somers_d_v[2]
			   / (2. / Dr
			      * sqrt (ctau_cum - pow2 (P - Q) / W)));
	}

      free (cum);
    }

  /* Spearman correlation, Pearson's r. */
  if (cmd.a_statistics[CRS_ST_CORR])
    {
      double *R = local_alloc (sizeof *R * n_rows);
      double *C = local_alloc (sizeof *C * n_cols);
      
      {
	double y, t, c = 0., s = 0.;
	int i = 0;
	
	for (;;)
	  {
	    R[i] = s + (row_tot[i] + 1.) / 2.;
	    y = row_tot[i] - c;
	    t = s + y;
	    c = (t - s) - y;
	    s = t;
	    if (++i == n_rows)
	      break;
	    assert (i < n_rows);
	  }
      }
      
      {
	double y, t, c = 0., s = 0.;
	int j = 0;
	
	for (;;)
	  {
	    C[j] = s + (col_tot[j] + 1.) / 2;
	    y = col_tot[j] - c;
	    t = s + y;
	    c = (t - s) - y;
	    s = t;
	    if (++j == n_cols)
	      break;
	    assert (j < n_cols);
	  }
      }
      
      calc_r (R, C, &v[6], &t[6], &ase[6]);
      t[6] = v[6] / t[6];

      local_free (R);
      local_free (C);

      calc_r ((double *) rows, (double *) cols, &v[7], &t[7], &ase[7]);
      t[7] = v[7] / t[7];
    }

  /* Cohen's kappa. */
  if (cmd.a_statistics[CRS_ST_KAPPA] && ns_rows == ns_cols)
    {
      double sum_fii, sum_rici, sum_fiiri_ci, sum_fijri_ci2, sum_riciri_ci;
      int i, j;
      
      for (sum_fii = sum_rici = sum_fiiri_ci = sum_riciri_ci = 0., i = j = 0;
	   i < ns_rows; i++, j++)
	{
	  double prod, sum;
	  
	  while (col_tot[j] == 0.)
	    j++;
	  
	  prod = row_tot[i] * col_tot[j];
	  sum = row_tot[i] + col_tot[j];
	  
	  sum_fii += mat[j + i * n_cols];
	  sum_rici += prod;
	  sum_fiiri_ci += mat[j + i * n_cols] * sum;
	  sum_riciri_ci += prod * sum;
	}
      for (sum_fijri_ci2 = 0., i = 0; i < ns_rows; i++)
	for (j = 0; j < ns_cols; j++)
	  {
	    double sum = row_tot[i] + col_tot[j];
	    sum_fijri_ci2 += mat[j + i * n_cols] * sum * sum;
	  }
      
      v[8] = (W * sum_fii - sum_rici) / (W * W - sum_rici);

      ase[8] = sqrt ((W * W * sum_rici
		      + sum_rici * sum_rici
		      - W * sum_riciri_ci)
		     / (W * (W * W - sum_rici) * (W * W - sum_rici)));
#if 0
      t[8] = v[8] / sqrt (W * (((sum_fii * (W - sum_fii))
				/ pow2 (W * W - sum_rici))
			       + ((2. * (W - sum_fii)
				   * (2. * sum_fii * sum_rici
				      - W * sum_fiiri_ci))
				  / cube (W * W - sum_rici))
			       + (pow2 (W - sum_fii)
				  * (W * sum_fijri_ci2 - 4.
				     * sum_rici * sum_rici)
				  / pow4 (W * W - sum_rici))));
#else
      t[8] = v[8] / ase[8];
#endif
    }

  return 1;
}

/* Calculate risk estimate. */
static int
calc_risk (double *value, double *upper, double *lower, union value *c)
{
  double f11, f12, f21, f22;
  double v;

  {
    int i;
      
    for (i = 0; i < 3; i++)
      value[i] = upper[i] = lower[i] = SYSMIS;
  }
    
  if (ns_rows != 2 || ns_cols != 2)
    return 0;
  
  {
    int nz_cols[2];
    int i, j;

    for (i = j = 0; i < n_cols; i++)
      if (col_tot[i] != 0.)
	{
	  nz_cols[j++] = i;
	  if (j == 2)
	    break;
	}

    assert (j == 2);

    f11 = mat[nz_cols[0]];
    f12 = mat[nz_cols[1]];
    f21 = mat[nz_cols[0] + n_cols];
    f22 = mat[nz_cols[1] + n_cols];

    c[0] = cols[nz_cols[0]];
    c[1] = cols[nz_cols[1]];
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
calc_directional (double v[N_DIRECTIONAL], double ase[N_DIRECTIONAL],
		  double t[N_DIRECTIONAL])
{
  {
    int i;

    for (i = 0; i < N_DIRECTIONAL; i++)
      v[i] = ase[i] = t[i] = SYSMIS;
  }

  /* Lambda. */
  if (cmd.a_statistics[CRS_ST_LAMBDA])
    {
      double *fim = xnmalloc (n_rows, sizeof *fim);
      int *fim_index = xnmalloc (n_rows, sizeof *fim_index);
      double *fmj = xnmalloc (n_cols, sizeof *fmj);
      int *fmj_index = xnmalloc (n_cols, sizeof *fmj_index);
      double sum_fim, sum_fmj;
      double rm, cm;
      int rm_index, cm_index;
      int i, j;

      /* Find maximum for each row and their sum. */
      for (sum_fim = 0., i = 0; i < n_rows; i++)
	{
	  double max = mat[i * n_cols];
	  int index = 0;

	  for (j = 1; j < n_cols; j++)
	    if (mat[j + i * n_cols] > max)
	      {
		max = mat[j + i * n_cols];
		index = j;
	      }
	
	  sum_fim += fim[i] = max;
	  fim_index[i] = index;
	}

      /* Find maximum for each column. */
      for (sum_fmj = 0., j = 0; j < n_cols; j++)
	{
	  double max = mat[j];
	  int index = 0;

	  for (i = 1; i < n_rows; i++)
	    if (mat[j + i * n_cols] > max)
	      {
		max = mat[j + i * n_cols];
		index = i;
	      }
	
	  sum_fmj += fmj[j] = max;
	  fmj_index[j] = index;
	}

      /* Find maximum row total. */
      rm = row_tot[0];
      rm_index = 0;
      for (i = 1; i < n_rows; i++)
	if (row_tot[i] > rm)
	  {
	    rm = row_tot[i];
	    rm_index = i;
	  }

      /* Find maximum column total. */
      cm = col_tot[0];
      cm_index = 0;
      for (j = 1; j < n_cols; j++)
	if (col_tot[j] > cm)
	  {
	    cm = col_tot[j];
	    cm_index = j;
	  }

      v[0] = (sum_fim + sum_fmj - cm - rm) / (2. * W - rm - cm);
      v[1] = (sum_fmj - rm) / (W - rm);
      v[2] = (sum_fim - cm) / (W - cm);

      /* ASE1 for Y given X. */
      {
	double accum;

	for (accum = 0., i = 0; i < n_rows; i++)
	  for (j = 0; j < n_cols; j++)
	    {
	      const int deltaj = j == cm_index;
	      accum += (mat[j + i * n_cols]
			* pow2 ((j == fim_index[i])
			       - deltaj
			       + v[0] * deltaj));
	    }
      
	ase[2] = sqrt (accum - W * v[0]) / (W - cm);
      }

      /* ASE0 for Y given X. */
      {
	double accum;
      
	for (accum = 0., i = 0; i < n_rows; i++)
	  if (cm_index != fim_index[i])
	    accum += (mat[i * n_cols + fim_index[i]]
		      + mat[i * n_cols + cm_index]);
	t[2] = v[2] / (sqrt (accum - pow2 (sum_fim - cm) / W) / (W - cm));
      }

      /* ASE1 for X given Y. */
      {
	double accum;

	for (accum = 0., i = 0; i < n_rows; i++)
	  for (j = 0; j < n_cols; j++)
	    {
	      const int deltaj = i == rm_index;
	      accum += (mat[j + i * n_cols]
			* pow2 ((i == fmj_index[j])
			       - deltaj
			       + v[0] * deltaj));
	    }
      
	ase[1] = sqrt (accum - W * v[0]) / (W - rm);
      }

      /* ASE0 for X given Y. */
      {
	double accum;
      
	for (accum = 0., j = 0; j < n_cols; j++)
	  if (rm_index != fmj_index[j])
	    accum += (mat[j + n_cols * fmj_index[j]]
		      + mat[j + n_cols * rm_index]);
	t[1] = v[1] / (sqrt (accum - pow2 (sum_fmj - rm) / W) / (W - rm));
      }

      /* Symmetric ASE0 and ASE1. */
      {
	double accum0;
	double accum1;

	for (accum0 = accum1 = 0., i = 0; i < n_rows; i++)
	  for (j = 0; j < n_cols; j++)
	    {
	      int temp0 = (fmj_index[j] == i) + (fim_index[i] == j);
	      int temp1 = (i == rm_index) + (j == cm_index);
	      accum0 += mat[j + i * n_cols] * pow2 (temp0 - temp1);
	      accum1 += (mat[j + i * n_cols]
			 * pow2 (temp0 + (v[0] - 1.) * temp1));
	    }
	ase[0] = sqrt (accum1 - 4. * W * v[0] * v[0]) / (2. * W - rm - cm);
	t[0] = v[0] / (sqrt (accum0 - pow2 ((sum_fim + sum_fmj - cm - rm) / W))
		       / (2. * W - rm - cm));
      }

      free (fim);
      free (fim_index);
      free (fmj);
      free (fmj_index);
      
      {
	double sum_fij2_ri, sum_fij2_ci;
	double sum_ri2, sum_cj2;

	for (sum_fij2_ri = sum_fij2_ci = 0., i = 0; i < n_rows; i++)
	  for (j = 0; j < n_cols; j++)
	    {
	      double temp = pow2 (mat[j + i * n_cols]);
	      sum_fij2_ri += temp / row_tot[i];
	      sum_fij2_ci += temp / col_tot[j];
	    }

	for (sum_ri2 = 0., i = 0; i < n_rows; i++)
	  sum_ri2 += row_tot[i] * row_tot[i];

	for (sum_cj2 = 0., j = 0; j < n_cols; j++)
	  sum_cj2 += col_tot[j] * col_tot[j];

	v[3] = (W * sum_fij2_ci - sum_ri2) / (W * W - sum_ri2);
	v[4] = (W * sum_fij2_ri - sum_cj2) / (W * W - sum_cj2);
      }
    }

  if (cmd.a_statistics[CRS_ST_UC])
    {
      double UX, UY, UXY, P;
      double ase1_yx, ase1_xy, ase1_sym;
      int i, j;

      for (UX = 0., i = 0; i < n_rows; i++)
	if (row_tot[i] > 0.)
	  UX -= row_tot[i] / W * log (row_tot[i] / W);
      
      for (UY = 0., j = 0; j < n_cols; j++)
	if (col_tot[j] > 0.)
	  UY -= col_tot[j] / W * log (col_tot[j] / W);

      for (UXY = P = 0., i = 0; i < n_rows; i++)
	for (j = 0; j < n_cols; j++)
	  {
	    double entry = mat[j + i * n_cols];

	    if (entry <= 0.)
	      continue;
	    
	    P += entry * pow2 (log (col_tot[j] * row_tot[i] / (W * entry)));
	    UXY -= entry / W * log (entry / W);
	  }

      for (ase1_yx = ase1_xy = ase1_sym = 0., i = 0; i < n_rows; i++)
	for (j = 0; j < n_cols; j++)
	  {
	    double entry = mat[j + i * n_cols];

	    if (entry <= 0.)
	      continue;
	    
	    ase1_yx += entry * pow2 (UY * log (entry / row_tot[i])
				    + (UX - UXY) * log (col_tot[j] / W));
	    ase1_xy += entry * pow2 (UX * log (entry / col_tot[j])
				    + (UY - UXY) * log (row_tot[i] / W));
	    ase1_sym += entry * pow2 ((UXY
				      * log (row_tot[i] * col_tot[j] / (W * W)))
				     - (UX + UY) * log (entry / W));
	  }
      
      v[5] = 2. * ((UX + UY - UXY) / (UX + UY));
      ase[5] = (2. / (W * pow2 (UX + UY))) * sqrt (ase1_sym);
      t[5] = v[5] / ((2. / (W * (UX + UY)))
		     * sqrt (P - pow2 (UX + UY - UXY) / W));
		    
      v[6] = (UX + UY - UXY) / UX;
      ase[6] = sqrt (ase1_xy) / (W * UX * UX);
      t[6] = v[6] / (sqrt (P - W * pow2 (UX + UY - UXY)) / (W * UX));
      
      v[7] = (UX + UY - UXY) / UY;
      ase[7] = sqrt (ase1_yx) / (W * UY * UY);
      t[7] = v[7] / (sqrt (P - W * pow2 (UX + UY - UXY)) / (W * UY));
    }

  /* Somers' D. */
  if (cmd.a_statistics[CRS_ST_D])
    {
      int i;
      
      if (!sym)
	calc_symmetric (NULL, NULL, NULL);
      for (i = 0; i < 3; i++)
	{
	  v[8 + i] = somers_d_v[i];
	  ase[8 + i] = somers_d_ase[i];
	  t[8 + i] = somers_d_t[i];
	}
    }

  /* Eta. */
  if (cmd.a_statistics[CRS_ST_ETA])
    {
      {
	double sum_Xr, sum_X2r;
	double SX, SXW;
	int i, j;
      
	for (sum_Xr = sum_X2r = 0., i = 0; i < n_rows; i++)
	  {
	    sum_Xr += rows[i].f * row_tot[i];
	    sum_X2r += rows[i].f * rows[i].f * row_tot[i];
	  }
	SX = sum_X2r - sum_Xr * sum_Xr / W;
      
	for (SXW = 0., j = 0; j < n_cols; j++)
	  {
	    double cum;

	    for (cum = 0., i = 0; i < n_rows; i++)
	      {
		SXW += rows[i].f * rows[i].f * mat[j + i * n_cols];
		cum += rows[i].f * mat[j + i * n_cols];
	      }

	    SXW -= cum * cum / col_tot[j];
	  }
	v[11] = sqrt (1. - SXW / SX);
      }

      {
	double sum_Yc, sum_Y2c;
	double SY, SYW;
	int i, j;

	for (sum_Yc = sum_Y2c = 0., i = 0; i < n_cols; i++)
	  {
	    sum_Yc += cols[i].f * col_tot[i];
	    sum_Y2c += cols[i].f * cols[i].f * col_tot[i];
	  }
	SY = sum_Y2c - sum_Yc * sum_Yc / W;

	for (SYW = 0., i = 0; i < n_rows; i++)
	  {
	    double cum;

	    for (cum = 0., j = 0; j < n_cols; j++)
	      {
		SYW += cols[j].f * cols[j].f * mat[j + i * n_cols];
		cum += cols[j].f * mat[j + i * n_cols];
	      }
	  
	    SYW -= cum * cum / row_tot[i];
	  }
	v[12] = sqrt (1. - SYW / SY);
      }
    }

  return 1;
}

/* A wrapper around data_out() that limits string output to short
   string width and null terminates the result. */
static void
format_short (char *s, const struct fmt_spec *fp, const union value *v)
{
  struct fmt_spec fmt_subst;

  /* Limit to short string width. */
  if (formats[fp->type].cat & FCAT_STRING) 
    {
      fmt_subst = *fp;

      assert (fmt_subst.type == FMT_A || fmt_subst.type == FMT_AHEX);
      if (fmt_subst.type == FMT_A)
        fmt_subst.w = min (8, fmt_subst.w);
      else
        fmt_subst.w = min (16, fmt_subst.w);

      fp = &fmt_subst;
    }

  /* Format. */
  data_out (s, fp, v);
  
  /* Null terminate. */
  s[fp->w] = '\0';
}

/* 
   Local Variables:
   mode: c
   End:
*/
