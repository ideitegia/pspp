/* PSPP - linear regression.
   Copyright (C) 2005 Free Software Foundation, Inc.

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

#include <gsl/gsl_cdf.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>
#include <math.h>
#include <stdlib.h>

#include "regression-export.h"
#include <data/case.h>
#include <data/casefile.h>
#include <data/cat-routines.h>
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
#include <libpspp/alloc.h>
#include <libpspp/compiler.h>
#include <libpspp/message.h>
#include <math/design-matrix.h>
#include <math/coefficient.h>
#include <math/linreg/linreg.h>
#include <math/moments.h>
#include <output/table.h>

#include "gettext.h"

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
   export=custom;
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

/* Linear regression models. */
static pspp_linreg_cache **models = NULL;

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

/*
  File where the model will be saved if the EXPORT subcommand
  is given. 
 */
static struct file_handle *model_file;

/*
  Return value for the procedure.
 */
static int pspp_reg_rc = CMD_SUCCESS;

static bool run_regression (const struct ccase *,
			    const struct casefile *, void *, 
			    const struct dataset *);

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
  double t_stat;
  double pval;
  double coeff;
  double std_err;
  double beta;
  const char *label;
  char *tmp;
  const struct variable *v;
  const union value *val;
  const char *val_s;
  struct tab_table *t;

  assert (c != NULL);
  tmp = xnmalloc (MAX_STRING, sizeof (*tmp));
  n_rows = c->n_coeffs + 2;

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
  coeff = c->coeff[0]->estimate;
  tab_float (t, 2, 1, 0, coeff, 10, 2);
  std_err = sqrt (gsl_matrix_get (c->cov, 0, 0));
  tab_float (t, 3, 1, 0, std_err, 10, 2);
  beta = coeff / c->depvar_std;
  tab_float (t, 4, 1, 0, beta, 10, 2);
  t_stat = coeff / std_err;
  tab_float (t, 5, 1, 0, t_stat, 10, 2);
  pval = 2 * gsl_cdf_tdist_Q (fabs (t_stat), 1.0);
  tab_float (t, 6, 1, 0, pval, 10, 2);
  for (j = 1; j <= c->n_indeps; j++)
    {
      v = pspp_coeff_get_var (c->coeff[j], 0);
      label = var_to_string (v);
      /* Do not overwrite the variable's name. */
      strncpy (tmp, label, MAX_STRING);
      if (var_is_alpha (v))
	{
	  /*
	     Append the value associated with this coefficient.
	     This makes sense only if we us the usual binary encoding
	     for that value.
	   */

	  val = pspp_coeff_get_value (c->coeff[j], v);
	  val_s = var_get_value_name (v, val);
	  strncat (tmp, val_s, MAX_STRING);
	}

      tab_text (t, 1, j + 1, TAB_CENTER, tmp);
      /*
         Regression coefficients.
       */
      coeff = c->coeff[j]->estimate;
      tab_float (t, 2, j + 1, 0, coeff, 10, 2);
      /*
         Standard error of the coefficients.
       */
      std_err = sqrt (gsl_matrix_get (c->cov, j, j));
      tab_float (t, 3, j + 1, 0, std_err, 10, 2);
      /*
         'Standardized' coefficient, i.e., regression coefficient
         if all variables had unit variance.
       */
      beta = gsl_vector_get (c->indep_std, j);
      beta *= coeff / c->depvar_std;
      tab_float (t, 4, j + 1, 0, beta, 10, 2);

      /*
         Test statistic for H0: coefficient is 0.
       */
      t_stat = coeff / std_err;
      tab_float (t, 5, j + 1, 0, t_stat, 10, 2);
      /*
         P values for the test statistic above.
       */
      pval = 2 * gsl_cdf_tdist_Q (fabs (t_stat), (double) (c->n_obs - c->n_coeffs));
      tab_float (t, 6, j + 1, 0, pval, 10, 2);
    }
  tab_title (t, _("Coefficients"));
  tab_submit (t);
  free (tmp);
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
  tab_float (t, 3, 1, 0, c->dfm, 4, 0);
  tab_float (t, 3, 2, 0, c->dfe, 4, 0);
  tab_float (t, 3, 3, 0, c->dft, 4, 0);

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
  for (i = 1; i < c->n_coeffs; i++)
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
  struct variable **vars = NULL;

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
  struct variable **vars = NULL;

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
reg_get_name (const struct dictionary *dict, char name[LONG_NAME_LEN], const char prefix[LONG_NAME_LEN])
{
  int i = 1;

  snprintf (name, LONG_NAME_LEN, "%s%d", prefix, i);
  while (!try_name (dict, name))
    {
      i++;
      snprintf (name, LONG_NAME_LEN, "%s%d", prefix, i);
    }
}

static void
reg_save_var (struct dataset *ds, const char *prefix, trns_proc_func * f,
	      pspp_linreg_cache * c, struct variable **v, int n_trns)
{
  struct dictionary *dict = dataset_dict (ds);
  static int trns_index = 1;
  char name[LONG_NAME_LEN];
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
	  assert (*lc != NULL);
	  pspp_linreg_cache_free (*lc);
	}
    }
}

static int
reg_inserted (const struct variable *v, struct variable **varlist, int n_vars)
{
  int i;

  for (i = 0; i < n_vars; i++)
    {
      if (v == varlist[i])
	{
	  return 1;
	}
    }
  return 0;
}

static void
reg_print_categorical_encoding (FILE * fp, pspp_linreg_cache * c)
{
  int i;
  int n_vars = 0;
  struct variable **varlist;

  fprintf (fp, "%s", reg_export_categorical_encode_1);

  varlist = xnmalloc (c->n_indeps, sizeof (*varlist));
  for (i = 1; i < c->n_indeps; i++)	/* c->coeff[0] is the intercept. */
    {
      struct pspp_coeff *coeff = c->coeff[i];
      const struct variable *v = pspp_coeff_get_var (coeff, 0);
      if (var_is_alpha (v))
	{
	  if (!reg_inserted (v, varlist, n_vars))
	    {
	      fprintf (fp, "struct pspp_reg_categorical_variable %s;\n\t",
		       var_get_name (v));
	      varlist[n_vars] = (struct variable *) v;
	      n_vars++;
	    }
	}
    }
  fprintf (fp, "int n_vars = %d;\n\t", n_vars);
  fprintf (fp, "struct pspp_reg_categorical_variable *varlist[%d] = {",
	   n_vars);
  for (i = 0; i < n_vars - 1; i++)
    {
      fprintf (fp, "&%s,\n\t\t", var_get_name (varlist[i]));
    }
  fprintf (fp, "&%s};\n\t", var_get_name (varlist[i]));

  for (i = 0; i < n_vars; i++)
    {
      int n_categories = cat_get_n_categories (varlist[i]);
      int j;
      
      fprintf (fp, "%s.name = \"%s\";\n\t",
               var_get_name (varlist[i]),
	       var_get_name (varlist[i]));
      fprintf (fp, "%s.n_vals = %d;\n\t",
               var_get_name (varlist[i]),
               n_categories);

      for (j = 0; j < n_categories; j++)
	{
          union value *val = cat_subscript_to_value (j, varlist[i]);
	  fprintf (fp, "%s.values[%d] = \"%s\";\n\t",
                   var_get_name (varlist[i]), j,
		   var_get_value_name (varlist[i], val));
	}
    }
  fprintf (fp, "%s", reg_export_categorical_encode_2);
}

static void
reg_print_depvars (FILE * fp, pspp_linreg_cache * c)
{
  int i;
  struct pspp_coeff *coeff;
  const struct variable *v;

  fprintf (fp, "char *model_depvars[%d] = {", c->n_indeps);
  for (i = 1; i < c->n_indeps; i++)
    {
      coeff = c->coeff[i];
      v = pspp_coeff_get_var (coeff, 0);
      fprintf (fp, "\"%s\",\n\t\t", var_get_name (v));
    }
  coeff = c->coeff[i];
  v = pspp_coeff_get_var (coeff, 0);
  fprintf (fp, "\"%s\"};\n\t", var_get_name (v));
}
static void
reg_print_getvar (FILE * fp, pspp_linreg_cache * c)
{
  fprintf (fp, "static int\npspp_reg_getvar (char *v_name)\n{\n\t");
  fprintf (fp, "int i;\n\tint n_vars = %d;\n\t", c->n_indeps);
  reg_print_depvars (fp, c);
  fprintf (fp, "for (i = 0; i < n_vars; i++)\n\t{\n\t\t");
  fprintf (fp,
	   "if (strncmp (v_name, model_depvars[i], PSPP_REG_MAXLEN) == 0)\n\t\t{\n\t\t\t");
  fprintf (fp, "return i;\n\t\t}\n\t}\n}\n");
}
static int
reg_has_categorical (pspp_linreg_cache * c)
{
  int i;
  const struct variable *v;

  for (i = 1; i < c->n_coeffs; i++)
    {
      v = pspp_coeff_get_var (c->coeff[i], 0);
      if (var_is_alpha (v))
        return 1;
    }
  return 0;
}

static void
subcommand_export (int export, pspp_linreg_cache * c)
{
  FILE *fp;
  size_t i;
  size_t j;
  int n_quantiles = 100;
  double tmp;
  struct pspp_coeff *coeff;

  if (export)
    {
      assert (c != NULL);
      assert (model_file != NULL);
      fp = fopen (fh_get_file_name (model_file), "w");
      assert (fp != NULL);
      fprintf (fp, "%s", reg_preamble);
      reg_print_getvar (fp, c);
      if (reg_has_categorical (c))
	{
	  reg_print_categorical_encoding (fp, c);
	}
      fprintf (fp, "%s", reg_export_t_quantiles_1);
      for (i = 0; i < n_quantiles - 1; i++)
	{
	  tmp = 0.5 + 0.005 * (double) i;
	  fprintf (fp, "%.15e,\n\t\t",
		   gsl_cdf_tdist_Pinv (tmp, c->n_obs - c->n_indeps));
	}
      fprintf (fp, "%.15e};\n\t",
	       gsl_cdf_tdist_Pinv (.9995, c->n_obs - c->n_indeps));
      fprintf (fp, "%s", reg_export_t_quantiles_2);
      fprintf (fp, "%s", reg_mean_cmt);
      fprintf (fp, "double\npspp_reg_estimate (const double *var_vals,");
      fprintf (fp, "const char *var_names[])\n{\n\t");
      fprintf (fp, "double model_coeffs[%d] = {", c->n_indeps);
      for (i = 1; i < c->n_indeps; i++)
	{
	  coeff = c->coeff[i];
	  fprintf (fp, "%.15e,\n\t\t", coeff->estimate);
	}
      coeff = c->coeff[i];
      fprintf (fp, "%.15e};\n\t", coeff->estimate);
      coeff = c->coeff[0];
      fprintf (fp, "double estimate = %.15e;\n\t", coeff->estimate);
      fprintf (fp, "int i;\n\tint j;\n\n\t");
      fprintf (fp, "for (i = 0; i < %d; i++)\n\t", c->n_indeps);
      fprintf (fp, "%s", reg_getvar);
      fprintf (fp, "const double cov[%d][%d] = {\n\t", c->n_coeffs,
	       c->n_coeffs);
      for (i = 0; i < c->cov->size1 - 1; i++)
	{
	  fprintf (fp, "{");
	  for (j = 0; j < c->cov->size2 - 1; j++)
	    {
	      fprintf (fp, "%.15e, ", gsl_matrix_get (c->cov, i, j));
	    }
	  fprintf (fp, "%.15e},\n\t", gsl_matrix_get (c->cov, i, j));
	}
      fprintf (fp, "{");
      for (j = 0; j < c->cov->size2 - 1; j++)
	{
	  fprintf (fp, "%.15e, ",
		   gsl_matrix_get (c->cov, c->cov->size1 - 1, j));
	}
      fprintf (fp, "%.15e}\n\t",
	       gsl_matrix_get (c->cov, c->cov->size1 - 1, c->cov->size2 - 1));
      fprintf (fp, "};\n\tint n_vars = %d;\n\tint i;\n\tint j;\n\t",
	       c->n_indeps);
      fprintf (fp, "double unshuffled_vals[%d];\n\t", c->n_indeps);
      fprintf (fp, "%s", reg_variance);
      fprintf (fp, "%s", reg_export_confidence_interval);
      tmp = c->mse * c->mse;
      fprintf (fp, "%s %.15e", reg_export_prediction_interval_1, tmp);
      fprintf (fp, "%s %.15e", reg_export_prediction_interval_2, tmp);
      fprintf (fp, "%s", reg_export_prediction_interval_3);
      fclose (fp);
      fp = fopen ("pspp_model_reg.h", "w");
      fprintf (fp, "%s", reg_header);
      fclose (fp);
    }
}

static int
regression_custom_export (struct lexer *lexer, struct dataset *ds UNUSED, struct cmd_regression *cmd UNUSED, void *aux UNUSED)
{
  /* 0 on failure, 1 on success, 2 on failure that should result in syntax error */
  if (!lex_force_match (lexer, '('))
    return 0;

  if (lex_match (lexer, '*'))
    model_file = NULL;
  else
    {
      model_file = fh_parse (lexer, FH_REF_FILE);
      if (model_file == NULL)
	return 0;
    }

  if (!lex_force_match (lexer, ')'))
    return 0;

  return 1;
}

int
cmd_regression (struct lexer *lexer, struct dataset *ds)
{
  if (!parse_regression (lexer, ds, &cmd, NULL))
    return CMD_FAILURE;

  models = xnmalloc (cmd.n_dependent, sizeof *models);
  if (!multipass_procedure_with_splits (ds, run_regression, &cmd))
    return CMD_CASCADING_FAILURE;
  subcommand_save (ds, cmd.sbc_save, models);
  free (v_variables);
  free (models);
  return pspp_reg_rc;
}

/*
  Is variable k the dependent variable?
 */
static bool
is_depvar (size_t k, const struct variable *v)
{
  return v == v_variables[k];
}

/*
  Mark missing cases. Return the number of non-missing cases.
  Compute the first two moments.
 */
static size_t
mark_missing_cases (const struct casefile *cf, const struct variable *v,
		    int *is_missing_case, double n_data,
                    struct moments_var *mom)
{
  struct casereader *r;
  struct ccase c;
  size_t row;
  const union value *val;
  double w = 1.0;

  for (r = casefile_get_reader (cf, NULL);
       casereader_read (r, &c); case_destroy (&c))
    {
      row = casereader_cnum (r) - 1;

      val = case_data (&c, v);
      if (mom != NULL)
	{
	  moments1_add (mom->m, val->f, w);
	}
      cat_value_update (v, val);
      if (var_is_value_missing (v, val, MV_ANY))
	{
	  if (!is_missing_case[row])
	    {
	      /* Now it is missing. */
	      n_data--;
	      is_missing_case[row] = 1;
	    }
	}
    }
  casereader_destroy (r);

  return n_data;
}

/* Parser for the variables sub command */
static int
regression_custom_variables (struct lexer *lexer, struct dataset *ds, 
			     struct cmd_regression *cmd UNUSED,
			     void *aux UNUSED)
{
  const struct dictionary *dict = dataset_dict (ds);

  lex_match (lexer, '=');

  if ((lex_token (lexer) != T_ID || dict_lookup_var (dict, lex_tokid (lexer)) == NULL)
      && lex_token (lexer) != T_ALL)
    return 2;


  if (!parse_variables_const (lexer, dict, &v_variables, &n_variables, PV_NONE))
    {
      free (v_variables);
      return 0;
    }
  assert (n_variables);

  return 1;
}

/*
  Count the explanatory variables. The user may or may
  not have specified a response variable in the syntax.
 */
static int
get_n_indep (const struct variable *v)
{
  int result;
  int i = 0;

  result = n_variables;
  while (i < n_variables)
    {
      if (is_depvar (i, v))
	{
	  result--;
	  i = n_variables;
	}
      i++;
    }
  return result;
}

/*
  Read from the active file. Identify the explanatory variables in
  v_variables. Encode categorical variables. Drop cases with missing
  values.
*/
static int
prepare_data (int n_data, int is_missing_case[],
	      const struct variable **indep_vars,
	      const struct variable *depvar, const struct casefile *cf,
              struct moments_var *mom)
{
  int i;
  int j;

  assert (indep_vars != NULL);
  j = 0;
  for (i = 0; i < n_variables; i++)
    {
      if (!is_depvar (i, depvar))
	{
	  indep_vars[j] = v_variables[i];
	  j++;
	  if (var_is_alpha (v_variables[i]))
	    {
	      /* Make a place to hold the binary vectors 
	         corresponding to this variable's values. */
	      cat_stored_values_create (v_variables[i]);
	    }
	  n_data =
	    mark_missing_cases (cf, v_variables[i], is_missing_case, n_data, mom + i);
	}
    }
  /*
     Mark missing cases for the dependent variable.
   */
  n_data = mark_missing_cases (cf, depvar, is_missing_case, n_data, NULL);

  return n_data;
}
static void
coeff_init (pspp_linreg_cache * c, struct design_matrix *dm)
{
  c->coeff = xnmalloc (dm->m->size2 + 1, sizeof (*c->coeff));
  c->coeff[0] = xmalloc (sizeof (*(c->coeff[0])));	/* The first coefficient is the intercept. */
  c->coeff[0]->v_info = NULL;	/* Intercept has no associated variable. */
  pspp_coeff_init (c->coeff + 1, dm);
}

/*
  Put the moments in the linreg cache.
 */
static void
compute_moments (pspp_linreg_cache *c, struct moments_var *mom, struct design_matrix *dm, size_t n)
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
	      gsl_vector_set (c->indep_means, i, mean);
	      gsl_vector_set (c->indep_std, i, sqrt (variance));
	    }
	}
    }
}
static bool
run_regression (const struct ccase *first,
		const struct casefile *cf, void *cmd_ UNUSED, const struct dataset *ds)
{
  size_t i;
  size_t n_data = 0;		/* Number of valide cases. */
  size_t n_cases;		/* Number of cases. */
  size_t row;
  size_t case_num;
  int n_indep = 0;
  int k;
  /*
     Keep track of the missing cases.
   */
  int *is_missing_case;
  const union value *val;
  struct casereader *r;
  struct ccase c;
  const struct variable **indep_vars;
  struct design_matrix *X;
  struct moments_var *mom;
  gsl_vector *Y;

  pspp_linreg_opts lopts;

  assert (models != NULL);

  output_split_file_values (ds, first);

  if (!v_variables)
    {
      dict_get_vars (dataset_dict (ds), &v_variables, &n_variables,
		     1u << DC_SYSTEM);
    }

  n_cases = casefile_get_case_cnt (cf);

  for (i = 0; i < cmd.n_dependent; i++)
    {
      if (!var_is_numeric (cmd.v_dependent[i]))
	{
	  msg (SE, gettext ("Dependent variable must be numeric."));
	  pspp_reg_rc = CMD_FAILURE;
	  return true;
	}
    }

  is_missing_case = xnmalloc (n_cases, sizeof (*is_missing_case));
  mom = xnmalloc (n_variables, sizeof (*mom));
  for (i = 0; i < n_variables; i++)
    {
      (mom + i)->m = moments1_create (MOMENT_VARIANCE);
      (mom + i)->v = v_variables[i];
    }
  lopts.get_depvar_mean_std = 1;

  for (k = 0; k < cmd.n_dependent; k++)
    {
      n_indep = get_n_indep ((const struct variable *) cmd.v_dependent[k]);
      lopts.get_indep_mean_std = xnmalloc (n_indep, sizeof (int));
      indep_vars = xnmalloc (n_indep, sizeof *indep_vars);
      assert (indep_vars != NULL);

      for (i = 0; i < n_cases; i++)
	{
	  is_missing_case[i] = 0;
	}
      n_data = prepare_data (n_cases, is_missing_case, indep_vars,
			     cmd.v_dependent[k],
			     (const struct casefile *) cf, mom);
      Y = gsl_vector_alloc (n_data);

      X =
	design_matrix_create (n_indep, (const struct variable **) indep_vars,
			      n_data);
      for (i = 0; i < X->m->size2; i++)
	{
	  lopts.get_indep_mean_std[i] = 1;
	}
      models[k] = pspp_linreg_cache_alloc (X->m->size1, X->m->size2);
      models[k]->indep_means = gsl_vector_alloc (X->m->size2);
      models[k]->indep_std = gsl_vector_alloc (X->m->size2);
      models[k]->depvar = (const struct variable *) cmd.v_dependent[k];
      /*
         For large data sets, use QR decomposition.
       */
      if (n_data > sqrt (n_indep) && n_data > REG_LARGE_DATA)
	{
	  models[k]->method = PSPP_LINREG_SVD;
	}

      /*
         The second pass fills the design matrix.
       */
      row = 0;
      for (r = casefile_get_reader (cf, NULL); casereader_read (r, &c);
	   case_destroy (&c))
	/* Iterate over the cases. */
	{
	  case_num = casereader_cnum (r) - 1;
	  if (!is_missing_case[case_num])
	    {
	      for (i = 0; i < n_variables; ++i)	/* Iterate over the
						   variables for the
						   current case.
						 */
		{
		  val = case_data (&c, v_variables[i]);
		  /*
		     Independent/dependent variable separation. The
		     'variables' subcommand specifies a varlist which contains
		     both dependent and independent variables. The dependent
		     variables are specified with the 'dependent'
		     subcommand, and maybe also in the 'variables' subcommand. 
		     We need to separate the two.
		   */
		  if (!is_depvar (i, cmd.v_dependent[k]))
		    {
		      if (var_is_alpha (v_variables[i]))
			{
			  design_matrix_set_categorical (X, row,
							 v_variables[i], val);
			}
		      else
			{
			  design_matrix_set_numeric (X, row, v_variables[i],
						     val);
			}
		    }
		}
	      val = case_data (&c, cmd.v_dependent[k]);
	      gsl_vector_set (Y, row, val->f);
	      row++;
	    }
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
      pspp_linreg ((const gsl_vector *) Y, X->m, &lopts, models[k]);
      compute_moments (models[k], mom, X, n_variables);
      subcommand_statistics (cmd.a_statistics, models[k]);
      subcommand_export (cmd.sbc_export, models[k]);

      gsl_vector_free (Y);
      design_matrix_destroy (X);
      free (indep_vars);
      free (lopts.get_indep_mean_std);
      casereader_destroy (r);
    }
  for (i = 0; i < n_variables; i++)
    {
      moments1_destroy ((mom + i)->m);
    }
  free (mom);
  free (is_missing_case);

  return true;
}

/*
  Local Variables:   
  mode: c
  End:
*/
