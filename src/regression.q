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
#include "alloc.h"
#include "case.h"
#include "dictionary.h"
#include "file-handle.h"
#include "command.h"
#include "lexer.h"
#include "tab.h"
#include "var.h"
#include "vfm.h"
#include "casefile.h"
#include <linreg/pspp_linreg.h>
#include "cat.h"
/* (headers) */


/* (specification)
   "REGRESSION" (regression_):
   *variables=varlist;
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
   ^dependent=varlist;
   ^method=enter.
*/
/* (declarations) */
/* (functions) */
static struct cmd_regression cmd;

/*
  Array holding the subscripts of the independent variables.
 */
size_t *indep_vars;

static void run_regression (const struct casefile *, void *);
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
  assert (c != NULL);
}

/*
  Table showing estimated regression coefficients.
 */
static void
reg_stats_coeff (pspp_linreg_cache * c)
{
  size_t i;
  size_t j;
  int n_cols = 7;
  int n_rows;
  double t_stat;
  double pval;
  double coeff;
  double std_err;
  double beta;
  const char *label;
  struct tab_table *t;

  assert (c != NULL);
  n_rows = 2 + c->param_estimates->size;
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
  coeff = gsl_vector_get (c->param_estimates, 0);
  tab_float (t, 2, 1, 0, coeff, 10, 2);
  std_err = sqrt (gsl_matrix_get (c->cov, 0, 0));
  tab_float (t, 3, 1, 0, std_err, 10, 2);
  beta = coeff / c->depvar_std;
  tab_float (t, 4, 1, 0, beta, 10, 2);
  t_stat = coeff / std_err;
  tab_float (t, 5, 1, 0, t_stat, 10, 2);
  pval = 2 * gsl_cdf_tdist_Q (fabs (t_stat), 1.0);
  tab_float (t, 6, 1, 0, pval, 10, 2);
  for (j = 0; j < c->n_indeps; j++)
    {
      i = indep_vars[j];
      struct variable *v = cmd.v_variables[i];
      label = var_to_string (v);
      tab_text (t, 1, j + 2, TAB_CENTER, label);
      /*
         Regression coefficients.
       */
      coeff = gsl_vector_get (c->param_estimates, j + 1);
      tab_float (t, 2, j + 2, 0, coeff, 10, 2);
      /*
         Standard error of the coefficients.
       */
      std_err = sqrt (gsl_matrix_get (c->cov, j + 1, j + 1));
      tab_float (t, 3, j + 2, 0, std_err, 10, 2);
      /*
         'Standardized' coefficient, i.e., regression coefficient
         if all variables had unit variance.
       */
      beta = gsl_vector_get (c->indep_std, j + 1);
      beta *= coeff / c->depvar_std;
      tab_float (t, 4, j + 2, 0, beta, 10, 2);

      /*
         Test statistic for H0: coefficient is 0.
       */
      t_stat = coeff / std_err;
      tab_float (t, 5, j + 2, 0, t_stat, 10, 2);
      /*
         P values for the test statistic above.
       */
      pval = 2 * gsl_cdf_tdist_Q (fabs (t_stat), 1.0);
      tab_float (t, 6, j + 2, 0, pval, 10, 2);
    }
  tab_title (t, 0, _("Coefficients"));
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
  tab_float (t, 3, 1, 0, c->dfm, 4, 0);
  tab_float (t, 3, 2, 0, c->dfe, 4, 0);
  tab_float (t, 3, 3, 0, c->dft, 4, 0);

  /* Mean Squares */

  tab_float (t, 4, 1, TAB_RIGHT, msm, 8, 3);
  tab_float (t, 4, 2, TAB_RIGHT, mse, 8, 3);

  tab_float (t, 5, 1, 0, F, 8, 3);

  tab_float (t, 6, 1, 0, pval, 8, 3);

  tab_title (t, 0, _("ANOVA"));
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
  assert (c != NULL);
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
	  *(keywords + i) = 1;
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
	  *(keywords + anova) = 1;
	  *(keywords + outs) = 1;
	  *(keywords + coeff) = 1;
	  *(keywords + r) = 1;
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

int
cmd_regression (void)
{
  if (!parse_regression (&cmd))
    {
      return CMD_FAILURE;
    }
  multipass_procedure_with_splits (run_regression, &cmd);

  return CMD_SUCCESS;
}

/*
  Is variable k one of the dependent variables?
 */
static int
is_depvar (size_t k)
{
  size_t j = 0;
  for (j = 0; j < cmd.n_dependent; j++)
    {
      /*
         compare_var_names returns 0 if the variable
         names match.
       */
      if (!compare_var_names (cmd.v_dependent[j], cmd.v_variables[k], NULL))
	return 1;
    }
  return 0;
}

static void
run_regression (const struct casefile *cf, void *cmd_)
{
  size_t i;
  size_t k;
  size_t n_data = 0;
  size_t row;
  int n_indep;
  const union value *val;
  struct casereader *r;
  struct casereader *r2;
  struct ccase c;
  const struct variable *v;
  struct recoded_categorical_array *ca;
  struct recoded_categorical *rc;
  struct design_matrix *X;
  gsl_vector *Y;
  pspp_linreg_cache *lcache;
  pspp_linreg_opts lopts;

  n_data = casefile_get_case_cnt (cf);
  n_indep = cmd.n_variables - cmd.n_dependent;
  indep_vars = (size_t *) malloc (n_indep * sizeof (*indep_vars));

  Y = gsl_vector_alloc (n_data);
  lopts.get_depvar_mean_std = 1;
  lopts.get_indep_mean_std = (int *) malloc (n_indep * sizeof (int));

  lcache = pspp_linreg_cache_alloc (n_data, n_indep);
  lcache->indep_means = gsl_vector_alloc (n_indep);
  lcache->indep_std = gsl_vector_alloc (n_indep);

  /*
     Read from the active file. The first pass encodes categorical
     variables.
   */
  ca = cr_recoded_cat_ar_create (cmd.n_variables, cmd.v_variables);
  for (r = casefile_get_reader (cf);
       casereader_read (r, &c); case_destroy (&c))
    {
      for (i = 0; i < ca->n_vars; i++)
	{
	  v = (*(ca->a + i))->v;
	  val = case_data (&c, v->fv);
	  cr_value_update (*(ca->a + i), val);
	}
    }
  cr_create_value_matrices (ca);
  X =
    design_matrix_create (n_indep, (const struct variable **) cmd.v_variables,
			  ca, n_data);

  /*
     The second pass creates the design matrix.
   */
  for (r2 = casefile_get_reader (cf); casereader_read (r2, &c);
       case_destroy (&c))
    /* Iterate over the cases. */
    {
      k = 0;
      row = casereader_cnum (r2) - 1;
      for (i = 0; i < cmd.n_variables; ++i)	/* Iterate over the variables
						   for the current case. 
						 */
	{
	  v = cmd.v_variables[i];
	  val = case_data (&c, v->fv);
	  /*
	     Independent/dependent variable separation. The
	     'variables' subcommand specifies a varlist which contains
	     both dependent and independent variables. The dependent
	     variables are specified with the 'dependent'
	     subcommand. We need to separate the two.
	   */
	  if (is_depvar (i))
	    {
	      if (v->type == NUMERIC)
		{
		  gsl_vector_set (Y, row, val->f);
		}
	      else
		{
		  errno = EINVAL;
		  fprintf (stderr,
			   "%s:%d: Dependent variable should be numeric: %s\n",
			   __FILE__, __LINE__, strerror (errno));
		  err_cond_fail ();
		}
	    }
	  else
	    {
	      if (v->type == ALPHA)
		{
		  rc = cr_var_to_recoded_categorical (v, ca);
		  design_matrix_set_categorical (X, row, v, val, rc);
		}
	      else if (v->type == NUMERIC)
		{
		  design_matrix_set_numeric (X, row, v, val);
		}

	      indep_vars[k] = i;
	      k++;
	      lopts.get_indep_mean_std[i] = 1;
	    }
	}
    }
  /* 
     Find the least-squares estimates and other statistics.
   */
  pspp_linreg ((const gsl_vector *) Y, X->m, &lopts, lcache);
  subcommand_statistics (cmd.a_statistics, lcache);
  gsl_vector_free (Y);
  design_matrix_destroy (X);
  pspp_linreg_cache_free (lcache);
  free (lopts.get_indep_mean_std);
  free (indep_vars);
  casereader_destroy (r);
}

/*
  Local Variables:   
  mode: c
  End:
*/
