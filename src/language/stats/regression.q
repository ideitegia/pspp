/* PSPP - a program for statistical analysis.
   Copyright (C) 2005 Free Software Foundation, Inc.

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
#include <math/design-matrix.h>
#include <math/coefficient.h>
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
  pspp_linreg_cache *c;		/* Linear model for this trns. */
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
			    struct dataset *, pspp_linreg_cache **);

/*
   STATISTICS subcommand output functions.
 */
static void reg_stats_r (pspp_linreg_cache *);
static void reg_stats_coeff (pspp_linreg_cache *);
static void reg_stats_anova (pspp_linreg_cache *);
static void reg_stats_outs (pspp_linreg_cache *);
static void reg_stats_zpp (pspp_linreg_cache *);
static void reg_stats_label (pspp_linreg_cache *);
static void reg_stats_sha (pspp_linreg_cache *);
static void reg_stats_ci (pspp_linreg_cache *);
static void reg_stats_f (pspp_linreg_cache *);
static void reg_stats_bcov (pspp_linreg_cache *);
static void reg_stats_ses (pspp_linreg_cache *);
static void reg_stats_xtx (pspp_linreg_cache *);
static void reg_stats_collin (pspp_linreg_cache *);
static void reg_stats_tol (pspp_linreg_cache *);
static void reg_stats_selection (pspp_linreg_cache *);
static void statistics_keyword_output (void (*)(pspp_linreg_cache *),
				       int, pspp_linreg_cache *);

static void
reg_stats_r (pspp_linreg_cache * c)
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
  std_error = sqrt ((c->n_indeps - 1.0) / (c->n_obs - 1.0));
  t = tab_create (n_cols, n_rows, 0);
  tab_dim (t, tab_natural_dimensions);
  tab_box (t, TAL_2, TAL_2, -1, TAL_1, 0, 0, n_cols - 1, n_rows - 1);
  tab_hline (t, TAL_2, 0, n_cols - 1, 1);
  tab_vline (t, TAL_2, 2, 0, n_rows - 1);
  tab_vline (t, TAL_0, 1, 0, 0);

  tab_text (t, 1, 0, TAB_CENTER | TAT_TITLE, _("R"));
  tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("R Square"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("Adjusted R Square"));
  tab_text (t, 4, 0, TAB_CENTER | TAT_TITLE, _("Std. Error of the Estimate"));
  tab_float (t, 1, 1, TAB_RIGHT, sqrt (rsq), 10, 2);
  tab_float (t, 2, 1, TAB_RIGHT, rsq, 10, 2);
  tab_float (t, 3, 1, TAB_RIGHT, adjrsq, 10, 2);
  tab_float (t, 4, 1, TAB_RIGHT, std_error, 10, 2);
  tab_title (t, _("Model Summary"));
  tab_submit (t);
}

/*
  Table showing estimated regression coefficients.
 */
static void
reg_stats_coeff (pspp_linreg_cache * c)
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
  const union value *val;
  struct tab_table *t;

  assert (c != NULL);
  n_rows = c->n_coeffs + 3;

  t = tab_create (n_cols, n_rows, 0);
  tab_headers (t, 2, 0, 1, 0);
  tab_dim (t, tab_natural_dimensions);
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
  tab_float (t, 2, 1, 0, c->intercept, 10, 2);
  std_err = sqrt (gsl_matrix_get (c->cov, 0, 0));
  tab_float (t, 3, 1, 0, std_err, 10, 2);
  tab_float (t, 4, 1, 0, 0.0, 10, 2);
  t_stat = c->intercept / std_err;
  tab_float (t, 5, 1, 0, t_stat, 10, 2);
  pval = 2 * gsl_cdf_tdist_Q (fabs (t_stat), 1.0);
  tab_float (t, 6, 1, 0, pval, 10, 2);
  for (j = 0; j < c->n_coeffs; j++)
    {
      struct string tstr;
      ds_init_empty (&tstr);
      this_row = j + 2;

      v = pspp_coeff_get_var (c->coeff[j], 0);
      label = var_to_string (v);
      /* Do not overwrite the variable's name. */
      ds_put_cstr (&tstr, label);
      if (var_is_alpha (v))
	{
	  /*
	     Append the value associated with this coefficient.
	     This makes sense only if we us the usual binary encoding
	     for that value.
	   */

	  val = pspp_coeff_get_value (c->coeff[j], v);

	  var_append_value_name (v, val, &tstr);
	}

      tab_text (t, 1, this_row, TAB_CENTER, ds_cstr (&tstr));
      /*
         Regression coefficients.
       */
      tab_float (t, 2, this_row, 0, c->coeff[j]->estimate, 10, 2);
      /*
         Standard error of the coefficients.
       */
      std_err = sqrt (gsl_matrix_get (c->cov, j + 1, j + 1));
      tab_float (t, 3, this_row, 0, std_err, 10, 2);
      /*
         Standardized coefficient, i.e., regression coefficient
         if all variables had unit variance.
       */
      beta = pspp_coeff_get_sd (c->coeff[j]);
      beta *= c->coeff[j]->estimate / c->depvar_std;
      tab_float (t, 4, this_row, 0, beta, 10, 2);

      /*
         Test statistic for H0: coefficient is 0.
       */
      t_stat = c->coeff[j]->estimate / std_err;
      tab_float (t, 5, this_row, 0, t_stat, 10, 2);
      /*
         P values for the test statistic above.
       */
      pval =
	2 * gsl_cdf_tdist_Q (fabs (t_stat),
			     (double) (c->n_obs - c->n_coeffs));
      tab_float (t, 6, this_row, 0, pval, 10, 2);
      ds_destroy (&tstr);
    }
  tab_title (t, _("Coefficients"));
  tab_submit (t);
}

/*
  Display the ANOVA table.
 */
static void
reg_stats_anova (pspp_linreg_cache * c)
{
  int n_cols = 7;
  int n_rows = 4;
  const double msm = c->ssm / c->dfm;
  const double mse = c->sse / c->dfe;
  const double F = msm / mse;
  const double pval = gsl_cdf_fdist_Q (F, c->dfm, c->dfe);

  struct tab_table *t;

  assert (c != NULL);
  t = tab_create (n_cols, n_rows, 0);
  tab_headers (t, 2, 0, 1, 0);
  tab_dim (t, tab_natural_dimensions);

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
  tab_float (t, 2, 1, 0, c->ssm, 10, 2);
  tab_float (t, 2, 3, 0, c->sst, 10, 2);
  tab_float (t, 2, 2, 0, c->sse, 10, 2);


  /* Degrees of freedom */
  tab_text (t, 3, 1, TAB_RIGHT | TAT_PRINTF, "%g", c->dfm);
  tab_text (t, 3, 2, TAB_RIGHT | TAT_PRINTF, "%g", c->dfe);
  tab_text (t, 3, 3, TAB_RIGHT | TAT_PRINTF, "%g", c->dft);

  /* Mean Squares */
  tab_float (t, 4, 1, TAB_RIGHT, msm, 8, 3);
  tab_float (t, 4, 2, TAB_RIGHT, mse, 8, 3);

  tab_float (t, 5, 1, 0, F, 8, 3);

  tab_float (t, 6, 1, 0, pval, 8, 3);

  tab_title (t, _("ANOVA"));
  tab_submit (t);
}

static void
reg_stats_outs (pspp_linreg_cache * c)
{
  assert (c != NULL);
}

static void
reg_stats_zpp (pspp_linreg_cache * c)
{
  assert (c != NULL);
}

static void
reg_stats_label (pspp_linreg_cache * c)
{
  assert (c != NULL);
}

static void
reg_stats_sha (pspp_linreg_cache * c)
{
  assert (c != NULL);
}
static void
reg_stats_ci (pspp_linreg_cache * c)
{
  assert (c != NULL);
}
static void
reg_stats_f (pspp_linreg_cache * c)
{
  assert (c != NULL);
}
static void
reg_stats_bcov (pspp_linreg_cache * c)
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
  tab_dim (t, tab_natural_dimensions);
  tab_box (t, TAL_2, TAL_2, -1, TAL_1, 0, 0, n_cols - 1, n_rows - 1);
  tab_hline (t, TAL_2, 0, n_cols - 1, 1);
  tab_vline (t, TAL_2, 2, 0, n_rows - 1);
  tab_vline (t, TAL_0, 1, 0, 0);
  tab_text (t, 0, 0, TAB_CENTER | TAT_TITLE, _("Model"));
  tab_text (t, 1, 1, TAB_CENTER | TAT_TITLE, _("Covariances"));
  for (i = 0; i < c->n_coeffs; i++)
    {
      const struct variable *v = pspp_coeff_get_var (c->coeff[i], 0);
      label = var_to_string (v);
      tab_text (t, 2, i, TAB_CENTER, label);
      tab_text (t, i + 2, 0, TAB_CENTER, label);
      for (k = 1; k < c->n_coeffs; k++)
	{
	  col = (i <= k) ? k : i;
	  row = (i <= k) ? i : k;
	  tab_float (t, k + 2, i, TAB_CENTER,
		     gsl_matrix_get (c->cov, row, col), 8, 3);
	}
    }
  tab_title (t, _("Coefficient Correlations"));
  tab_submit (t);
}
static void
reg_stats_ses (pspp_linreg_cache * c)
{
  assert (c != NULL);
}
static void
reg_stats_xtx (pspp_linreg_cache * c)
{
  assert (c != NULL);
}
static void
reg_stats_collin (pspp_linreg_cache * c)
{
  assert (c != NULL);
}
static void
reg_stats_tol (pspp_linreg_cache * c)
{
  assert (c != NULL);
}
static void
reg_stats_selection (pspp_linreg_cache * c)
{
  assert (c != NULL);
}

static void
statistics_keyword_output (void (*function) (pspp_linreg_cache *),
			   int keyword, pspp_linreg_cache * c)
{
  if (keyword)
    {
      (*function) (c);
    }
}

static void
subcommand_statistics (int *keywords, pspp_linreg_cache * c)
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
      result = pspp_linreg_cache_free (t->c);
    }
  free (t);

  return result;
}

/*
  Gets the predicted values.
 */
static int
regression_trns_pred_proc (void *t_, struct ccase *c,
			   casenumber case_idx UNUSED)
{
  size_t i;
  size_t n_vals;
  struct reg_trns *trns = t_;
  pspp_linreg_cache *model;
  union value *output = NULL;
  const union value **vals = NULL;
  const struct variable **vars = NULL;

  assert (trns != NULL);
  model = trns->c;
  assert (model != NULL);
  assert (model->depvar != NULL);
  assert (model->pred != NULL);

  vars = xnmalloc (model->n_coeffs, sizeof (*vars));
  n_vals = (*model->get_vars) (model, vars);

  vals = xnmalloc (n_vals, sizeof (*vals));
  output = case_data_rw (c, model->pred);
  assert (output != NULL);

  for (i = 0; i < n_vals; i++)
    {
      vals[i] = case_data (c, vars[i]);
    }
  output->f = (*model->predict) ((const struct variable **) vars,
				 vals, model, n_vals);
  free (vals);
  free (vars);
  return TRNS_CONTINUE;
}

/*
  Gets the residuals.
 */
static int
regression_trns_resid_proc (void *t_, struct ccase *c,
			    casenumber case_idx UNUSED)
{
  size_t i;
  size_t n_vals;
  struct reg_trns *trns = t_;
  pspp_linreg_cache *model;
  union value *output = NULL;
  const union value **vals = NULL;
  const union value *obs = NULL;
  const struct variable **vars = NULL;

  assert (trns != NULL);
  model = trns->c;
  assert (model != NULL);
  assert (model->depvar != NULL);
  assert (model->resid != NULL);

  vars = xnmalloc (model->n_coeffs, sizeof (*vars));
  n_vals = (*model->get_vars) (model, vars);

  vals = xnmalloc (n_vals, sizeof (*vals));
  output = case_data_rw (c, model->resid);
  assert (output != NULL);

  for (i = 0; i < n_vals; i++)
    {
      vals[i] = case_data (c, vars[i]);
    }
  obs = case_data (c, model->depvar);
  output->f = (*model->residual) ((const struct variable **) vars,
				  vals, obs, model, n_vals);
  free (vals);
  free (vars);
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
	      pspp_linreg_cache * c, struct variable **v, int n_trns)
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
subcommand_save (struct dataset *ds, int save, pspp_linreg_cache ** models)
{
  pspp_linreg_cache **lc;
  int n_trns = 0;
  int i;

  assert (models != NULL);

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
	  assert (*lc != NULL);
	  assert ((*lc)->depvar != NULL);
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
  else
    {
      for (lc = models; lc < models + cmd.n_dependent; lc++)
	{
	  if (*lc != NULL)
	    {
	      pspp_linreg_cache_free (*lc);
	    }
	}
    }
}

int
cmd_regression (struct lexer *lexer, struct dataset *ds)
{
  struct casegrouper *grouper;
  struct casereader *group;
  pspp_linreg_cache **models;
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

/* Encode categorical variables.
   Returns number of valid cases. */
static int
prepare_categories (struct casereader *input,
		    const struct variable **vars, size_t n_vars,
		    struct moments_var *mom)
{
  int n_data;
  struct ccase c;
  size_t i;

  assert (vars != NULL);
  assert (mom != NULL);

  for (i = 0; i < n_vars; i++)
    if (var_is_alpha (vars[i]))
      cat_stored_values_create (vars[i]);

  n_data = 0;
  for (; casereader_read (input, &c); case_destroy (&c))
    {
      /*
         The second condition ensures the program will run even if
         there is only one variable to act as both explanatory and
         response.
       */
      for (i = 0; i < n_vars; i++)
	{
	  const union value *val = case_data (&c, vars[i]);
	  if (var_is_alpha (vars[i]))
	    cat_value_update (vars[i], val);
	  else
	    moments1_add (mom[i].m, val->f, 1.0);
	}
      n_data++;
    }
  casereader_destroy (input);

  return n_data;
}

static void
coeff_init (pspp_linreg_cache * c, struct design_matrix *dm)
{
  c->coeff = xnmalloc (dm->m->size2, sizeof (*c->coeff));
  pspp_coeff_init (c->coeff, dm);
}

/*
  Put the moments in the linreg cache.
 */
static void
compute_moments (pspp_linreg_cache * c, struct moments_var *mom,
		 struct design_matrix *dm, size_t n)
{
  size_t i;
  size_t j;
  double weight;
  double mean;
  double variance;
  double skewness;
  double kurtosis;
  /*
     Scan the variable names in the columns of the design matrix.
     When we find the variable we need, insert its mean in the cache.
   */
  for (i = 0; i < dm->m->size2; i++)
    {
      for (j = 0; j < n; j++)
	{
	  if (design_matrix_col_to_var (dm, i) == (mom + j)->v)
	    {
	      moments1_calculate ((mom + j)->m, &weight, &mean, &variance,
				  &skewness, &kurtosis);
	      pspp_linreg_set_indep_variable_mean (c, (mom + j)->v, mean);
	      pspp_linreg_set_indep_variable_sd (c, (mom + j)->v, sqrt (variance));
	    }
	}
    }
}

static bool
run_regression (struct casereader *input, struct cmd_regression *cmd,
		struct dataset *ds, pspp_linreg_cache **models)
{
  size_t i;
  int n_indep = 0;
  int k;
  struct ccase c;
  const struct variable **indep_vars;
  struct design_matrix *X;
  struct moments_var *mom;
  gsl_vector *Y;

  pspp_linreg_opts lopts;

  assert (models != NULL);

  if (!casereader_peek (input, 0, &c))
    {
      casereader_destroy (input);
      return true;
    }
  output_split_file_values (ds, &c);
  case_destroy (&c);

  if (!v_variables)
    {
      dict_get_vars (dataset_dict (ds), &v_variables, &n_variables, 0);
    }

  for (i = 0; i < cmd->n_dependent; i++)
    {
      if (!var_is_numeric (cmd->v_dependent[i]))
	{
	  msg (SE, _("Dependent variable must be numeric."));
	  return false;
	}
    }

  mom = xnmalloc (n_variables, sizeof (*mom));
  for (i = 0; i < n_variables; i++)
    {
      (mom + i)->m = moments1_create (MOMENT_VARIANCE);
      (mom + i)->v = v_variables[i];
    }
  lopts.get_depvar_mean_std = 1;

  lopts.get_indep_mean_std = xnmalloc (n_variables, sizeof (int));
  indep_vars = xnmalloc (n_variables, sizeof *indep_vars);

  for (k = 0; k < cmd->n_dependent; k++)
    {
      const struct variable *dep_var;
      struct casereader *reader;
      casenumber row;
      struct ccase c;
      size_t n_data;		/* Number of valid cases. */

      dep_var = cmd->v_dependent[k];
      n_indep = identify_indep_vars (indep_vars, dep_var);
      reader = casereader_clone (input);
      reader = casereader_create_filter_missing (reader, indep_vars, n_indep,
						 MV_ANY, NULL, NULL);
      reader = casereader_create_filter_missing (reader, &dep_var, 1,
						 MV_ANY, NULL, NULL);
      n_data = prepare_categories (casereader_clone (reader),
				   indep_vars, n_indep, mom);

      if ((n_data > 0) && (n_indep > 0))
	{
	  Y = gsl_vector_alloc (n_data);
	  X =
	    design_matrix_create (n_indep,
				  (const struct variable **) indep_vars,
				  n_data);
	  for (i = 0; i < X->m->size2; i++)
	    {
	      lopts.get_indep_mean_std[i] = 1;
	    }
	  models[k] = pspp_linreg_cache_alloc (dep_var, (const struct variable **) indep_vars,
					       X->m->size1, X->m->size2);
	  models[k]->depvar = dep_var;
	  /*
	     For large data sets, use QR decomposition.
	   */
	  if (n_data > sqrt (n_indep) && n_data > REG_LARGE_DATA)
	    {
	      models[k]->method = PSPP_LINREG_QR;
	    }

	  /*
	     The second pass fills the design matrix.
	   */
	  reader = casereader_create_counter (reader, &row, -1);
	  for (; casereader_read (reader, &c); case_destroy (&c))
	    {
	      for (i = 0; i < n_indep; ++i)
		{
		  const struct variable *v = indep_vars[i];
		  const union value *val = case_data (&c, v);
		  if (var_is_alpha (v))
		    design_matrix_set_categorical (X, row, v, val);
		  else
		    design_matrix_set_numeric (X, row, v, val);
		}
	      gsl_vector_set (Y, row, case_num (&c, dep_var));
	    }
	  /*
	     Now that we know the number of coefficients, allocate space
	     and store pointers to the variables that correspond to the
	     coefficients.
	   */
	  coeff_init (models[k], X);

	  /*
	     Find the least-squares estimates and other statistics.
	   */
	  pspp_linreg ((const gsl_vector *) Y, X, &lopts, models[k]);

	  if (!taint_has_tainted_successor (casereader_get_taint (input)))
	    {
	      subcommand_statistics (cmd->a_statistics, models[k]);
	    }

	  gsl_vector_free (Y);
	  design_matrix_destroy (X);
	}
      else
	{
	  msg (SE,
	       gettext ("No valid data found. This command was skipped."));
	}
      casereader_destroy (reader);
    }
  for (i = 0; i < n_variables; i++)
    {
      moments1_destroy ((mom + i)->m);
    }
  free (mom);
  free (indep_vars);
  free (lopts.get_indep_mean_std);
  casereader_destroy (input);

  return true;
}

/*
  Local Variables:
  mode: c
  End:
*/
