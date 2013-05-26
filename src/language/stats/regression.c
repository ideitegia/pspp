/* PSPP - a program for statistical analysis.
   Copyright (C) 2005, 2009, 2010, 2011, 2012, 2013 Free Software Foundation, Inc.

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

#include <stdbool.h>

#include <gsl/gsl_cdf.h>
#include <gsl/gsl_matrix.h>

#include <data/dataset.h>

#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/value-parser.h"
#include "language/lexer/variable-parser.h"


#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dictionary.h"

#include "math/covariance.h"
#include "math/linreg.h"
#include "math/moments.h"

#include "libpspp/message.h"
#include "libpspp/taint.h"

#include "output/tab.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


#include <gl/intprops.h>

#define REG_LARGE_DATA 1000

struct regression
{
  struct dataset *ds;

  const struct variable **vars;
  size_t n_vars;

  const struct variable **dep_vars;
  size_t n_dep_vars;

  bool r;
  bool coeff;
  bool anova;
  bool bcov;


  bool resid;
  bool pred;

  linreg **models;
};


static void run_regression (const struct regression *cmd,
                            struct casereader *input);



/*
  Transformations for saving predicted values
  and residuals, etc.
*/
struct reg_trns
{
  int n_trns;                   /* Number of transformations. */
  int trns_id;                  /* Which trns is this one? */
  linreg *c;                    /* Linear model for this trns. */
};

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


static char *
reg_get_name (const struct dictionary *dict, const char *prefix)
{
  char *name;
  int i;

  /* XXX handle too-long prefixes */
  name = xmalloc (strlen (prefix) + INT_BUFSIZE_BOUND (i) + 1);
  for (i = 1;; i++)
    {
      sprintf (name, "%s%d", prefix, i);
      if (dict_lookup_var (dict, name) == NULL)
        return name;
    }
}

/*
  Free the transformation. Free its linear model if this
  transformation is the last one.
*/
static bool
regression_trns_free (void *t_)
{
  struct reg_trns *t = t_;

  if (t->trns_id == t->n_trns)
    {
      linreg_unref (t->c);
    }
  free (t);

  return true;
}

static void
reg_save_var (struct dataset *ds, const char *prefix, trns_proc_func * f,
              linreg * c, struct variable **v, int n_trns)
{
  struct dictionary *dict = dataset_dict (ds);
  static int trns_index = 1;
  char *name;
  struct variable *new_var;
  struct reg_trns *t = NULL;

  t = xmalloc (sizeof (*t));
  t->trns_id = trns_index;
  t->n_trns = n_trns;
  t->c = c;

  name = reg_get_name (dict, prefix);
  new_var = dict_create_var_assert (dict, name, 0);
  free (name);

  *v = new_var;
  add_transformation (ds, f, regression_trns_free, t);
  trns_index++;
}

static void
subcommand_save (const struct regression *cmd)
{
  linreg **lc;
  int n_trns = 0;

  if (cmd->resid)
    n_trns++;
  if (cmd->pred)
    n_trns++;

  n_trns *= cmd->n_dep_vars;

  for (lc = cmd->models; lc < cmd->models + cmd->n_dep_vars; lc++)
    {
      if (*lc != NULL)
        {
          if ((*lc)->depvar != NULL)
            {
              (*lc)->refcnt++;
              if (cmd->resid)
                {
                  reg_save_var (cmd->ds, "RES", regression_trns_resid_proc,
                                *lc, &(*lc)->resid, n_trns);
                }
              if (cmd->pred)
                {
                  reg_save_var (cmd->ds, "PRED", regression_trns_pred_proc,
                                *lc, &(*lc)->pred, n_trns);
                }
            }
        }
    }
}

int
cmd_regression (struct lexer *lexer, struct dataset *ds)
{
  int k;
  struct regression regression;
  const struct dictionary *dict = dataset_dict (ds);
  bool save;

  memset (&regression, 0, sizeof (struct regression));

  regression.anova = true;
  regression.coeff = true;
  regression.r = true;

  regression.pred = false;
  regression.resid = false;

  regression.ds = ds;

  /* Accept an optional, completely pointless "/VARIABLES=" */
  lex_match (lexer, T_SLASH);
  if (lex_match_id (lexer, "VARIABLES"))
    {
      if (!lex_force_match (lexer, T_EQUALS))
        goto error;
    }

  if (!parse_variables_const (lexer, dict,
                              &regression.vars, &regression.n_vars,
                              PV_NO_DUPLICATE | PV_NUMERIC))
    goto error;


  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "DEPENDENT"))
        {
          if (!lex_force_match (lexer, T_EQUALS))
            goto error;

          if (!parse_variables_const (lexer, dict,
                                      &regression.dep_vars,
                                      &regression.n_dep_vars,
                                      PV_NO_DUPLICATE | PV_NUMERIC))
            goto error;
        }
      else if (lex_match_id (lexer, "METHOD"))
        {
          lex_match (lexer, T_EQUALS);

          if (!lex_force_match_id (lexer, "ENTER"))
            {
              goto error;
            }
        }
      else if (lex_match_id (lexer, "STATISTICS"))
        {
          lex_match (lexer, T_EQUALS);

          while (lex_token (lexer) != T_ENDCMD
                 && lex_token (lexer) != T_SLASH)
            {
              if (lex_match (lexer, T_ALL))
                {
                }
              else if (lex_match_id (lexer, "DEFAULTS"))
                {
                }
              else if (lex_match_id (lexer, "R"))
                {
                }
              else if (lex_match_id (lexer, "COEFF"))
                {
                }
              else if (lex_match_id (lexer, "ANOVA"))
                {
                }
              else if (lex_match_id (lexer, "BCOV"))
                {
                }
              else
                {
                  lex_error (lexer, NULL);
                  goto error;
                }
            }
        }
      else if (lex_match_id (lexer, "SAVE"))
        {
          lex_match (lexer, T_EQUALS);

          while (lex_token (lexer) != T_ENDCMD
                 && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "PRED"))
                {
                  regression.pred = true;
                }
              else if (lex_match_id (lexer, "RESID"))
                {
                  regression.resid = true;
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

  if (!regression.vars)
    {
      dict_get_vars (dict, &regression.vars, &regression.n_vars, 0);
    }


  regression.models =
    xcalloc (regression.n_dep_vars, sizeof *regression.models);

  save = regression.pred || regression.resid;
  if (save)
    {
      if (proc_make_temporary_transformations_permanent (ds))
        msg (SW, _("REGRESSION with SAVE ignores TEMPORARY.  "
                   "Temporary transformations will be made permanent."));
    }

  {
    struct casegrouper *grouper;
    struct casereader *group;
    bool ok;

    grouper = casegrouper_create_splits (proc_open_filtering (ds, !save),
                                         dict);
    while (casegrouper_get_next_group (grouper, &group))
      run_regression (&regression, group);
    ok = casegrouper_destroy (grouper);
    ok = proc_commit (ds) && ok;
  }

  if (save)
    {
      subcommand_save (&regression);
    }


  for (k = 0; k < regression.n_dep_vars; k++)
    linreg_unref (regression.models[k]);
  free (regression.models);
  free (regression.vars);
  free (regression.dep_vars);
  return CMD_SUCCESS;

error:
  if (regression.models)
    {
      for (k = 0; k < regression.n_dep_vars; k++)
        linreg_unref (regression.models[k]);
      free (regression.models);
    }
  free (regression.vars);
  free (regression.dep_vars);
  return CMD_FAILURE;
}


static size_t
get_n_all_vars (const struct regression *cmd)
{
  size_t result = cmd->n_vars;
  size_t i;
  size_t j;

  result += cmd->n_dep_vars;
  for (i = 0; i < cmd->n_dep_vars; i++)
    {
      for (j = 0; j < cmd->n_vars; j++)
        {
          if (cmd->vars[j] == cmd->dep_vars[i])
            {
              result--;
            }
        }
    }
  return result;
}

static void
fill_all_vars (const struct variable **vars, const struct regression *cmd)
{
  size_t i;
  size_t j;
  bool absent;

  for (i = 0; i < cmd->n_vars; i++)
    {
      vars[i] = cmd->vars[i];
    }
  for (i = 0; i < cmd->n_dep_vars; i++)
    {
      absent = true;
      for (j = 0; j < cmd->n_vars; j++)
        {
          if (cmd->dep_vars[i] == cmd->vars[j])
            {
              absent = false;
              break;
            }
        }
      if (absent)
        {
          vars[i + cmd->n_vars] = cmd->dep_vars[i];
        }
    }
}

/*
  Is variable k the dependent variable?
*/
static bool
is_depvar (const struct regression *cmd, size_t k, const struct variable *v)
{
  return v == cmd->vars[k];
}


/* Identify the explanatory variables in v_variables.  Returns
   the number of independent variables. */
static int
identify_indep_vars (const struct regression *cmd,
                     const struct variable **indep_vars,
                     const struct variable *depvar)
{
  int n_indep_vars = 0;
  int i;

  for (i = 0; i < cmd->n_vars; i++)
    if (!is_depvar (cmd, i, depvar))
      indep_vars[n_indep_vars++] = cmd->vars[i];
  if ((n_indep_vars < 1) && is_depvar (cmd, 0, depvar))
    {
      /*
         There is only one independent variable, and it is the same
         as the dependent variable. Print a warning and continue.
       */
      msg (SW,
           gettext
           ("The dependent variable is equal to the independent variable."
            "The least squares line is therefore Y=X."
            "Standard errors and related statistics may be meaningless."));
      n_indep_vars = 1;
      indep_vars[0] = cmd->vars[0];
    }
  return n_indep_vars;
}


static double
fill_covariance (gsl_matrix * cov, struct covariance *all_cov,
                 const struct variable **vars,
                 size_t n_vars, const struct variable *dep_var,
                 const struct variable **all_vars, size_t n_all_vars,
                 double *means)
{
  size_t i;
  size_t j;
  size_t dep_subscript;
  size_t *rows;
  const gsl_matrix *ssizes;
  const gsl_matrix *mean_matrix;
  const gsl_matrix *ssize_matrix;
  double result = 0.0;

  gsl_matrix *cm = covariance_calculate_unnormalized (all_cov);

  if (cm == NULL)
    return 0;

  rows = xnmalloc (cov->size1 - 1, sizeof (*rows));

  for (i = 0; i < n_all_vars; i++)
    {
      for (j = 0; j < n_vars; j++)
        {
          if (vars[j] == all_vars[i])
            {
              rows[j] = i;
            }
        }
      if (all_vars[i] == dep_var)
        {
          dep_subscript = i;
        }
    }
  mean_matrix = covariance_moments (all_cov, MOMENT_MEAN);
  ssize_matrix = covariance_moments (all_cov, MOMENT_NONE);
  for (i = 0; i < cov->size1 - 1; i++)
    {
      means[i] = gsl_matrix_get (mean_matrix, rows[i], 0)
        / gsl_matrix_get (ssize_matrix, rows[i], 0);
      for (j = 0; j < cov->size2 - 1; j++)
        {
          gsl_matrix_set (cov, i, j, gsl_matrix_get (cm, rows[i], rows[j]));
          gsl_matrix_set (cov, j, i, gsl_matrix_get (cm, rows[j], rows[i]));
        }
    }
  means[cov->size1 - 1] = gsl_matrix_get (mean_matrix, dep_subscript, 0)
    / gsl_matrix_get (ssize_matrix, dep_subscript, 0);
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
  gsl_matrix_set (cov, cov->size1 - 1, cov->size1 - 1,
                  gsl_matrix_get (cm, dep_subscript, dep_subscript));
  free (rows);
  gsl_matrix_free (cm);
  return result;
}


/*
  STATISTICS subcommand output functions.
*/
static void reg_stats_r (linreg *, void *, const struct variable *);
static void reg_stats_coeff (linreg *, void *, const struct variable *);
static void reg_stats_anova (linreg *, void *, const struct variable *);
static void reg_stats_bcov (linreg *, void *, const struct variable *);

static void
statistics_keyword_output (void (*)
                           (linreg *, void *, const struct variable *), bool,
                           linreg *, void *, const struct variable *);



static void
subcommand_statistics (const struct regression *cmd, linreg * c, void *aux,
                       const struct variable *var)
{
  statistics_keyword_output (reg_stats_r, cmd->r, c, aux, var);
  statistics_keyword_output (reg_stats_anova, cmd->anova, c, aux, var);
  statistics_keyword_output (reg_stats_coeff, cmd->coeff, c, aux, var);
  statistics_keyword_output (reg_stats_bcov, cmd->bcov, c, aux, var);
}


static void
run_regression (const struct regression *cmd, struct casereader *input)
{
  size_t i;
  int n_indep = 0;
  int k;
  double *means;
  struct ccase *c;
  struct covariance *cov;
  const struct variable **vars;
  const struct variable **all_vars;
  struct casereader *reader;
  size_t n_all_vars;

  linreg **models = cmd->models;

  n_all_vars = get_n_all_vars (cmd);
  all_vars = xnmalloc (n_all_vars, sizeof (*all_vars));
  fill_all_vars (all_vars, cmd);
  vars = xnmalloc (cmd->n_vars, sizeof (*vars));
  means = xnmalloc (n_all_vars, sizeof (*means));
  cov = covariance_1pass_create (n_all_vars, all_vars,
                                 dict_get_weight (dataset_dict (cmd->ds)),
                                 MV_ANY);

  reader = casereader_clone (input);
  reader = casereader_create_filter_missing (reader, all_vars, n_all_vars,
                                             MV_ANY, NULL, NULL);


  for (; (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      covariance_accumulate (cov, c);
    }

  for (k = 0; k < cmd->n_dep_vars; k++)
    {
      double n_data;
      const struct variable *dep_var = cmd->dep_vars[k];
      gsl_matrix *this_cm;

      n_indep = identify_indep_vars (cmd, vars, dep_var);

      this_cm = gsl_matrix_alloc (n_indep + 1, n_indep + 1);
      n_data = fill_covariance (this_cm, cov, vars, n_indep,
                                dep_var, all_vars, n_all_vars, means);
      models[k] = linreg_alloc (dep_var, (const struct variable **) vars,
                                n_data, n_indep);
      models[k]->depvar = dep_var;
      for (i = 0; i < n_indep; i++)
        {
          linreg_set_indep_variable_mean (models[k], i, means[i]);
        }
      linreg_set_depvar_mean (models[k], means[i]);
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
              subcommand_statistics (cmd, models[k], this_cm, dep_var);
            }
        }
      else
        {
          msg (SE, _("No valid data found. This command was skipped."));
          linreg_unref (models[k]);
          models[k] = NULL;
        }
      gsl_matrix_free (this_cm);
    }

  casereader_destroy (reader);
  free (vars);
  free (all_vars);
  free (means);
  casereader_destroy (input);
  covariance_destroy (cov);
}





static void
reg_stats_r (linreg * c, void *aux UNUSED, const struct variable *var)
{
  struct tab_table *t;
  int n_rows = 2;
  int n_cols = 5;
  double rsq;
  double adjrsq;
  double std_error;

  assert (c != NULL);
  rsq = linreg_ssreg (c) / linreg_sst (c);
  adjrsq = rsq -
    (1.0 - rsq) * linreg_n_coeffs (c) / (linreg_n_obs (c) -
                                         linreg_n_coeffs (c) - 1);
  std_error = sqrt (linreg_mse (c));
  t = tab_create (n_cols, n_rows);
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
  tab_title (t, _("Model Summary (%s)"), var_to_string (var));
  tab_submit (t);
}

/*
  Table showing estimated regression coefficients.
*/
static void
reg_stats_coeff (linreg * c, void *aux_, const struct variable *var)
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
  gsl_matrix *cov = aux_;

  assert (c != NULL);
  n_rows = linreg_n_coeffs (c) + 3;

  t = tab_create (n_cols, n_rows);
  tab_headers (t, 2, 0, 1, 0);
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
  pval =
    2 * gsl_cdf_tdist_Q (fabs (t_stat),
                         (double) (linreg_n_obs (c) - linreg_n_coeffs (c)));
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
      beta = sqrt (gsl_matrix_get (cov, j, j));
      beta *= linreg_coeff (c, j) /
        sqrt (gsl_matrix_get (cov, cov->size1 - 1, cov->size2 - 1));
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
                             (double) (linreg_n_obs (c) -
                                       linreg_n_coeffs (c) - 1));
      tab_double (t, 6, this_row, 0, pval, NULL);
      ds_destroy (&tstr);
    }
  tab_title (t, _("Coefficients (%s)"), var_to_string (var));
  tab_submit (t);
}

/*
  Display the ANOVA table.
*/
static void
reg_stats_anova (linreg * c, void *aux UNUSED, const struct variable *var)
{
  int n_cols = 7;
  int n_rows = 4;
  const double msm = linreg_ssreg (c) / linreg_dfmodel (c);
  const double mse = linreg_mse (c);
  const double F = msm / mse;
  const double pval = gsl_cdf_fdist_Q (F, c->dfm, c->dfe);

  struct tab_table *t;

  assert (c != NULL);
  t = tab_create (n_cols, n_rows);
  tab_headers (t, 2, 0, 1, 0);

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
  tab_double (t, 2, 1, 0, linreg_ssreg (c), NULL);
  tab_double (t, 2, 3, 0, linreg_sst (c), NULL);
  tab_double (t, 2, 2, 0, linreg_sse (c), NULL);


  /* Degrees of freedom */
  tab_text_format (t, 3, 1, TAB_RIGHT, "%g", c->dfm);
  tab_text_format (t, 3, 2, TAB_RIGHT, "%g", c->dfe);
  tab_text_format (t, 3, 3, TAB_RIGHT, "%g", c->dft);

  /* Mean Squares */
  tab_double (t, 4, 1, TAB_RIGHT, msm, NULL);
  tab_double (t, 4, 2, TAB_RIGHT, mse, NULL);

  tab_double (t, 5, 1, 0, F, NULL);

  tab_double (t, 6, 1, 0, pval, NULL);

  tab_title (t, _("ANOVA (%s)"), var_to_string (var));
  tab_submit (t);
}


static void
reg_stats_bcov (linreg * c, void *aux UNUSED, const struct variable *var)
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
  t = tab_create (n_cols, n_rows);
  tab_headers (t, 2, 0, 1, 0);
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
  tab_title (t, _("Coefficient Correlations (%s)"), var_to_string (var));
  tab_submit (t);
}

static void
statistics_keyword_output (void (*function)
                           (linreg *, void *, const struct variable * var),
                           bool keyword, linreg * c, void *aux,
                           const struct variable *var)
{
  if (keyword)
    {
      (*function) (c, aux, var);
    }
}
