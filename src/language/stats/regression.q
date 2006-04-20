/* PSPP - linear regression.
   Copyright (C) 2005 Free Software Foundation, Inc.
   Written by Jason H Stover <jason@sakla.net>.

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
#include <stdlib.h>
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>
#include <math.h>
#include <libpspp/alloc.h>
#include <data/case.h>
#include <data/casefile.h>
#include <data/category.h>
#include <data/cat-routines.h>
#include <language/command.h>
#include <libpspp/compiler.h>
#include <math/design-matrix.h>
#include <data/dictionary.h>
#include <libpspp/message.h>
#include <language/data-io/file-handle.h>
#include "gettext.h"
#include <language/lexer/lexer.h>
#include <math/linreg/linreg.h>
#include <math/linreg/coefficient.h>
#include <data/missing-values.h>
#include "regression-export.h"
#include <output/table.h>
#include <data/value-labels.h>
#include <data/variable.h>
#include <procedure.h>

#define REG_LARGE_DATA 1000

/* (headers) */

/* (specification)
   "REGRESSION" (regression_):
   *variables=custom;
   statistics[st_]=r,
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
   save=residuals;
   method=enter.
*/
/* (declarations) */
/* (functions) */
static struct cmd_regression cmd;

/* Linear regression models. */
pspp_linreg_cache **models = NULL;

/*
  Variables used (both explanatory and response).
 */
static struct variable **v_variables;

/*
  Number of variables.
 */
static size_t n_variables;

/*
  File where the model will be saved if the EXPORT subcommand
  is given. 
 */
struct file_handle *model_file;

/*
  Return value for the procedure.
 */
int pspp_reg_rc = CMD_SUCCESS;

static bool run_regression (const struct casefile *, void *);

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
  coeff = c->coeff[0].estimate;
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
      v = pspp_linreg_coeff_get_var (c->coeff + j, 0);
      label = var_to_string (v);
      /* Do not overwrite the variable's name. */
      strncpy (tmp, label, MAX_STRING);
      if (v->type == ALPHA)
	{
	  /*
	     Append the value associated with this coefficient.
	     This makes sense only if we us the usual binary encoding
	     for that value.
	   */

	  val = pspp_linreg_coeff_get_value (c->coeff + j, v);
	  val_s = value_to_string (val, v);
	  strncat (tmp, val_s, MAX_STRING);
	}

      tab_text (t, 1, j + 1, TAB_CENTER, tmp);
      /*
         Regression coefficients.
       */
      coeff = c->coeff[j].estimate;
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
      pval = 2 * gsl_cdf_tdist_Q (fabs (t_stat), 1.0);
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
      const struct variable *v = pspp_linreg_coeff_get_var (c->coeff + i, 0);
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
static int
regression_trns_proc (void *m, struct ccase *c, int case_idx UNUSED)
{
  size_t i;
  size_t n_vars;
  size_t n_vals = 0;
  pspp_linreg_cache *model = m;
  union value *output;
  const union value **vals = NULL;
  const union value *obs = NULL;
  struct variable **vars = NULL;
  
  assert (model != NULL);
  assert (model->depvar != NULL);
  assert (model->resid != NULL);
  
  dict_get_vars (default_dict, &vars, &n_vars, 1u << DC_SYSTEM);
  vals = xnmalloc (n_vars, sizeof (*vals));
  assert (vals != NULL);
  output = case_data_rw (c, model->resid->fv);
  assert (output != NULL);

  for (i = 0; i < n_vars; i++)
    {
      /* Do not use the residual variable. */
      if (vars[i]->index != model->resid->index) 
	{
	  /* Do not use the dependent variable as a predictor. */
	  if (vars[i]->index == model->depvar->index) 
	    {
	      obs = case_data (c, i);
	      assert (obs != NULL);
	    }
	  else
	    {
	      vals[i] = case_data (c, i);
	      n_vals++;
	    }
	}
    }
  output->f = (*model->residual) ((const struct variable **) vars, 
				  vals, obs, model, n_vals);
  free (vals);
  return TRNS_CONTINUE;
}
static void
subcommand_save (int save, pspp_linreg_cache **models)
{
  struct variable *residuals = NULL;
  pspp_linreg_cache **lc;

  assert (models != NULL);

  if (save)
    {
      for (lc = models; lc < models + cmd.n_dependent; lc++)
	{
	  assert (*lc != NULL);
	  assert ((*lc)->depvar != NULL);
	  residuals = dict_create_var (default_dict, "residuals", 0);
	  assert (residuals != NULL);
	  (*lc)->resid = residuals;
	  add_transformation (regression_trns_proc, pspp_linreg_cache_free, *lc);
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
      if (v->index == varlist[i]->index)
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
  size_t j;
  int n_vars = 0;
  struct variable **varlist;
  struct pspp_linreg_coeff *coeff;
  const struct variable *v;
  union value *val;

  fprintf (fp, "%s", reg_export_categorical_encode_1);

  varlist = xnmalloc (c->n_indeps, sizeof (*varlist));
  for (i = 1; i < c->n_indeps; i++)	/* c->coeff[0] is the intercept. */
    {
      coeff = c->coeff + i;
      v = pspp_linreg_coeff_get_var (coeff, 0);
      if (v->type == ALPHA)
	{
	  if (!reg_inserted (v, varlist, n_vars))
	    {
	      fprintf (fp, "struct pspp_reg_categorical_variable %s;\n\t",
		       v->name);
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
      fprintf (fp, "&%s,\n\t\t", varlist[i]->name);
    }
  fprintf (fp, "&%s};\n\t", varlist[i]->name);

  for (i = 0; i < n_vars; i++)
    {
      coeff = c->coeff + i;
      fprintf (fp, "%s.name = \"%s\";\n\t", varlist[i]->name,
	       varlist[i]->name);
      fprintf (fp, "%s.n_vals = %d;\n\t", varlist[i]->name,
	       varlist[i]->obs_vals->n_categories);

      for (j = 0; j < varlist[i]->obs_vals->n_categories; j++)
	{
	  val = cat_subscript_to_value ((const size_t) j, varlist[i]);
	  fprintf (fp, "%s.values[%d] = \"%s\";\n\t", varlist[i]->name, j,
		   value_to_string (val, varlist[i]));
	}
    }
  fprintf (fp, "%s", reg_export_categorical_encode_2);
}

static void
reg_print_depvars (FILE * fp, pspp_linreg_cache * c)
{
  int i;
  struct pspp_linreg_coeff *coeff;
  const struct variable *v;

  fprintf (fp, "char *model_depvars[%d] = {", c->n_indeps);
  for (i = 1; i < c->n_indeps; i++)
    {
      coeff = c->coeff + i;
      v = pspp_linreg_coeff_get_var (coeff, 0);
      fprintf (fp, "\"%s\",\n\t\t", v->name);
    }
  coeff = c->coeff + i;
  v = pspp_linreg_coeff_get_var (coeff, 0);
  fprintf (fp, "\"%s\"};\n\t", v->name);
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
      v = pspp_linreg_coeff_get_var (c->coeff + i, 0);
      if (v->type == ALPHA)
	{
	  return 1;
	}
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
  double increment;
  double tmp;
  struct pspp_linreg_coeff coeff;

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
      increment = 0.5 / (double) increment;
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
	  fprintf (fp, "%.15e,\n\t\t", coeff.estimate);
	}
      coeff = c->coeff[i];
      fprintf (fp, "%.15e};\n\t", coeff.estimate);
      coeff = c->coeff[0];
      fprintf (fp, "double estimate = %.15e;\n\t", coeff.estimate);
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
regression_custom_export (struct cmd_regression *cmd UNUSED)
{
  /* 0 on failure, 1 on success, 2 on failure that should result in syntax error */
  if (!lex_force_match ('('))
    return 0;

  if (lex_match ('*'))
    model_file = NULL;
  else
    {
      model_file = fh_parse (FH_REF_FILE);
      if (model_file == NULL)
	return 0;
    }

  if (!lex_force_match (')'))
    return 0;

  return 1;
}

int
cmd_regression (void)
{
  if (!parse_regression (&cmd))
    return CMD_FAILURE;

  models = xnmalloc (cmd.n_dependent, sizeof *models);
  if (!multipass_procedure_with_splits (run_regression, &cmd))
    return CMD_CASCADING_FAILURE;
  subcommand_save (cmd.sbc_save, models);
  free (v_variables);
  free (models);
  return pspp_reg_rc;
}

/*
  Is variable k the dependent variable?
 */
static int
is_depvar (size_t k, const struct variable *v)
{
  /*
    compare_var_names returns 0 if the variable
    names match.
  */
  if (!compare_var_names (v, v_variables[k], NULL))
    return 1;

  return 0;
}

/*
  Mark missing cases. Return the number of non-missing cases.
 */
static size_t
mark_missing_cases (const struct casefile *cf, struct variable *v,
		    int *is_missing_case, double n_data)
{
  struct casereader *r;
  struct ccase c;
  size_t row;
  const union value *val;

  for (r = casefile_get_reader (cf);
       casereader_read (r, &c); case_destroy (&c))
    {
      row = casereader_cnum (r) - 1;

      val = case_data (&c, v->fv);
      cat_value_update (v, val);
      if (mv_is_value_missing (&v->miss, val))
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
regression_custom_variables(struct cmd_regression *cmd UNUSED)
{

  lex_match('=');

  if ((token != T_ID || dict_lookup_var (default_dict, tokid) == NULL)
      && token != T_ALL)
    return 2;
  

  if (!parse_variables (default_dict, &v_variables, &n_variables,
			PV_NONE ))
    {
      free (v_variables);
      return 0;
    }
  assert(n_variables);

  return 1;
}
/*
  Count the explanatory variables. The user may or may
  not have specified a response variable in the syntax.
 */
static
int get_n_indep (const struct variable *v)
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
static 
int prepare_data (int n_data, int is_missing_case[], 
		  struct variable **indep_vars, 
		  struct variable *depvar,
		  const struct casefile *cf)
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
	  if (v_variables[i]->type == ALPHA)
	    {
	      /* Make a place to hold the binary vectors 
		 corresponding to this variable's values. */
	      cat_stored_values_create (v_variables[i]);
	    }
	  n_data = mark_missing_cases (cf, v_variables[i], is_missing_case, n_data);
	}
    }
  /*
    Mark missing cases for the dependent variable.
   */
  n_data = mark_missing_cases (cf, depvar, is_missing_case, n_data);

  return n_data;
}
static bool
run_regression (const struct casefile *cf, void *cmd_ UNUSED)
{
  size_t i;
  size_t n_data = 0; /* Number of valide cases. */
  size_t n_cases; /* Number of cases. */
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
  struct variable **indep_vars;
  struct design_matrix *X;
  gsl_vector *Y;

  pspp_linreg_opts lopts;

  assert (models != NULL);
  if (!v_variables)
    {
      dict_get_vars (default_dict, &v_variables, &n_variables,
		     1u << DC_SYSTEM);
    }

  n_cases = casefile_get_case_cnt (cf);

  for (i = 0; i < cmd.n_dependent; i++)
    {
      if (cmd.v_dependent[i]->type != NUMERIC)
	{
	  msg (SE, gettext ("Dependent variable must be numeric."));
	  pspp_reg_rc = CMD_FAILURE;
	  return true;
	}
    }

  is_missing_case = xnmalloc (n_cases, sizeof (*is_missing_case));

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
			     (const struct casefile *) cf);
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
      for (r = casefile_get_reader (cf); casereader_read (r, &c);
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
		  val = case_data (&c, v_variables[i]->fv);
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
		      if (v_variables[i]->type == ALPHA)
			{
			  design_matrix_set_categorical (X, row, v_variables[i], val);
			}
		      else if (v_variables[i]->type == NUMERIC)
			{
			  design_matrix_set_numeric (X, row, v_variables[i], val);
			}
		    }
		}
	      val = case_data (&c, cmd.v_dependent[k]->fv);
	      gsl_vector_set (Y, row, val->f);
	      row++;
	    }
	}
      /*
         Now that we know the number of coefficients, allocate space
         and store pointers to the variables that correspond to the
         coefficients.
       */
      pspp_linreg_coeff_init (models[k], X);

      /* 
         Find the least-squares estimates and other statistics.
       */
      pspp_linreg ((const gsl_vector *) Y, X->m, &lopts, models[k]);
      subcommand_statistics (cmd.a_statistics, models[k]);
      subcommand_export (cmd.sbc_export, models[k]);

      gsl_vector_free (Y);
      design_matrix_destroy (X);
      free (indep_vars);
      free (lopts.get_indep_mean_std);
      casereader_destroy (r);
    }

  free (is_missing_case);

  return true;
}

/*
  Local Variables:   
  mode: c
  End:
*/
