/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009, 2010 Free Software Foundation, Inc.

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

#include <config.h>

#include <data/case.h>
#include <data/casegrouper.h>
#include <data/casereader.h>

#include <math/covariance.h>
#include <math/categoricals.h>
#include <math/moments.h>
#include <gsl/gsl_matrix.h>
#include <linreg/sweep.h>

#include <libpspp/ll.h>

#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <language/lexer/value-parser.h>
#include <language/command.h>

#include <data/procedure.h>
#include <data/value.h>
#include <data/dictionary.h>

#include <language/dictionary/split-file.h>
#include <libpspp/hash.h>
#include <libpspp/taint.h>
#include <math/group-proc.h>
#include <math/levene.h>
#include <libpspp/misc.h>

#include <output/tab.h>

#include <gsl/gsl_cdf.h>
#include <math.h>
#include <data/format.h>

#include <libpspp/message.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

enum missing_type
  {
    MISS_LISTWISE,
    MISS_ANALYSIS,
  };

enum statistics
  {
    STATS_DESCRIPTIVES = 0x0001,
    STATS_HOMOGENEITY = 0x0002
  };

struct coeff_node
{
  struct ll ll; 
  double coeff; 
};


struct contrasts_node
{
  struct ll ll; 
  struct ll_list coefficient_list;

  bool bad_count; /* True if the number of coefficients does not equal the number of groups */
};

struct oneway_spec
{
  size_t n_vars;
  const struct variable **vars;

  const struct variable *indep_var;

  enum statistics stats;

  enum missing_type missing_type;
  enum mv_class exclude;

  /* List of contrasts */
  struct ll_list contrast_list;

  /* The weight variable */
  const struct variable *wv;

};

/* Per category data */
struct descriptive_data
{
  const struct variable *var;
  struct moments1 *mom;

  double minimum;
  double maximum;
};

/* Workspace variable for each dependent variable */
struct per_var_ws
{
  struct covariance *cov;

  double sst;
  double sse;
  double ssa;

  int n_groups;

  double mse;
};

struct oneway_workspace
{
  /* The number of distinct values of the independent variable, when all
     missing values are disregarded */
  int actual_number_of_groups;

  /* A  hash table containing all the distinct values of the independent
     variable */
  struct hsh_table *group_hash;

  struct per_var_ws *vws;

  /* An array of descriptive data.  One for each dependent variable */
  struct descriptive_data **dd_total;
};

/* Routines to show the output tables */
static void show_anova_table (const struct oneway_spec *, const struct oneway_workspace *);
static void show_descriptives (const struct oneway_spec *, const struct oneway_workspace *);
static void show_homogeneity (const struct oneway_spec *, const struct oneway_workspace *);

static void output_oneway (const struct oneway_spec *, struct oneway_workspace *ws);
static void run_oneway (const struct oneway_spec *cmd, struct casereader *input, const struct dataset *ds);

int
cmd_oneway (struct lexer *lexer, struct dataset *ds)
{
  const struct dictionary *dict = dataset_dict (ds);  
  struct oneway_spec oneway ;
  oneway.n_vars = 0;
  oneway.vars = NULL;
  oneway.indep_var = NULL;
  oneway.stats = 0;
  oneway.missing_type = MISS_ANALYSIS;
  oneway.exclude = MV_ANY;
  oneway.wv = dict_get_weight (dict);

  ll_init (&oneway.contrast_list);

  
  if ( lex_match (lexer, '/'))
    {
      if (!lex_force_match_id (lexer, "VARIABLES"))
	{
	  goto error;
	}
      lex_match (lexer, '=');
    }

  if (!parse_variables_const (lexer, dict,
			      &oneway.vars, &oneway.n_vars,
			      PV_NO_DUPLICATE | PV_NUMERIC))
    goto error;

  lex_force_match (lexer, T_BY);

  oneway.indep_var = parse_variable_const (lexer, dict);

  while (lex_token (lexer) != '.')
    {
      lex_match (lexer, '/');

      if (lex_match_id (lexer, "STATISTICS"))
	{
          lex_match (lexer, '=');
          while (lex_token (lexer) != '.' && lex_token (lexer) != '/')
	    {
	      if (lex_match_id (lexer, "DESCRIPTIVES"))
		{
		  oneway.stats |= STATS_DESCRIPTIVES;
		}
	      else if (lex_match_id (lexer, "HOMOGENEITY"))
		{
		  oneway.stats |= STATS_HOMOGENEITY;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "CONTRAST"))
	{
	  struct contrasts_node *cl = xzalloc (sizeof *cl);

	  struct ll_list *coefficient_list = &cl->coefficient_list;
          lex_match (lexer, '=');

	  ll_init (coefficient_list);

          while (lex_token (lexer) != '.' && lex_token (lexer) != '/')
	    {
	      if ( lex_is_number (lexer))
		{
		  struct coeff_node *cc = xmalloc (sizeof *cc);
		  cc->coeff = lex_number (lexer);

		  ll_push_tail (coefficient_list, &cc->ll);
		  lex_get (lexer);
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }

	  ll_push_tail (&oneway.contrast_list, &cl->ll);
	}
      else if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, '=');
          while (lex_token (lexer) != '.' && lex_token (lexer) != '/')
            {
	      if (lex_match_id (lexer, "INCLUDE"))
		{
		  oneway.exclude = MV_SYSTEM;
		}
	      else if (lex_match_id (lexer, "EXCLUDE"))
		{
		  oneway.exclude = MV_ANY;
		}
	      else if (lex_match_id (lexer, "LISTWISE"))
		{
		  oneway.missing_type = MISS_LISTWISE;
		}
	      else if (lex_match_id (lexer, "ANALYSIS"))
		{
		  oneway.missing_type = MISS_ANALYSIS;
		}
	      else
		{
                  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else
	{
	  lex_error (lexer, NULL);
	  goto error;
	}
    }


  {
    struct casegrouper *grouper;
    struct casereader *group;
    bool ok;

    grouper = casegrouper_create_splits (proc_open (ds), dict);
    while (casegrouper_get_next_group (grouper, &group))
      run_oneway (&oneway, group, ds);
    ok = casegrouper_destroy (grouper);
    ok = proc_commit (ds) && ok;
  }

  free (oneway.vars);
  return CMD_SUCCESS;

 error:
  free (oneway.vars);
  return CMD_FAILURE;
}




static int
compare_double_3way (const void *a_, const void *b_, const void *aux UNUSED)
{
  const double *a = a_;
  const double *b = b_;
  return *a < *b ? -1 : *a > *b;
}

static unsigned
do_hash_double (const void *value_, const void *aux UNUSED)
{
  const double *value = value_;
  return hash_double (*value, 0);
}

static void
free_double (void *value_, const void *aux UNUSED)
{
  double *value = value_;
  free (value);
}



static void postcalc (const struct oneway_spec *cmd);
static void  precalc (const struct oneway_spec *cmd);

static struct descriptive_data *
dd_create (const struct variable *var)
{
  struct descriptive_data *dd = xmalloc (sizeof *dd);

  dd->mom = moments1_create (MOMENT_VARIANCE);
  dd->minimum = DBL_MAX;
  dd->maximum = -DBL_MAX;
  dd->var = var;

  return dd;
}


static void *
makeit (void *aux1, void *aux2 UNUSED)
{
  const struct variable *var = aux1;

  struct descriptive_data *dd = dd_create (var);

  return dd;
}

static void 
updateit (void *user_data, 
	  enum mv_class exclude,
	  const struct variable *wv, 
	  const struct variable *catvar UNUSED,
	  const struct ccase *c,
	  void *aux1, void *aux2)
{
  struct descriptive_data *dd = user_data;

  const struct variable *varp = aux1;

  const union value *valx = case_data (c, varp);

  struct descriptive_data *dd_total = aux2;

  double weight;

  if ( var_is_value_missing (varp, valx, exclude))
    return;

  weight = wv != NULL ? case_data (c, wv)->f : 1.0;

  moments1_add (dd->mom, valx->f, weight);
  if (valx->f < dd->minimum)
    dd->minimum = valx->f;

  if (valx->f > dd->maximum)
    dd->maximum = valx->f;

  {
    const struct variable *var = dd_total->var;
    const union value *val = case_data (c, var);

    moments1_add (dd_total->mom,
		  val->f,
		  weight);

    if (val->f < dd_total->minimum)
      dd_total->minimum = val->f;

    if (val->f > dd_total->maximum)
      dd_total->maximum = val->f;
  }
}

static void
run_oneway (const struct oneway_spec *cmd,
            struct casereader *input,
            const struct dataset *ds)
{
  int v;
  struct taint *taint;
  struct dictionary *dict = dataset_dict (ds);
  struct casereader *reader;
  struct ccase *c;

  struct oneway_workspace ws;

  ws.actual_number_of_groups = 0;
  ws.vws = xmalloc (cmd->n_vars * sizeof (*ws.vws));
  ws.dd_total = xmalloc (sizeof (struct descriptive_data) * cmd->n_vars);

  for (v = 0 ; v < cmd->n_vars; ++v)
    ws.dd_total[v] = dd_create (cmd->vars[v]);

  for (v = 0; v < cmd->n_vars; ++v)
    {
      struct categoricals *cats = categoricals_create (&cmd->indep_var, 1,
						       cmd->wv, cmd->exclude, 
						       makeit,
						       updateit,
						       cmd->vars[v], ws.dd_total[v]);

      ws.vws[v].cov = covariance_2pass_create (1, &cmd->vars[v],
					       cats, 
					       cmd->wv, cmd->exclude);
    }

  c = casereader_peek (input, 0);
  if (c == NULL)
    {
      casereader_destroy (input);
      return;
    }
  output_split_file_values (ds, c);
  case_unref (c);

  taint = taint_clone (casereader_get_taint (input));

  ws.group_hash = hsh_create (4,
				  compare_double_3way,
				  do_hash_double,
				  free_double,
				  cmd->indep_var);

  precalc (cmd);

  input = casereader_create_filter_missing (input, &cmd->indep_var, 1,
                                            cmd->exclude, NULL, NULL);
  if (cmd->missing_type == MISS_LISTWISE)
    input = casereader_create_filter_missing (input, cmd->vars, cmd->n_vars,
                                              cmd->exclude, NULL, NULL);
  input = casereader_create_filter_weight (input, dict, NULL, NULL);

  reader = casereader_clone (input);

  for (; (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      size_t i;

      const double weight = dict_get_case_weight (dict, c, NULL);

      const union value *indep_val = case_data (c, cmd->indep_var);
      void **p = hsh_probe (ws.group_hash, &indep_val->f);
      if (*p == NULL)
        {
          double *value = *p = xmalloc (sizeof *value);
          *value = indep_val->f;
        }

      for (i = 0; i < cmd->n_vars; ++i)
	{
	  struct per_var_ws *pvw = &ws.vws[i];
	  const struct variable *v = cmd->vars[i];
	  const union value *val = case_data (c, v);
          struct group_proc *gp = group_proc_get (cmd->vars[i]);
	  struct hsh_table *group_hash = gp->group_hash;
	  struct group_statistics *gs;

	  if ( MISS_ANALYSIS == cmd->missing_type)
	    {
	      if ( var_is_value_missing (v, val, cmd->exclude))
		continue;
	    }

	  covariance_accumulate_pass1 (pvw->cov, c);

	  gs = hsh_find (group_hash, indep_val );

	  if ( ! gs )
	    {
	      gs = xmalloc (sizeof *gs);
	      gs->id = *indep_val;
	      gs->sum = 0;
	      gs->n = 0;
	      gs->ssq = 0;
	      gs->sum_diff = 0;
	      gs->minimum = DBL_MAX;
	      gs->maximum = -DBL_MAX;

	      hsh_insert ( group_hash, gs );
	    }

	  if (!var_is_value_missing (v, val, cmd->exclude))
	    {
	      struct group_statistics *totals = &gp->ugs;

	      totals->n += weight;
	      totals->sum += weight * val->f;
	      totals->ssq += weight * pow2 (val->f);

	      if ( val->f < totals->minimum )
		totals->minimum = val->f;

	      if ( val->f > totals->maximum )
		totals->maximum = val->f;

	      gs->n += weight;
	      gs->sum += weight * val->f;
	      gs->ssq += weight * pow2 (val->f);

	      if ( val->f < gs->minimum )
		gs->minimum = val->f;

	      if ( val->f > gs->maximum )
		gs->maximum = val->f;
	    }

	  gp->n_groups = hsh_count (group_hash );
	}

    }
  casereader_destroy (reader);
  reader = casereader_clone (input);
  for ( ; (c = casereader_read (reader) ); case_unref (c))
    {
      int i;
      for (i = 0; i < cmd->n_vars; ++i)
	{
	  struct per_var_ws *pvw = &ws.vws[i];
	  const struct variable *v = cmd->vars[i];
	  const union value *val = case_data (c, v);

	  if ( MISS_ANALYSIS == cmd->missing_type)
	    {
	      if ( var_is_value_missing (v, val, cmd->exclude))
		continue;
	    }

	  covariance_accumulate_pass2 (pvw->cov, c);
	}
    }
  casereader_destroy (reader);

  for (v = 0; v < cmd->n_vars; ++v)
    {
      struct per_var_ws *pvw = &ws.vws[v];
      gsl_matrix *cm = covariance_calculate_unnormalized (pvw->cov);
      const struct categoricals *cats = covariance_get_categoricals (pvw->cov);

      double n;
      moments1_calculate (ws.dd_total[v]->mom, &n, NULL, NULL, NULL, NULL);

      pvw->sst = gsl_matrix_get (cm, 0, 0);

      //      gsl_matrix_fprintf (stdout, cm, "%g ");

      reg_sweep (cm, 0);

      pvw->sse = gsl_matrix_get (cm, 0, 0);

      pvw->ssa = pvw->sst - pvw->sse;

      pvw->n_groups = categoricals_total (cats);

      pvw->mse = (pvw->sst - pvw->ssa) / (n - pvw->n_groups);
    }

  postcalc (cmd);

  
  for (v = 0; v < cmd->n_vars; ++v)
    {
      struct categoricals *cats = covariance_get_categoricals (ws.vws[v].cov);

      categoricals_done (cats);
      
      if (categoricals_total (cats) > ws.actual_number_of_groups)
	ws.actual_number_of_groups = categoricals_total (cats);
    }

  if ( cmd->stats & STATS_HOMOGENEITY )
    levene (dict, casereader_clone (input), cmd->indep_var,
	    cmd->n_vars, cmd->vars, cmd->exclude);

  casereader_destroy (input);

  if (!taint_has_tainted_successor (taint))
    output_oneway (cmd, &ws);

  taint_destroy (taint);
}

/* Pre calculations */
static void
precalc (const struct oneway_spec *cmd)
{
  size_t i = 0;

  for (i = 0; i < cmd->n_vars; ++i)
    {
      struct group_proc *gp = group_proc_get (cmd->vars[i]);
      struct group_statistics *totals = &gp->ugs;

      /* Create a hash for each of the dependent variables.
	 The hash contains a group_statistics structure,
	 and is keyed by value of the independent variable */

      gp->group_hash = hsh_create (4, compare_group, hash_group,
				   (hsh_free_func *) free_group,
				   cmd->indep_var);

      totals->sum = 0;
      totals->n = 0;
      totals->ssq = 0;
      totals->sum_diff = 0;
      totals->maximum = -DBL_MAX;
      totals->minimum = DBL_MAX;
    }
}

/* Post calculations for the ONEWAY command */
static void
postcalc (const struct oneway_spec *cmd)
{
  size_t i = 0;

  for (i = 0; i < cmd->n_vars; ++i)
    {
      struct group_proc *gp = group_proc_get (cmd->vars[i]);
      struct hsh_table *group_hash = gp->group_hash;
      struct group_statistics *totals = &gp->ugs;

      struct hsh_iterator g;
      struct group_statistics *gs;

      for (gs =  hsh_first (group_hash, &g);
	   gs != 0;
	   gs = hsh_next (group_hash, &g))
	{
	  gs->mean = gs->sum / gs->n;
	  gs->s_std_dev = sqrt (gs->ssq / gs->n - pow2 (gs->mean));

	  gs->std_dev = sqrt (
			      gs->n / (gs->n - 1) *
			      ( gs->ssq / gs->n - pow2 (gs->mean))
			      );

	  gs->se_mean = gs->std_dev / sqrt (gs->n);
	  gs->mean_diff = gs->sum_diff / gs->n;
	}

      totals->mean = totals->sum / totals->n;
      totals->std_dev = sqrt (
			      totals->n / (totals->n - 1) *
			      (totals->ssq / totals->n - pow2 (totals->mean))
			      );

      totals->se_mean = totals->std_dev / sqrt (totals->n);
    }
}

static void show_contrast_coeffs (const struct oneway_spec *cmd, const struct oneway_workspace *ws);
static void show_contrast_tests (const struct oneway_spec *cmd, const struct oneway_workspace *ws);

static void
output_oneway (const struct oneway_spec *cmd, struct oneway_workspace *ws)
{
  size_t i = 0;

  /* Check the sanity of the given contrast values */
  struct contrasts_node *coeff_list  = NULL;
  ll_for_each (coeff_list, struct contrasts_node, ll, &cmd->contrast_list)
    {
      struct coeff_node *cn = NULL;
      double sum = 0;
      struct ll_list *cl = &coeff_list->coefficient_list;
      ++i;

      if (ll_count (cl) != ws->actual_number_of_groups)
	{
	  msg (SW,
	       _("Number of contrast coefficients must equal the number of groups"));
	  coeff_list->bad_count = true;
	  continue;
	}

      ll_for_each (cn, struct coeff_node, ll, cl)
	sum += cn->coeff;

      if ( sum != 0.0 )
	msg (SW, _("Coefficients for contrast %zu do not total zero"), i);
    }

  if (cmd->stats & STATS_DESCRIPTIVES)
    show_descriptives (cmd, ws);

  if (cmd->stats & STATS_HOMOGENEITY)
    show_homogeneity (cmd, ws);

  show_anova_table (cmd, ws);

  if (ll_count (&cmd->contrast_list) > 0)
    {
      show_contrast_coeffs (cmd, ws);
      show_contrast_tests (cmd, ws);
    }

  /* Clean up */
  for (i = 0; i < cmd->n_vars; ++i )
    {
      struct hsh_table *group_hash = group_proc_get (cmd->vars[i])->group_hash;

      hsh_destroy (group_hash);
    }

  hsh_destroy (ws->group_hash);
}


/* Show the ANOVA table */
static void
show_anova_table (const struct oneway_spec *cmd, const struct oneway_workspace *ws)
{
  size_t i;
  int n_cols =7;
  size_t n_rows = cmd->n_vars * 3 + 1;

  struct tab_table *t = tab_create (n_cols, n_rows);

  tab_headers (t, 2, 0, 1, 0);

  tab_box (t,
	   TAL_2, TAL_2,
	   -1, TAL_1,
	   0, 0,
	   n_cols - 1, n_rows - 1);

  tab_hline (t, TAL_2, 0, n_cols - 1, 1 );
  tab_vline (t, TAL_2, 2, 0, n_rows - 1);
  tab_vline (t, TAL_0, 1, 0, 0);

  tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("Sum of Squares"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("df"));
  tab_text (t, 4, 0, TAB_CENTER | TAT_TITLE, _("Mean Square"));
  tab_text (t, 5, 0, TAB_CENTER | TAT_TITLE, _("F"));
  tab_text (t, 6, 0, TAB_CENTER | TAT_TITLE, _("Significance"));


  for (i = 0; i < cmd->n_vars; ++i)
    {
      double n;
      double df1, df2;
      double msa;
      const char *s = var_to_string (cmd->vars[i]);
      const struct per_var_ws *pvw = &ws->vws[i];

      moments1_calculate (ws->dd_total[i]->mom, &n, NULL, NULL, NULL, NULL);

      df1 = pvw->n_groups - 1;
      df2 = n - pvw->n_groups;
      msa = pvw->ssa / df1;

      tab_text (t, 0, i * 3 + 1, TAB_LEFT | TAT_TITLE, s);
      tab_text (t, 1, i * 3 + 1, TAB_LEFT | TAT_TITLE, _("Between Groups"));
      tab_text (t, 1, i * 3 + 2, TAB_LEFT | TAT_TITLE, _("Within Groups"));
      tab_text (t, 1, i * 3 + 3, TAB_LEFT | TAT_TITLE, _("Total"));

      if (i > 0)
	tab_hline (t, TAL_1, 0, n_cols - 1, i * 3 + 1);


      /* Sums of Squares */
      tab_double (t, 2, i * 3 + 1, 0, pvw->ssa, NULL);
      tab_double (t, 2, i * 3 + 3, 0, pvw->sst, NULL);
      tab_double (t, 2, i * 3 + 2, 0, pvw->sse, NULL);


      /* Degrees of freedom */
      tab_fixed (t, 3, i * 3 + 1, 0, df1, 4, 0);
      tab_fixed (t, 3, i * 3 + 2, 0, df2, 4, 0);
      tab_fixed (t, 3, i * 3 + 3, 0, n - 1, 4, 0);

      /* Mean Squares */
      tab_double (t, 4, i * 3 + 1, TAB_RIGHT, msa, NULL);
      tab_double (t, 4, i * 3 + 2, TAB_RIGHT, pvw->mse, NULL);

      {
	const double F = msa / pvw->mse ;

	/* The F value */
	tab_double (t, 5, i * 3 + 1, 0,  F, NULL);

	/* The significance */
	tab_double (t, 6, i * 3 + 1, 0, gsl_cdf_fdist_Q (F, df1, df2), NULL);
      }
    }

  tab_title (t, _("ANOVA"));
  tab_submit (t);
}


/* Show the descriptives table */
static void
show_descriptives (const struct oneway_spec *cmd, const struct oneway_workspace *ws)
{
  size_t v;
  int n_cols = 10;
  struct tab_table *t;
  int row;

  const double confidence = 0.95;
  const double q = (1.0 - confidence) / 2.0;

  const struct fmt_spec *wfmt = cmd->wv ? var_get_print_format (cmd->wv) : &F_8_0;

  int n_rows = 2;

  for (v = 0; v < cmd->n_vars; ++v)
    n_rows += ws->actual_number_of_groups + 1;

  t = tab_create (n_cols, n_rows);
  tab_headers (t, 2, 0, 2, 0);

  /* Put a frame around the entire box, and vertical lines inside */
  tab_box (t,
	   TAL_2, TAL_2,
	   -1, TAL_1,
	   0, 0,
	   n_cols - 1, n_rows - 1);

  /* Underline headers */
  tab_hline (t, TAL_2, 0, n_cols - 1, 2);
  tab_vline (t, TAL_2, 2, 0, n_rows - 1);

  tab_text (t, 2, 1, TAB_CENTER | TAT_TITLE, _("N"));
  tab_text (t, 3, 1, TAB_CENTER | TAT_TITLE, _("Mean"));
  tab_text (t, 4, 1, TAB_CENTER | TAT_TITLE, _("Std. Deviation"));
  tab_text (t, 5, 1, TAB_CENTER | TAT_TITLE, _("Std. Error"));


  tab_vline (t, TAL_0, 7, 0, 0);
  tab_hline (t, TAL_1, 6, 7, 1);
  tab_joint_text_format (t, 6, 0, 7, 0, TAB_CENTER | TAT_TITLE,
                         _("%g%% Confidence Interval for Mean"),
                         confidence*100.0);

  tab_text (t, 6, 1, TAB_CENTER | TAT_TITLE, _("Lower Bound"));
  tab_text (t, 7, 1, TAB_CENTER | TAT_TITLE, _("Upper Bound"));

  tab_text (t, 8, 1, TAB_CENTER | TAT_TITLE, _("Minimum"));
  tab_text (t, 9, 1, TAB_CENTER | TAT_TITLE, _("Maximum"));

  tab_title (t, _("Descriptives"));

  row = 2;
  for (v = 0; v < cmd->n_vars; ++v)
    {
      const char *s = var_to_string (cmd->vars[v]);
      const struct fmt_spec *fmt = var_get_print_format (cmd->vars[v]);

      int count = 0;

      struct per_var_ws *pvw = &ws->vws[v];
      const struct categoricals *cats = covariance_get_categoricals (pvw->cov);

      tab_text (t, 0, row, TAB_LEFT | TAT_TITLE, s);
      if ( v > 0)
	tab_hline (t, TAL_1, 0, n_cols - 1, row);

      for (count = 0; count < categoricals_total (cats); ++count)
	{
	  double T;
	  double n, mean, variance;
	  double std_dev, std_error ;

	  struct string vstr;

	  const union value *gval = categoricals_get_value_by_subscript (cats, count);
	  const struct descriptive_data *dd = categoricals_get_user_data_by_subscript (cats, count);

	  moments1_calculate (dd->mom, &n, &mean, &variance, NULL, NULL);

	  std_dev = sqrt (variance);
	  std_error = std_dev / sqrt (n) ;

	  ds_init_empty (&vstr);

	  var_append_value_name (cmd->indep_var, gval, &vstr);

	  tab_text (t, 1, row + count,
		    TAB_LEFT | TAT_TITLE,
		    ds_cstr (&vstr));

	  ds_destroy (&vstr);

	  /* Now fill in the numbers ... */

	  tab_double (t, 2, row + count, 0, n, wfmt);

	  tab_double (t, 3, row + count, 0, mean, NULL);

	  tab_double (t, 4, row + count, 0, std_dev, NULL);


	  tab_double (t, 5, row + count, 0, std_error, NULL);

	  /* Now the confidence interval */

	  T = gsl_cdf_tdist_Qinv (q, n - 1);

	  tab_double (t, 6, row + count, 0,
		      mean - T * std_error, NULL);

	  tab_double (t, 7, row + count, 0,
		      mean + T * std_error, NULL);

	  /* Min and Max */

	  tab_double (t, 8, row + count, 0,  dd->minimum, fmt);
	  tab_double (t, 9, row + count, 0,  dd->maximum, fmt);
	}

      {
	double T;
	double n, mean, variance;
	double std_dev;
	double std_error;

	moments1_calculate (ws->dd_total[v]->mom, &n, &mean, &variance, NULL, NULL);

	std_dev = sqrt (variance);
	std_error = std_dev / sqrt (n) ;

	tab_text (t, 1, row + count,
		  TAB_LEFT | TAT_TITLE, _("Total"));

	tab_double (t, 2, row + count, 0, n, wfmt);

	tab_double (t, 3, row + count, 0, mean, NULL);

	tab_double (t, 4, row + count, 0, std_dev, NULL);

	tab_double (t, 5, row + count, 0, std_error, NULL);

	/* Now the confidence interval */
	T = gsl_cdf_tdist_Qinv (q, n - 1);

	tab_double (t, 6, row + count, 0,
		    mean - T * std_error, NULL);

	tab_double (t, 7, row + count, 0,
		    mean + T * std_error, NULL);

	/* Min and Max */
	tab_double (t, 8, row + count, 0,  ws->dd_total[v]->minimum, fmt);
	tab_double (t, 9, row + count, 0,  ws->dd_total[v]->maximum, fmt);
      }

      row += categoricals_total (cats) + 1;
    }

  tab_submit (t);
}

/* Show the homogeneity table */
static void
show_homogeneity (const struct oneway_spec *cmd, const struct oneway_workspace *ws)
{
  size_t v;
  int n_cols = 5;
  size_t n_rows = cmd->n_vars + 1;

  struct tab_table *t = tab_create (n_cols, n_rows);
  tab_headers (t, 1, 0, 1, 0);

  /* Put a frame around the entire box, and vertical lines inside */
  tab_box (t,
	   TAL_2, TAL_2,
	   -1, TAL_1,
	   0, 0,
	   n_cols - 1, n_rows - 1);


  tab_hline (t, TAL_2, 0, n_cols - 1, 1);
  tab_vline (t, TAL_2, 1, 0, n_rows - 1);

  tab_text (t, 1, 0, TAB_CENTER | TAT_TITLE, _("Levene Statistic"));
  tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("df1"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("df2"));
  tab_text (t, 4, 0, TAB_CENTER | TAT_TITLE, _("Significance"));

  tab_title (t, _("Test of Homogeneity of Variances"));

  for (v = 0; v < cmd->n_vars; ++v)
    {
      double n;


      const struct per_var_ws *pvw = &ws->vws[v];

      const struct variable *var = cmd->vars[v];
      const struct group_proc *gp = group_proc_get (cmd->vars[v]);
      const char *s = var_to_string (var);
      double df1, df2;
      double F = gp->levene;

      moments1_calculate (ws->dd_total[v]->mom, &n, NULL, NULL, NULL, NULL);

      df1 = pvw->n_groups - 1;
      df2 = n - pvw->n_groups;

      tab_text (t, 0, v + 1, TAB_LEFT | TAT_TITLE, s);

      tab_double (t, 1, v + 1, TAB_RIGHT, F, NULL);
      tab_fixed (t, 2, v + 1, TAB_RIGHT, df1, 8, 0);
      tab_fixed (t, 3, v + 1, TAB_RIGHT, df2, 8, 0);

      /* Now the significance */
      tab_double (t, 4, v + 1, TAB_RIGHT, gsl_cdf_fdist_Q (F, df1, df2), NULL);
    }

  tab_submit (t);
}


/* Show the contrast coefficients table */
static void
show_contrast_coeffs (const struct oneway_spec *cmd, const struct oneway_workspace *ws)
{
  int c_num = 0;
  struct ll *cli;

  int n_contrasts = ll_count (&cmd->contrast_list);
  int n_cols = 2 + ws->actual_number_of_groups;
  int n_rows = 2 + n_contrasts;

  struct tab_table *t;

  const struct covariance *cov = ws->vws[0].cov ;

  t = tab_create (n_cols, n_rows);
  tab_headers (t, 2, 0, 2, 0);

  /* Put a frame around the entire box, and vertical lines inside */
  tab_box (t,
	   TAL_2, TAL_2,
	   -1, TAL_1,
	   0, 0,
	   n_cols - 1, n_rows - 1);

  tab_box (t,
	   -1, -1,
	   TAL_0, TAL_0,
	   2, 0,
	   n_cols - 1, 0);

  tab_box (t,
	   -1, -1,
	   TAL_0, TAL_0,
	   0, 0,
	   1, 1);

  tab_hline (t, TAL_1, 2, n_cols - 1, 1);
  tab_hline (t, TAL_2, 0, n_cols - 1, 2);

  tab_vline (t, TAL_2, 2, 0, n_rows - 1);

  tab_title (t, _("Contrast Coefficients"));

  tab_text (t,  0, 2, TAB_LEFT | TAT_TITLE, _("Contrast"));


  tab_joint_text (t, 2, 0, n_cols - 1, 0, TAB_CENTER | TAT_TITLE,
		  var_to_string (cmd->indep_var));

  for ( cli = ll_head (&cmd->contrast_list);
	cli != ll_null (&cmd->contrast_list);
	cli = ll_next (cli))
    {
      int count = 0;
      struct contrasts_node *cn = ll_data (cli, struct contrasts_node, ll);
      struct ll *coeffi ;

      tab_text_format (t, 1, c_num + 2, TAB_CENTER, "%d", c_num + 1);

      for (coeffi = ll_head (&cn->coefficient_list);
	   coeffi != ll_null (&cn->coefficient_list);
	   ++count, coeffi = ll_next (coeffi))
	{
	  const struct categoricals *cats = covariance_get_categoricals (cov);
	  const union value *val = categoricals_get_value_by_subscript (cats, count);
	  struct string vstr;

	  ds_init_empty (&vstr);

	  var_append_value_name (cmd->indep_var, val, &vstr);

	  tab_text (t, count + 2, 1, TAB_CENTER | TAT_TITLE, ds_cstr (&vstr));

	  ds_destroy (&vstr);

	  if (cn->bad_count)
	    tab_text (t, count + 2, c_num + 2, TAB_RIGHT, "?" );
	  else
	    {
	      struct coeff_node *coeffn = ll_data (coeffi, struct coeff_node, ll);

	      tab_text_format (t, count + 2, c_num + 2, TAB_RIGHT, "%g", coeffn->coeff);
	    }
	}
      ++c_num;
    }

  tab_submit (t);
}


/* Show the results of the contrast tests */
static void
show_contrast_tests (const struct oneway_spec *cmd, const struct oneway_workspace *ws)
{
  int n_contrasts = ll_count (&cmd->contrast_list);
  size_t v;
  int n_cols = 8;
  size_t n_rows = 1 + cmd->n_vars * 2 * n_contrasts;

  struct tab_table *t;

  t = tab_create (n_cols, n_rows);
  tab_headers (t, 3, 0, 1, 0);

  /* Put a frame around the entire box, and vertical lines inside */
  tab_box (t,
	   TAL_2, TAL_2,
	   -1, TAL_1,
	   0, 0,
	   n_cols - 1, n_rows - 1);

  tab_box (t,
	   -1, -1,
	   TAL_0, TAL_0,
	   0, 0,
	   2, 0);

  tab_hline (t, TAL_2, 0, n_cols - 1, 1);
  tab_vline (t, TAL_2, 3, 0, n_rows - 1);

  tab_title (t, _("Contrast Tests"));

  tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("Contrast"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("Value of Contrast"));
  tab_text (t,  4, 0, TAB_CENTER | TAT_TITLE, _("Std. Error"));
  tab_text (t,  5, 0, TAB_CENTER | TAT_TITLE, _("t"));
  tab_text (t,  6, 0, TAB_CENTER | TAT_TITLE, _("df"));
  tab_text (t,  7, 0, TAB_CENTER | TAT_TITLE, _("Sig. (2-tailed)"));

  for (v = 0; v < cmd->n_vars; ++v)
    {
      const struct per_var_ws *pvw = &ws->vws[v];
      const struct categoricals *cats = covariance_get_categoricals (pvw->cov);
      struct ll *cli;
      int i = 0;
      int lines_per_variable = 2 * n_contrasts;

      tab_text (t,  0, (v * lines_per_variable) + 1, TAB_LEFT | TAT_TITLE,
		var_to_string (cmd->vars[v]));

      for ( cli = ll_head (&cmd->contrast_list);
	    cli != ll_null (&cmd->contrast_list);
	    ++i, cli = ll_next (cli))
	{
	  struct contrasts_node *cn = ll_data (cli, struct contrasts_node, ll);
	  struct ll *coeffi ;
	  int ci = 0;
	  double contrast_value = 0.0;
	  double coef_msq = 0.0;

	  double T;
	  double std_error_contrast;
	  double df;
	  double sec_vneq = 0.0;

	  /* Note: The calculation of the degrees of freedom in the
	     "variances not equal" case is painfull!!
	     The following formula may help to understand it:
	     \frac{\left (\sum_{i=1}^k{c_i^2\frac{s_i^2}{n_i}}\right)^2}
	     {
	     \sum_{i=1}^k\left (
	     \frac{\left (c_i^2\frac{s_i^2}{n_i}\right)^2}  {n_i-1}
	     \right)
	     }
	  */

	  double df_denominator = 0.0;
	  double df_numerator = 0.0;

	  double grand_n;
	  moments1_calculate (ws->dd_total[v]->mom, &grand_n, NULL, NULL, NULL, NULL);
	  df = grand_n - pvw->n_groups;

	  if ( i == 0 )
	    {
	      tab_text (t,  1, (v * lines_per_variable) + i + 1,
			TAB_LEFT | TAT_TITLE,
			_("Assume equal variances"));

	      tab_text (t,  1, (v * lines_per_variable) + i + 1 + n_contrasts,
			TAB_LEFT | TAT_TITLE,
			_("Does not assume equal"));
	    }

	  tab_text_format (t,  2, (v * lines_per_variable) + i + 1,
                           TAB_CENTER | TAT_TITLE, "%d", i + 1);


	  tab_text_format (t,  2,
                           (v * lines_per_variable) + i + 1 + n_contrasts,
                           TAB_CENTER | TAT_TITLE, "%d", i + 1);

	  if (cn->bad_count)
	    continue;

	  for (coeffi = ll_head (&cn->coefficient_list);
	       coeffi != ll_null (&cn->coefficient_list);
	       ++ci, coeffi = ll_next (coeffi))
	    {
	      double n, mean, variance;
	      const struct descriptive_data *dd = categoricals_get_user_data_by_subscript (cats, ci);
	      struct coeff_node *cn = ll_data (coeffi, struct coeff_node, ll);
	      const double coef = cn->coeff; 
	      double winv ;

	      moments1_calculate (dd->mom, &n, &mean, &variance, NULL, NULL);

	      winv = variance / n;

	      contrast_value += coef * mean;

	      coef_msq += (pow2 (coef)) / n;

	      sec_vneq += (pow2 (coef)) * variance / n;

	      df_numerator += (pow2 (coef)) * winv;
	      df_denominator += pow2((pow2 (coef)) * winv) / (n - 1);
	    }

	  sec_vneq = sqrt (sec_vneq);

	  df_numerator = pow2 (df_numerator);

	  tab_double (t,  3, (v * lines_per_variable) + i + 1,
		      TAB_RIGHT, contrast_value, NULL);

	  tab_double (t,  3, (v * lines_per_variable) + i + 1 +
		      n_contrasts,
		      TAB_RIGHT, contrast_value, NULL);

	  std_error_contrast = sqrt (pvw->mse * coef_msq);

	  /* Std. Error */
	  tab_double (t,  4, (v * lines_per_variable) + i + 1,
		      TAB_RIGHT, std_error_contrast,
		      NULL);

	  T = fabs (contrast_value / std_error_contrast);

	  /* T Statistic */

	  tab_double (t,  5, (v * lines_per_variable) + i + 1,
		      TAB_RIGHT, T,
		      NULL);


	  /* Degrees of Freedom */
	  tab_fixed (t,  6, (v * lines_per_variable) + i + 1,
		     TAB_RIGHT,  df,
		     8, 0);


	  /* Significance TWO TAILED !!*/
	  tab_double (t,  7, (v * lines_per_variable) + i + 1,
		      TAB_RIGHT,  2 * gsl_cdf_tdist_Q (T, df),
		      NULL);

	  /* Now for the Variances NOT Equal case */

	  /* Std. Error */
	  tab_double (t,  4,
		      (v * lines_per_variable) + i + 1 + n_contrasts,
		      TAB_RIGHT, sec_vneq,
		      NULL);

	  T = contrast_value / sec_vneq;
	  tab_double (t,  5,
		      (v * lines_per_variable) + i + 1 + n_contrasts,
		      TAB_RIGHT, T,
		      NULL);

	  df = df_numerator / df_denominator;

	  tab_double (t,  6,
		      (v * lines_per_variable) + i + 1 + n_contrasts,
		      TAB_RIGHT, df,
		      NULL);

	  /* The Significance */
	  tab_double (t, 7, (v * lines_per_variable) + i + 1 + n_contrasts,
		      TAB_RIGHT,  2 * gsl_cdf_tdist_Q (T,df),
		      NULL);
	}

      if ( v > 0 )
	tab_hline (t, TAL_1, 0, n_cols - 1, (v * lines_per_variable) + 1);
    }

  tab_submit (t);
}


