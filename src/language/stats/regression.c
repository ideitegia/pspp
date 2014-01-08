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
#include <data/casewriter.h>

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
};

struct regression_workspace
{
  /* The new variables which will be introduced by /SAVE */
  const struct variable **predvars; 
  const struct variable **residvars;

  /* A reader/writer pair to temporarily hold the 
     values of the new variables */
  struct casewriter *writer;
  struct casereader *reader;

  /* Indeces of the new values in the reader/writer (-1 if not applicable) */
  int res_idx;
  int pred_idx;

  /* 0, 1 or 2 depending on what new variables are to be created */
  int extras;
};

static void run_regression (const struct regression *cmd,
                            struct regression_workspace *ws,
                            struct casereader *input);


/* Return a string based on PREFIX which may be used as the name
   of a new variable in DICT */
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


static const struct variable *
create_aux_var (struct dataset *ds, const char *prefix)
{
  struct variable *var;
  struct dictionary *dict = dataset_dict (ds);
  char *name = reg_get_name (dict, prefix);
  var = dict_create_var_assert (dict, name, 0);
  free (name);
  return var;
}

/* Auxilliary data for transformation when /SAVE is entered */
struct save_trans_data
{
  int n_dep_vars;
  struct regression_workspace *ws;
};

static bool
save_trans_free (void *aux)
{
  struct save_trans_data *save_trans_data = aux;
  free (save_trans_data->ws->predvars);
  free (save_trans_data->ws->residvars);

  casereader_destroy (save_trans_data->ws->reader);
  free (save_trans_data->ws);
  free (save_trans_data);
  return true;
}

static int 
save_trans_func (void *aux, struct ccase **c, casenumber x UNUSED)
{
  struct save_trans_data *save_trans_data = aux;
  struct regression_workspace *ws = save_trans_data->ws;
  struct ccase *in =  casereader_read (ws->reader);

  if (in)
    {
      int k;
      *c = case_unshare (*c);

      for (k = 0; k < save_trans_data->n_dep_vars; ++k)
        {
          if (ws->pred_idx != -1)
            {
              double pred = case_data_idx (in, ws->extras * k + ws->pred_idx)->f;
              case_data_rw (*c, ws->predvars[k])->f = pred;
            }
          
          if (ws->res_idx != -1)
            {
              double resid = case_data_idx (in, ws->extras * k + ws->res_idx)->f;
              case_data_rw (*c, ws->residvars[k])->f = resid;
            }
        }
      case_unref (in);
    }

  return TRNS_CONTINUE;
}


int
cmd_regression (struct lexer *lexer, struct dataset *ds)
{
  struct regression_workspace workspace;
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

	  free (regression.dep_vars);
	  regression.n_dep_vars = 0;
	  
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

  save = regression.pred || regression.resid;
  workspace.extras = 0;
  workspace.res_idx = -1;
  workspace.pred_idx = -1;
  workspace.writer = NULL;                      
  workspace.reader = NULL;
  if (save)
    {
      int i;
      struct caseproto *proto = caseproto_create ();

      if (regression.resid)
        {
          workspace.extras ++;
          workspace.res_idx = 0;
          workspace.residvars = xcalloc (regression.n_dep_vars, sizeof (*workspace.residvars));

          for (i = 0; i < regression.n_dep_vars; ++i)
            {
              workspace.residvars[i] = create_aux_var (ds, "RES");
              proto = caseproto_add_width (proto, 0);
            }
        }

      if (regression.pred)
        {
          workspace.extras ++;
          workspace.pred_idx = 1;
          workspace.predvars = xcalloc (regression.n_dep_vars, sizeof (*workspace.predvars));

          for (i = 0; i < regression.n_dep_vars; ++i)
            {
              workspace.predvars[i] = create_aux_var (ds, "PRED");
              proto = caseproto_add_width (proto, 0);
            }
        }

      if (proc_make_temporary_transformations_permanent (ds))
        msg (SW, _("REGRESSION with SAVE ignores TEMPORARY.  "
                   "Temporary transformations will be made permanent."));

      workspace.writer = autopaging_writer_create (proto);
      caseproto_unref (proto);
    }


  {
    struct casegrouper *grouper;
    struct casereader *group;
    bool ok;

    grouper = casegrouper_create_splits (proc_open_filtering (ds, !save), dict);


    while (casegrouper_get_next_group (grouper, &group))
      {
	run_regression (&regression,
                        &workspace,
                        group);

      }
    ok = casegrouper_destroy (grouper);
    ok = proc_commit (ds) && ok;
  }

  if (workspace.writer)
    {
      struct save_trans_data *save_trans_data = xmalloc (sizeof *save_trans_data);
      struct casereader *r = casewriter_make_reader (workspace.writer);
      workspace.writer = NULL;
      workspace.reader = r;
      save_trans_data->ws = xmalloc (sizeof (workspace));
      memcpy (save_trans_data->ws, &workspace, sizeof (workspace));
      save_trans_data->n_dep_vars = regression.n_dep_vars;
          
      add_transformation (ds, save_trans_func, save_trans_free, save_trans_data);
    }


  free (regression.vars);
  free (regression.dep_vars);
  return CMD_SUCCESS;

error:

  free (regression.vars);
  free (regression.dep_vars);
  return CMD_FAILURE;
}

/* Return the size of the union of dependent and independent variables */
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

/* Fill VARS with the union of dependent and independent variables */
static void
fill_all_vars (const struct variable **vars, const struct regression *cmd)
{
  size_t x = 0;
  size_t i;
  for (i = 0; i < cmd->n_vars; i++)
    {
      vars[i] = cmd->vars[i];
    }

  for (i = 0; i < cmd->n_dep_vars; i++)
    {
      size_t j;
      bool absent = true;
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
          vars[cmd->n_vars + x++] = cmd->dep_vars[i];
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
           ("The dependent variable is equal to the independent variable. "
            "The least squares line is therefore Y=X. "
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

  const gsl_matrix *cm = covariance_calculate_unnormalized (all_cov);

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
  return result;
}



/*
  STATISTICS subcommand output functions.
*/
static void reg_stats_r (const linreg *,     const struct variable *);
static void reg_stats_coeff (const linreg *, const gsl_matrix *, const struct variable *);
static void reg_stats_anova (const linreg *, const struct variable *);
static void reg_stats_bcov (const linreg *,  const struct variable *);


static void
subcommand_statistics (const struct regression *cmd, const linreg * c, const gsl_matrix * cm,
                       const struct variable *var)
{
  if (cmd->r) 
    reg_stats_r     (c, var);

  if (cmd->anova) 
    reg_stats_anova (c, var);

  if (cmd->coeff)
    reg_stats_coeff (c, cm, var);

  if (cmd->bcov)
    reg_stats_bcov  (c, var);
}


static void
run_regression (const struct regression *cmd, 
                struct regression_workspace *ws,
                struct casereader *input)
{
  size_t i;
  linreg **models;

  int k;
  struct ccase *c;
  struct covariance *cov;
  struct casereader *reader;
  size_t n_all_vars = get_n_all_vars (cmd);
  const struct variable **all_vars = xnmalloc (n_all_vars, sizeof (*all_vars));

  double *means = xnmalloc (n_all_vars, sizeof (*means));

  fill_all_vars (all_vars, cmd);
  cov = covariance_1pass_create (n_all_vars, all_vars,
                                 dict_get_weight (dataset_dict (cmd->ds)),
                                 MV_ANY);

  reader = casereader_clone (input);
  reader = casereader_create_filter_missing (reader, all_vars, n_all_vars,
                                             MV_ANY, NULL, NULL);


  {
    struct casereader *r = casereader_clone (reader);

    for (; (c = casereader_read (r)) != NULL; case_unref (c))
      {
        covariance_accumulate (cov, c);
      }
    casereader_destroy (r);
  }

  models = xcalloc (cmd->n_dep_vars, sizeof (*models));
  for (k = 0; k < cmd->n_dep_vars; k++)
    {
      const struct variable **vars = xnmalloc (cmd->n_vars, sizeof (*vars));
      const struct variable *dep_var = cmd->dep_vars[k];
      int n_indep = identify_indep_vars (cmd, vars, dep_var);
      gsl_matrix *this_cm = gsl_matrix_alloc (n_indep + 1, n_indep + 1);
      double n_data = fill_covariance (this_cm, cov, vars, n_indep,
                                dep_var, all_vars, n_all_vars, means);
      models[k] = linreg_alloc (dep_var, vars,  n_data, n_indep);
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
        }
      gsl_matrix_free (this_cm);
      free (vars);
    }


  if (ws->extras > 0)
   {
      struct casereader *r = casereader_clone (reader);
      
      for (; (c = casereader_read (r)) != NULL; case_unref (c))
        {
          struct ccase *outc = case_clone (c);
          for (k = 0; k < cmd->n_dep_vars; k++)
            {
              const struct variable **vars = xnmalloc (cmd->n_vars, sizeof (*vars));
              const struct variable *dep_var = cmd->dep_vars[k];
              int n_indep = identify_indep_vars (cmd, vars, dep_var);
              double *vals = xnmalloc (n_indep, sizeof (*vals));
              for (i = 0; i < n_indep; i++)
                {
                  const union value *tmp = case_data (c, vars[i]);
                  vals[i] = tmp->f;
                }

              if (cmd->pred)
                {
                  double pred = linreg_predict (models[k], vals, n_indep);
                  case_data_rw_idx (outc, k * ws->extras + ws->pred_idx)->f = pred;
                }

              if (cmd->resid)
                {
                  double obs = case_data (c, models[k]->depvar)->f;
                  double res = linreg_residual (models[k], obs,  vals, n_indep);
                  case_data_rw_idx (outc, k * ws->extras + ws->res_idx)->f = res;
                }
	      free (vals);
	      free (vars);
            }          
          casewriter_write (ws->writer, outc);
        }
      casereader_destroy (r);
    }

  casereader_destroy (reader);

  for (k = 0; k < cmd->n_dep_vars; k++)
    {
      linreg_unref (models[k]);
    }
  free (models);

  free (all_vars);
  free (means);
  casereader_destroy (input);
  covariance_destroy (cov);
}




static void
reg_stats_r (const linreg * c, const struct variable *var)
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
reg_stats_coeff (const linreg * c, const gsl_matrix *cov, const struct variable *var)
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
  tab_text (t, 6, 0, TAB_CENTER | TAT_TITLE, _("Sig."));
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
reg_stats_anova (const linreg * c, const struct variable *var)
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
  tab_text (t, 6, 0, TAB_CENTER | TAT_TITLE, _("Sig."));

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
reg_stats_bcov (const linreg * c, const struct variable *var)
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

