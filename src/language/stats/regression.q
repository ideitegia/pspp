/* PSPP - a program for statistical analysis.
   Copyright (C) 2005, 2009 Free Software Foundation, Inc.

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

#include <gsl/gsl_cdf.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>
#include <math.h>
#include <stdlib.h>
#include <data/case.h>
#include <data/casegrouper.h>
#include <data/casereader.h>
#include <data/category.h>
#include <data/dictionary.h>
#include <data/missing-values.h>
#include <data/procedure.h>
#include <data/transformations.h>
#include <data/value-labels.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/dictionary/split-file.h>
#include <language/data-io/file-handle.h>
#include <language/lexer/lexer.h>
#include <libpspp/compiler.h>
#include <libpspp/message.h>
#include <libpspp/taint.h>
#include <math/covariance.h>
#include <math/linreg.h>
#include <math/moments.h>
#include <output/table.h>

#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

#define REG_LARGE_DATA 1000

/* (headers) */

/* (specification)
   "REGRESSION" (regression_):
   *variables=custom;
   +statistics[st_]=r,
                    coeff,
                    anova,
                    outs,
                    zpp,
                    label,
                    sha,
                    ci,
                    bcov,
                    ses,
                    xtx,
                    collin,
                    tol,
                    selection,
                    f,
                    defaults,
                    all;
   ^dependent=varlist;
   +save[sv_]=resid,pred;
   +method=enter.
*/
/* (declarations) */
/* (functions) */
static struct cmd_regression cmd;

/*
  Moments for each of the variables used.
 */
struct moments_var
{
  struct moments1 *m;
  const struct variable *v;
};

/*
  Transformations for saving predicted values
  and residuals, etc.
 */
struct reg_trns
{
  int n_trns;			/* Number of transformations. */
  int trns_id;			/* Which trns is this one? */
  linreg *c;		/* Linear model for this trns. */
};
/*
  Variables used (both explanatory and response).
 */
static const struct variable **v_variables;

/*
  Number of variables.
 */
static size_t n_variables;

static bool run_regression (struct casereader *, struct cmd_regression *,
			    struct dataset *, linreg **);

/*
   STATISTICS subcommand output functions.
 */
static void reg_stats_r (linreg *);
static void reg_stats_coeff (linreg *);
static void reg_stats_anova (linreg *);
static void reg_stats_outs (linreg *);
static void reg_stats_zpp (linreg *);
static void reg_stats_label (linreg *);
static void reg_stats_sha (linreg *);
static void reg_stats_ci (linreg *);
static void reg_stats_f (linreg *);
static void reg_stats_bcov (linreg *);
static void reg_stats_ses (linreg *);
static void reg_stats_xtx (linreg *);
static void reg_stats_collin (linreg *);
static void reg_stats_tol (linreg *);
static void reg_stats_selection (linreg *);
static void statistics_keyword_output (void (*)(linreg *),
				       int, linreg *);

static void
reg_stats_r (linreg * c)
{
  struct tab_table *t;
  int n_rows = 2;
  int n_cols = 5;
  double rsq;
  double adjrsq;
  double std_error;

  assert (c != NULL);
  rsq = c->ssm / c->sst;
  adjrsq = 1.0 - (1.0 - rsq) * (c->n_obs - 1.0) / (c->n_obs - c->n_indeps);
  std_error = sqrt (linreg_mse (c));
  t = tab_create (n_cols, n_rows, 0);
  tab_dim (t, tab_natural_dimensions, NULL);
  tab_box (t, TAL_2, TAL_2, -1, TAL_1, 0, 0, n_cols - 1, n_rows - 1);
  tab_hline (t, TAL_2, 0, n_cols - 1, 1);
  tab_vline (t, TAL_2, 2, 0, n_rows - 1);
  tab_vline (t, TAL_0, 1, 0, 0);

  tab_text (t, 1, 0, TAB_CENTER | TAT_TITLE, _("R"));
  tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("R Square"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("Adjusted R Square"));
  tab_text (t, 4, 0, TAB_CENTER | TAT_TITLE, _("Std. Error of the Estimate"));
  tab_double (t, 1, 1, TAB_RIGHT, sqrt (rsq), NULL);
  tab_double (t, 2, 1, TAB_RIGHT, rsq, NULL);
  tab_double (t, 3, 1, TAB_RIGHT, adjrsq, NULL);
  tab_double (t, 4, 1, TAB_RIGHT, std_error, NULL);
  tab_title (t, _("Model Summary"));
  tab_submit (t);
}

/*
  Table showing estimated regression coefficients.
 */
static void
reg_stats_coeff (linreg * c)
{
  size_t j;
  int n_cols = 7;
  int n_rows;
  int this_row;
  double t_stat;
  double pval;
  double std_err;
  double beta;
  const char *label;

  const struct variable *v;
  struct tab_table *t;

  assert (c != NULL);
  n_rows = c->n_coeffs + 3;

  t = tab_create (n_cols, n_rows, 0);
  tab_headers (t, 2, 0, 1, 0);
  tab_dim (t, tab_natural_dimensions, NULL);
  tab_box (t, TAL_2, TAL_2, -1, TAL_1, 0, 0, n_cols - 1, n_rows - 1);
  tab_hline (t, TAL_2, 0, n_cols - 1, 1);
  tab_vline (t, TAL_2, 2, 0, n_rows - 1);
  tab_vline (t, TAL_0, 1, 0, 0);

  tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("B"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("Std. Error"));
  tab_text (t, 4, 0, TAB_CENTER | TAT_TITLE, _("Beta"));
  tab_text (t, 5, 0, TAB_CENTER | TAT_TITLE, _("t"));
  tab_text (t, 6, 0, TAB_CENTER | TAT_TITLE, _("Significance"));
  tab_text (t, 1, 1, TAB_LEFT | TAT_TITLE, _("(Constant)"));
  tab_double (t, 2, 1, 0, linreg_intercept (c), NULL);
  std_err = sqrt (gsl_matrix_get (linreg_cov (c), 0, 0));
  tab_double (t, 3, 1, 0, std_err, NULL);
  tab_double (t, 4, 1, 0, 0.0, NULL);
  t_stat = linreg_intercept (c) / std_err;
  tab_double (t, 5, 1, 0, t_stat, NULL);
  pval = 2 * gsl_cdf_tdist_Q (fabs (t_stat), 1.0);
  tab_double (t, 6, 1, 0, pval, NULL);
  for (j = 0; j < linreg_n_coeffs (c); j++)
    {
      struct string tstr;
      ds_init_empty (&tstr);
      this_row = j + 2;

      v = linreg_indep_var (c, j);
      label = var_to_string (v);
      /* Do not overwrite the variable's name. */
      ds_put_cstr (&tstr, label);
      tab_text (t, 1, this_row, TAB_CENTER, ds_cstr (&tstr));
      /*
         Regression coefficients.
       */
      tab_double (t, 2, this_row, 0, linreg_coeff (c, j), NULL);
      /*
         Standard error of the coefficients.
       */
      std_err = sqrt (gsl_matrix_get (linreg_cov (c), j + 1, j + 1));
      tab_double (t, 3, this_row, 0, std_err, NULL);
      /*
         Standardized coefficient, i.e., regression coefficient
         if all variables had unit variance.
       */
      beta = sqrt (gsl_matrix_get (linreg_cov (c), j, j));
      beta *= linreg_coeff (c, j) / c->depvar_std;
      tab_double (t, 4, this_row, 0, beta, NULL);

      /*
         Test statistic for H0: coefficient is 0.
       */
      t_stat = linreg_coeff (c, j) / std_err;
      tab_double (t, 5, this_row, 0, t_stat, NULL);
      /*
         P values for the test statistic above.
       */
      pval =
	2 * gsl_cdf_tdist_Q (fabs (t_stat),
			     (double) (linreg_n_obs (c) - linreg_n_coeffs (c)));
      tab_double (t, 6, this_row, 0, pval, NULL);
      ds_destroy (&tstr);
    }
  tab_title (t, _("Coefficients"));
  tab_submit (t);
}

/*
  Display the ANOVA table.
 */
static void
reg_stats_anova (linreg * c)
{
  int n_cols = 7;
  int n_rows = 4;
  const double msm = linreg_ssreg (c) / linreg_dfmodel (c);
  const double mse = linreg_mse (c);
  const double F = msm / mse;
  const double pval = gsl_cdf_fdist_Q (F, c->dfm, c->dfe);

  struct tab_table *t;

  assert (c != NULL);
  t = tab_create (n_cols, n_rows, 0);
  tab_headers (t, 2, 0, 1, 0);
  tab_dim (t, tab_natural_dimensions, NULL);

  tab_box (t, TAL_2, TAL_2, -1, TAL_1, 0, 0, n_cols - 1, n_rows - 1);

  tab_hline (t, TAL_2, 0, n_cols - 1, 1);
  tab_vline (t, TAL_2, 2, 0, n_rows - 1);
  tab_vline (t, TAL_0, 1, 0, 0);

  tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("Sum of Squares"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("df"));
  tab_text (t, 4, 0, TAB_CENTER | TAT_TITLE, _("Mean Square"));
  tab_text (t, 5, 0, TAB_CENTER | TAT_TITLE, _("F"));
  tab_text (t, 6, 0, TAB_CENTER | TAT_TITLE, _("Significance"));

  tab_text (t, 1, 1, TAB_LEFT | TAT_TITLE, _("Regression"));
  tab_text (t, 1, 2, TAB_LEFT | TAT_TITLE, _("Residual"));
  tab_text (t, 1, 3, TAB_LEFT | TAT_TITLE, _("Total"));

  /* Sums of Squares */
  tab_double (t, 2, 1, 0, c->ssm, NULL);
  tab_double (t, 2, 3, 0, c->sst, NULL);
  tab_double (t, 2, 2, 0, c->sse, NULL);


  /* Degrees of freedom */
  tab_text_format (t, 3, 1, TAB_RIGHT, "%g", c->dfm);
  tab_text_format (t, 3, 2, TAB_RIGHT, "%g", c->dfe);
  tab_text_format (t, 3, 3, TAB_RIGHT, "%g", c->dft);

  /* Mean Squares */
  tab_double (t, 4, 1, TAB_RIGHT, msm, NULL);
  tab_double (t, 4, 2, TAB_RIGHT, mse, NULL);

  tab_double (t, 5, 1, 0, F, NULL);

  tab_double (t, 6, 1, 0, pval, NULL);

  tab_title (t, _("ANOVA"));
  tab_submit (t);
}

static void
reg_stats_outs (linreg * c)
{
  assert (c != NULL);
}

static void
reg_stats_zpp (linreg * c)
{
  assert (c != NULL);
}

static void
reg_stats_label (linreg * c)
{
  assert (c != NULL);
}

static void
reg_stats_sha (linreg * c)
{
  assert (c != NULL);
}
static void
reg_stats_ci (linreg * c)
{
  assert (c != NULL);
}
static void
reg_stats_f (linreg * c)
{
  assert (c != NULL);
}
static void
reg_stats_bcov (linreg * c)
{
  int n_cols;
  int n_rows;
  int i;
  int k;
  int row;
  int col;
  const char *label;
  struct tab_table *t;

  assert (c != NULL);
  n_cols = c->n_indeps + 1 + 2;
  n_rows = 2 * (c->n_indeps + 1);
  t = tab_create (n_cols, n_rows, 0);
  tab_headers (t, 2, 0, 1, 0);
  tab_dim (t, tab_natural_dimensions, NULL);
  tab_box (t, TAL_2, TAL_2, -1, TAL_1, 0, 0, n_cols - 1, n_rows - 1);
  tab_hline (t, TAL_2, 0, n_cols - 1, 1);
  tab_vline (t, TAL_2, 2, 0, n_rows - 1);
  tab_vline (t, TAL_0, 1, 0, 0);
  tab_text (t, 0, 0, TAB_CENTER | TAT_TITLE, _("Model"));
  tab_text (t, 1, 1, TAB_CENTER | TAT_TITLE, _("Covariances"));
  for (i = 0; i < linreg_n_coeffs (c); i++)
    {
      const struct variable *v = linreg_indep_var (c, i);
      label = var_to_string (v);
      tab_text (t, 2, i, TAB_CENTER, label);
      tab_text (t, i + 2, 0, TAB_CENTER, label);
      for (k = 1; k < linreg_n_coeffs (c); k++)
	{
	  col = (i <= k) ? k : i;
	  row = (i <= k) ? i : k;
	  tab_double (t, k + 2, i, TAB_CENTER,
		     gsl_matrix_get (c->cov, row, col), NULL);
	}
    }
  tab_title (t, _("Coefficient Correlations"));
  tab_submit (t);
}
static void
reg_stats_ses (linreg * c)
{
  assert (c != NULL);
}
static void
reg_stats_xtx (linreg * c)
{
  assert (c != NULL);
}
static void
reg_stats_collin (linreg * c)
{
  assert (c != NULL);
}
static void
reg_stats_tol (linreg * c)
{
  assert (c != NULL);
}
static void
reg_stats_selection (linreg * c)
{
  assert (c != NULL);
}

static void
statistics_keyword_output (void (*function) (linreg *),
			   int keyword, linreg * c)
{
  if (keyword)
    {
      (*function) (c);
    }
}

static void
subcommand_statistics (int *keywords, linreg * c)
{
  /*
     The order here must match the order in which the STATISTICS
     keywords appear in the specification section above.
   */
  enum
  { r,
    coeff,
    anova,
    outs,
    zpp,
    label,
    sha,
    ci,
    bcov,
    ses,
    xtx,
    collin,
    tol,
    selection,
    f,
    defaults,
    all
  };
  int i;
  int d = 1;

  if (keywords[all])
    {
      /*
         Set everything but F.
       */
      for (i = 0; i < f; i++)
	{
	  keywords[i] = 1;
	}
    }
  else
    {
      for (i = 0; i < all; i++)
	{
	  if (keywords[i])
	    {
	      d = 0;
	    }
	}
      /*
         Default output: ANOVA table, parameter estimates,
         and statistics for variables not entered into model,
         if appropriate.
       */
      if (keywords[defaults] | d)
	{
	  keywords[anova] = 1;
	  keywords[outs] = 1;
	  keywords[coeff] = 1;
	  keywords[r] = 1;
	}
    }
  statistics_keyword_output (reg_stats_r, keywords[r], c);
  statistics_keyword_output (reg_stats_anova, keywords[anova], c);
  statistics_keyword_output (reg_stats_coeff, keywords[coeff], c);
  statistics_keyword_output (reg_stats_outs, keywords[outs], c);
  statistics_keyword_output (reg_stats_zpp, keywords[zpp], c);
  statistics_keyword_output (reg_stats_label, keywords[label], c);
  statistics_keyword_output (reg_stats_sha, keywords[sha], c);
  statistics_keyword_output (reg_stats_ci, keywords[ci], c);
  statistics_keyword_output (reg_stats_f, keywords[f], c);
  statistics_keyword_output (reg_stats_bcov, keywords[bcov], c);
  statistics_keyword_output (reg_stats_ses, keywords[ses], c);
  statistics_keyword_output (reg_stats_xtx, keywords[xtx], c);
  statistics_keyword_output (reg_stats_collin, keywords[collin], c);
  statistics_keyword_output (reg_stats_tol, keywords[tol], c);
  statistics_keyword_output (reg_stats_selection, keywords[selection], c);
}

/*
  Free the transformation. Free its linear model if this
  transformation is the last one.
 */
static bool
regression_trns_free (void *t_)
{
  bool result = true;
  struct reg_trns *t = t_;

  if (t->trns_id == t->n_trns)
    {
      result = linreg_free (t->c);
    }
  free (t);

  return result;
}

/*
  Gets the predicted values.
 */
static int
regression_trns_pred_proc (void *t_, struct ccase **c,
			   casenumber case_idx UNUSED)
{
  size_t i;
  size_t n_vals;
  struct reg_trns *trns = t_;
  linreg *model;
  union value *output = NULL;
  const union value *tmp;
  double *vals;
  const struct variable **vars = NULL;

  assert (trns != NULL);
  model = trns->c;
  assert (model != NULL);
  assert (model->depvar != NULL);
  assert (model->pred != NULL);

  vars = linreg_get_vars (model);
  n_vals = linreg_n_coeffs (model);
  vals = xnmalloc (n_vals, sizeof (*vals));
  *c = case_unshare (*c);

  output = case_data_rw (*c, model->pred);

  for (i = 0; i < n_vals; i++)
    {
      tmp = case_data (*c, vars[i]);
      vals[i] = tmp->f;
    }
  output->f = linreg_predict (model, vals, n_vals);
  free (vals);
  return TRNS_CONTINUE;
}

/*
  Gets the residuals.
 */
static int
regression_trns_resid_proc (void *t_, struct ccase **c,
			    casenumber case_idx UNUSED)
{
  size_t i;
  size_t n_vals;
  struct reg_trns *trns = t_;
  linreg *model;
  union value *output = NULL;
  const union value *tmp;
  double *vals = NULL;
  double obs;
  const struct variable **vars = NULL;

  assert (trns != NULL);
  model = trns->c;
  assert (model != NULL);
  assert (model->depvar != NULL);
  assert (model->resid != NULL);

  vars = linreg_get_vars (model);
  n_vals = linreg_n_coeffs (model);

  vals = xnmalloc (n_vals, sizeof (*vals));
  *c = case_unshare (*c);
  output = case_data_rw (*c, model->resid);
  assert (output != NULL);

  for (i = 0; i < n_vals; i++)
    {
      tmp = case_data (*c, vars[i]);
      vals[i] = tmp->f;
    }
  tmp = case_data (*c, model->depvar);
  obs = tmp->f;
  output->f = linreg_residual (model, obs, vals, n_vals);
  free (vals);

  return TRNS_CONTINUE;
}

/*
   Returns false if NAME is a duplicate of any existing variable name.
*/
static bool
try_name (const struct dictionary *dict, const char *name)
{
  if (dict_lookup_var (dict, name) != NULL)
    return false;

  return true;
}

static void
reg_get_name (const struct dictionary *dict, char name[VAR_NAME_LEN],
	      const char prefix[VAR_NAME_LEN])
{
  int i = 1;

  snprintf (name, VAR_NAME_LEN, "%s%d", prefix, i);
  while (!try_name (dict, name))
    {
      i++;
      snprintf (name, VAR_NAME_LEN, "%s%d", prefix, i);
    }
}

static void
reg_save_var (struct dataset *ds, const char *prefix, trns_proc_func * f,
	      linreg * c, struct variable **v, int n_trns)
{
  struct dictionary *dict = dataset_dict (ds);
  static int trns_index = 1;
  char name[VAR_NAME_LEN];
  struct variable *new_var;
  struct reg_trns *t = NULL;

  t = xmalloc (sizeof (*t));
  t->trns_id = trns_index;
  t->n_trns = n_trns;
  t->c = c;
  reg_get_name (dict, name, prefix);
  new_var = dict_create_var (dict, name, 0);
  assert (new_var != NULL);
  *v = new_var;
  add_transformation (ds, f, regression_trns_free, t);
  trns_index++;
}
static void
subcommand_save (struct dataset *ds, int save, linreg ** models)
{
  linreg **lc;
  int n_trns = 0;
  int i;

  if (save)
    {
      /* Count the number of transformations we will need. */
      for (i = 0; i < REGRESSION_SV_count; i++)
	{
	  if (cmd.a_save[i])
	    {
	      n_trns++;
	    }
	}
      n_trns *= cmd.n_dependent;

      for (lc = models; lc < models + cmd.n_dependent; lc++)
	{
	  if (*lc != NULL)
	    {
	      if ((*lc)->depvar != NULL)
		{
		  if (cmd.a_save[REGRESSION_SV_RESID])
		    {
		      reg_save_var (ds, "RES", regression_trns_resid_proc, *lc,
				    &(*lc)->resid, n_trns);
		    }
		  if (cmd.a_save[REGRESSION_SV_PRED])
		    {
		      reg_save_var (ds, "PRED", regression_trns_pred_proc, *lc,
				    &(*lc)->pred, n_trns);
		    }
		}
	    }
	}
    }
  else
    {
      for (lc = models; lc < models + cmd.n_dependent; lc++)
	{
	  if (*lc != NULL)
	    {
	      linreg_free (*lc);
	    }
	}
    }
}

int
cmd_regression (struct lexer *lexer, struct dataset *ds)
{
  struct casegrouper *grouper;
  struct casereader *group;
  linreg **models;
  bool ok;
  size_t i;

  if (!parse_regression (lexer, ds, &cmd, NULL))
    {
      return CMD_FAILURE;
    }

  models = xnmalloc (cmd.n_dependent, sizeof *models);
  for (i = 0; i < cmd.n_dependent; i++)
    {
      models[i] = NULL;
    }

  /* Data pass. */
  grouper = casegrouper_create_splits (proc_open (ds), dataset_dict (ds));
  while (casegrouper_get_next_group (grouper, &group))
    run_regression (group, &cmd, ds, models);
  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  subcommand_save (ds, cmd.sbc_save, models);
  free (v_variables);
  free (models);
  free_regression (&cmd);

  return ok ? CMD_SUCCESS : CMD_FAILURE;
}

/*
  Is variable k the dependent variable?
 */
static bool
is_depvar (size_t k, const struct variable *v)
{
  return v == v_variables[k];
}

/* Parser for the variables sub command */
static int
regression_custom_variables (struct lexer *lexer, struct dataset *ds,
			     struct cmd_regression *cmd UNUSED,
			     void *aux UNUSED)
{
  const struct dictionary *dict = dataset_dict (ds);

  lex_match (lexer, '=');

  if ((lex_token (lexer) != T_ID
       || dict_lookup_var (dict, lex_tokid (lexer)) == NULL)
      && lex_token (lexer) != T_ALL)
    return 2;


  if (!parse_variables_const
      (lexer, dict, &v_variables, &n_variables, PV_NONE))
    {
      free (v_variables);
      return 0;
    }
  assert (n_variables);

  return 1;
}

/* Identify the explanatory variables in v_variables.  Returns
   the number of independent variables. */
static int
identify_indep_vars (const struct variable **indep_vars,
		     const struct variable *depvar)
{
  int n_indep_vars = 0;
  int i;

  for (i = 0; i < n_variables; i++)
    if (!is_depvar (i, depvar))
      indep_vars[n_indep_vars++] = v_variables[i];
  if ((n_indep_vars < 1) && is_depvar (0, depvar))
    {
      /*
	There is only one independent variable, and it is the same
	as the dependent variable. Print a warning and continue.
       */
      msg (SE,
	   gettext ("The dependent variable is equal to the independent variable." 
		    "The least squares line is therefore Y=X." 
		    "Standard errors and related statistics may be meaningless."));
      n_indep_vars = 1;
      indep_vars[0] = v_variables[0];
    }
  return n_indep_vars;
}
static double
fill_covariance (gsl_matrix *cov, struct covariance *all_cov, 
		 const struct variable **vars,
		 size_t n_vars, const struct variable *dep_var, 
		 const struct variable **all_vars, size_t n_all_vars)
{
  size_t i;
  size_t j;
  size_t k = 0;
  size_t dep_subscript;
  size_t *rows;
  const gsl_matrix *ssizes;
  const gsl_matrix *cm;
  double result = 0.0;
  
  cm = covariance_calculate (all_cov);
  rows = xnmalloc (cov->size1 - 1, sizeof (*rows));
  
  for (i = 0; i < n_all_vars; i++)
    {
      for (j = k; j < n_vars; j++)
	{
	  if (vars[j] == all_vars[i])
	    {
	      if (vars[j] != dep_var)
		{
		  rows[j] = i;
		}
	      else
		{
		  dep_subscript = i;
		}
	      k++;
	      break;
	    }
	}
    }
  for (i = 0; i < cov->size1 - 1; i++)
    {
      for (j = 0; j < cov->size2 - 1; j++)
	{
	  gsl_matrix_set (cov, i, j, gsl_matrix_get (cm, rows[i], rows[j]));
	  gsl_matrix_set (cov, j, i, gsl_matrix_get (cm, rows[j], rows[i]));
	}
    }
  ssizes = covariance_moments (all_cov, MOMENT_NONE);
  result = gsl_matrix_get (ssizes, dep_subscript, rows[0]);
  for (i = 0; i < cov->size1 - 1; i++)
    {
      gsl_matrix_set (cov, i, cov->size1 - 1, 
		      gsl_matrix_get (cm, rows[i], dep_subscript));
      gsl_matrix_set (cov, cov->size1 - 1, i, 
		      gsl_matrix_get (cm, rows[i], dep_subscript));
      if (result > gsl_matrix_get (ssizes, rows[i], dep_subscript))
	{
	  result = gsl_matrix_get (ssizes, rows[i], dep_subscript);
	}
    }
  free (rows);
  return result;
}

static bool
run_regression (struct casereader *input, struct cmd_regression *cmd,
		struct dataset *ds, linreg **models)
{
  size_t i;
  int n_indep = 0;
  int k;
  double n_data;
  struct ccase *c;
  struct covariance *cov;
  const struct variable **vars;
  const struct variable *dep_var;
  struct casereader *reader;
  const struct dictionary *dict;
  gsl_matrix *this_cm;

  assert (models != NULL);

  for (i = 0; i < n_variables; i++)
    {
      if (!var_is_numeric (v_variables[i]))
	{
	  msg (SE, _("REGRESSION requires numeric variables."));
	  return false;
	}
    }

  c = casereader_peek (input, 0);
  if (c == NULL)
    {
      casereader_destroy (input);
      return true;
    }
  output_split_file_values (ds, c);
  case_unref (c);

  dict = dataset_dict (ds);
  if (!v_variables)
    {
      dict_get_vars (dict, &v_variables, &n_variables, 0);
    }
  vars = xnmalloc (n_variables, sizeof (*vars));
  cov = covariance_1pass_create (n_variables, v_variables,
				 dict_get_weight (dict), MV_ANY);

  reader = casereader_clone (input);
  reader = casereader_create_filter_missing (reader, v_variables, n_variables,
					     MV_ANY, NULL, NULL);
  for (; (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      covariance_accumulate (cov, c);
    }
  
  for (k = 0; k < cmd->n_dependent; k++)
    {
      dep_var = cmd->v_dependent[k];
      n_indep = identify_indep_vars (vars, dep_var);
      
      this_cm = gsl_matrix_alloc (n_indep + 1, n_indep + 1);
      n_data = fill_covariance (this_cm, cov, vars, n_indep, 
				       dep_var, v_variables, n_variables);
      models[k] = linreg_alloc (dep_var, (const struct variable **) vars,
				n_data, n_indep);
      models[k]->depvar = dep_var;
      
      /*
	For large data sets, use QR decomposition.
      */
      if (n_data > sqrt (n_indep) && n_data > REG_LARGE_DATA)
	{
	  models[k]->method = LINREG_QR;
	}
      
      if (n_data > 0)
	{
	  /*
	    Find the least-squares estimates and other statistics.
	  */
	  linreg_fit (this_cm, models[k]);
	  
	  if (!taint_has_tainted_successor (casereader_get_taint (input)))
	    {
	      subcommand_statistics (cmd->a_statistics, models[k]);
	    }
	}
      else
	{
	  msg (SE,
	       gettext ("No valid data found. This command was skipped."));
	  linreg_free (models[k]);
	  models[k] = NULL;
	}
    }

  casereader_destroy (reader);
  free (vars);
  casereader_destroy (input);
  covariance_destroy (cov);
  
  return true;
}

/*
  Local Variables:
  mode: c
  End:
*/
