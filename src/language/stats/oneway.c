/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009, 2010, 2011, 2012, 2013 Free Software Foundation, Inc.

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
#include <math.h>

#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/value.h"
#include "language/command.h"
#include "language/dictionary/split-file.h"
#include "language/lexer/lexer.h"
#include "language/lexer/value-parser.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/ll.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/taint.h"
#include "linreg/sweep.h"
#include "tukey/tukey.h"
#include "math/categoricals.h"
#include "math/interaction.h"
#include "math/covariance.h"
#include "math/levene.h"
#include "math/moments.h"
#include "output/tab.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* Workspace variable for each dependent variable */
struct per_var_ws
{
  struct interaction *iact;
  struct categoricals *cat;
  struct covariance *cov;
  struct levene *nl;

  double n;

  double sst;
  double sse;
  double ssa;

  int n_groups;

  double mse;
};

/* Per category data */
struct descriptive_data
{
  const struct variable *var;
  struct moments1 *mom;

  double minimum;
  double maximum;
};

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
};


struct oneway_spec;

typedef double df_func (const struct per_var_ws *pvw, const struct moments1 *mom_i, const struct moments1 *mom_j);
typedef double ts_func (int k, const struct moments1 *mom_i, const struct moments1 *mom_j, double std_err);
typedef double p1tail_func (double ts, double df1, double df2);

typedef double pinv_func (double std_err, double alpha, double df, int k, const struct moments1 *mom_i, const struct moments1 *mom_j);


struct posthoc
{
  const char *syntax;
  const char *label;

  df_func *dff;
  ts_func *tsf;
  p1tail_func *p1f;

  pinv_func *pinv;
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

  /* The confidence level for multiple comparisons */
  double alpha;

  int *posthoc;
  int n_posthoc;
};

static double
df_common (const struct per_var_ws *pvw, const struct moments1 *mom_i UNUSED, const struct moments1 *mom_j UNUSED)
{
  return  pvw->n - pvw->n_groups;
}

static double
df_individual (const struct per_var_ws *pvw UNUSED, const struct moments1 *mom_i, const struct moments1 *mom_j)
{
  double n_i, var_i;
  double n_j, var_j;
  double nom,denom;

  moments1_calculate (mom_i, &n_i, NULL, &var_i, 0, 0);  
  moments1_calculate (mom_j, &n_j, NULL, &var_j, 0, 0);
  
  if ( n_i <= 1.0 || n_j <= 1.0)
    return SYSMIS;

  nom = pow2 (var_i/n_i + var_j/n_j);
  denom = pow2 (var_i/n_i) / (n_i - 1) + pow2 (var_j/n_j) / (n_j - 1);

  return nom / denom;
}

static double lsd_pinv (double std_err, double alpha, double df, int k UNUSED, const struct moments1 *mom_i UNUSED, const struct moments1 *mom_j UNUSED)
{
  return std_err * gsl_cdf_tdist_Pinv (1.0 - alpha / 2.0, df);
}

static double bonferroni_pinv (double std_err, double alpha, double df, int k, const struct moments1 *mom_i UNUSED, const struct moments1 *mom_j UNUSED)
{
  const int m = k * (k - 1) / 2;
  return std_err * gsl_cdf_tdist_Pinv (1.0 - alpha / (2.0 * m), df);
}

static double sidak_pinv (double std_err, double alpha, double df, int k, const struct moments1 *mom_i UNUSED, const struct moments1 *mom_j UNUSED)
{
  const double m = k * (k - 1) / 2;
  double lp = 1.0 - exp (log (1.0 - alpha) / m ) ;
  return std_err * gsl_cdf_tdist_Pinv (1.0 - lp / 2.0, df);
}

static double tukey_pinv (double std_err, double alpha, double df, int k, const struct moments1 *mom_i UNUSED, const struct moments1 *mom_j UNUSED)
{
  if ( k < 2 || df < 2)
    return SYSMIS;

  return std_err / sqrt (2.0)  * qtukey (1 - alpha, 1.0, k, df, 1, 0);
}

static double scheffe_pinv (double std_err, double alpha, double df, int k, const struct moments1 *mom_i UNUSED, const struct moments1 *mom_j UNUSED)
{
  double x = (k - 1) * gsl_cdf_fdist_Pinv (1.0 - alpha, k - 1, df);
  return std_err * sqrt (x);
}

static double gh_pinv (double std_err UNUSED, double alpha, double df, int k, const struct moments1 *mom_i, const struct moments1 *mom_j)
{
  double n_i, mean_i, var_i;
  double n_j, mean_j, var_j;
  double m;

  moments1_calculate (mom_i, &n_i, &mean_i, &var_i, 0, 0);  
  moments1_calculate (mom_j, &n_j, &mean_j, &var_j, 0, 0);

  m = sqrt ((var_i/n_i + var_j/n_j) / 2.0);

  if ( k < 2 || df < 2)
    return SYSMIS;

  return m * qtukey (1 - alpha, 1.0, k, df, 1, 0);
}


static double 
multiple_comparison_sig (double std_err,
				       const struct per_var_ws *pvw,
				       const struct descriptive_data *dd_i, const struct descriptive_data *dd_j,
				       const struct posthoc *ph)
{
  int k = pvw->n_groups;
  double df = ph->dff (pvw, dd_i->mom, dd_j->mom);
  double ts = ph->tsf (k, dd_i->mom, dd_j->mom, std_err);
  if ( df == SYSMIS)
    return SYSMIS;
  return  ph->p1f (ts, k - 1, df);
}

static double 
mc_half_range (const struct oneway_spec *cmd, const struct per_var_ws *pvw, double std_err, const struct descriptive_data *dd_i, const struct descriptive_data *dd_j, const struct posthoc *ph)
{
  int k = pvw->n_groups;
  double df = ph->dff (pvw, dd_i->mom, dd_j->mom);
  if ( df == SYSMIS)
    return SYSMIS;

  return ph->pinv (std_err, cmd->alpha, df, k, dd_i->mom, dd_j->mom);
}

static double tukey_1tailsig (double ts, double df1, double df2)
{
  double twotailedsig;

  if (df2 < 2 || df1 < 1)
    return SYSMIS;

  twotailedsig = 1.0 - ptukey (ts, 1.0, df1 + 1, df2, 1, 0);

  return twotailedsig / 2.0;
}

static double lsd_1tailsig (double ts, double df1 UNUSED, double df2)
{
  return ts < 0 ? gsl_cdf_tdist_P (ts, df2) : gsl_cdf_tdist_Q (ts, df2);
}

static double sidak_1tailsig (double ts, double df1, double df2)
{
  double ex = (df1 + 1.0) * df1 / 2.0;
  double lsd_sig = 2 * lsd_1tailsig (ts, df1, df2);

  return 0.5 * (1.0 - pow (1.0 - lsd_sig, ex));
}

static double bonferroni_1tailsig (double ts, double df1, double df2)
{
  const int m = (df1 + 1) * df1 / 2;

  double p = ts < 0 ? gsl_cdf_tdist_P (ts, df2) : gsl_cdf_tdist_Q (ts, df2);
  p *= m;

  return p > 0.5 ? 0.5 : p;
}

static double scheffe_1tailsig (double ts, double df1, double df2)
{
  return 0.5 * gsl_cdf_fdist_Q (ts, df1, df2);
}


static double tukey_test_stat (int k UNUSED, const struct moments1 *mom_i, const struct moments1 *mom_j, double std_err)
{
  double ts;
  double n_i, mean_i, var_i;
  double n_j, mean_j, var_j;

  moments1_calculate (mom_i, &n_i, &mean_i, &var_i, 0, 0);  
  moments1_calculate (mom_j, &n_j, &mean_j, &var_j, 0, 0);

  ts =  (mean_i - mean_j) / std_err;
  ts = fabs (ts) * sqrt (2.0);

  return ts;
}

static double lsd_test_stat (int k UNUSED, const struct moments1 *mom_i, const struct moments1 *mom_j, double std_err)
{
  double n_i, mean_i, var_i;
  double n_j, mean_j, var_j;

  moments1_calculate (mom_i, &n_i, &mean_i, &var_i, 0, 0);  
  moments1_calculate (mom_j, &n_j, &mean_j, &var_j, 0, 0);

  return (mean_i - mean_j) / std_err;
}

static double scheffe_test_stat (int k, const struct moments1 *mom_i, const struct moments1 *mom_j, double std_err)
{
  double t;
  double n_i, mean_i, var_i;
  double n_j, mean_j, var_j;

  moments1_calculate (mom_i, &n_i, &mean_i, &var_i, 0, 0);  
  moments1_calculate (mom_j, &n_j, &mean_j, &var_j, 0, 0);

  t = (mean_i - mean_j) / std_err;
  t = pow2 (t);
  t /= k - 1;

  return t;
}

static double gh_test_stat (int k UNUSED, const struct moments1 *mom_i, const struct moments1 *mom_j, double std_err UNUSED)
{
  double ts;
  double thing;
  double n_i, mean_i, var_i;
  double n_j, mean_j, var_j;

  moments1_calculate (mom_i, &n_i, &mean_i, &var_i, 0, 0);  
  moments1_calculate (mom_j, &n_j, &mean_j, &var_j, 0, 0);

  thing = var_i / n_i + var_j / n_j;
  thing /= 2.0;
  thing = sqrt (thing);

  ts = (mean_i - mean_j) / thing;

  return fabs (ts);
}



static const struct posthoc ph_tests [] = 
  {
    { "LSD",        N_("LSD"),          df_common, lsd_test_stat,     lsd_1tailsig,          lsd_pinv},
    { "TUKEY",      N_("Tukey HSD"),    df_common, tukey_test_stat,   tukey_1tailsig,        tukey_pinv},
    { "BONFERRONI", N_("Bonferroni"),   df_common, lsd_test_stat,     bonferroni_1tailsig,   bonferroni_pinv},
    { "SCHEFFE",    N_("Scheffé"),      df_common, scheffe_test_stat, scheffe_1tailsig,      scheffe_pinv},
    { "GH",         N_("Games-Howell"), df_individual, gh_test_stat,  tukey_1tailsig,        gh_pinv},
    { "SIDAK",      N_("Šidák"),        df_common, lsd_test_stat,     sidak_1tailsig,        sidak_pinv}
  };


struct oneway_workspace
{
  /* The number of distinct values of the independent variable, when all
     missing values are disregarded */
  int actual_number_of_groups;

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


static void
destroy_coeff_list (struct contrasts_node *coeff_list)
{
  struct coeff_node *cn = NULL;
  struct coeff_node *cnx = NULL;
  struct ll_list *cl = &coeff_list->coefficient_list;
  
  ll_for_each_safe (cn, cnx, struct coeff_node, ll, cl)
    {
      free (cn);
    }
  
  free (coeff_list);
}

static void
oneway_cleanup (struct oneway_spec *cmd)
{
  struct contrasts_node *coeff_list  = NULL;
  struct contrasts_node *coeff_next  = NULL;
  ll_for_each_safe (coeff_list, coeff_next, struct contrasts_node, ll, &cmd->contrast_list)
    {
      destroy_coeff_list (coeff_list);
    }

  free (cmd->posthoc);
}



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
  oneway.alpha = 0.05;
  oneway.posthoc = NULL;
  oneway.n_posthoc = 0;

  ll_init (&oneway.contrast_list);

  
  if ( lex_match (lexer, T_SLASH))
    {
      if (!lex_force_match_id (lexer, "VARIABLES"))
	{
	  goto error;
	}
      lex_match (lexer, T_EQUALS);
    }

  if (!parse_variables_const (lexer, dict,
			      &oneway.vars, &oneway.n_vars,
			      PV_NO_DUPLICATE | PV_NUMERIC))
    goto error;

  lex_force_match (lexer, T_BY);

  oneway.indep_var = parse_variable_const (lexer, dict);

  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "STATISTICS"))
	{
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
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
      else if (lex_match_id (lexer, "POSTHOC"))
	{
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      int p;
	      bool method = false;
	      for (p = 0 ; p < sizeof (ph_tests) / sizeof (struct posthoc); ++p)
		{
		  if (lex_match_id (lexer, ph_tests[p].syntax))
		    {
		      oneway.n_posthoc++;
		      oneway.posthoc = xrealloc (oneway.posthoc, sizeof (*oneway.posthoc) * oneway.n_posthoc);
		      oneway.posthoc[oneway.n_posthoc - 1] = p;
		      method = true;
		      break;
		    }
		}
	      if ( method == false)
		{
		  if (lex_match_id (lexer, "ALPHA"))
		    {
		      if ( !lex_force_match (lexer, T_LPAREN))
			goto error;
		      lex_force_num (lexer);
		      oneway.alpha = lex_number (lexer);
		      lex_get (lexer);
		      if ( !lex_force_match (lexer, T_RPAREN))
			goto error;
		    }
		  else
		    {
		      msg (SE, _("The post hoc analysis method %s is not supported."), lex_tokcstr (lexer));
		      lex_error (lexer, NULL);
		      goto error;
		    }
		}
	    }
	}
      else if (lex_match_id (lexer, "CONTRAST"))
	{
	  struct contrasts_node *cl = xzalloc (sizeof *cl);

	  struct ll_list *coefficient_list = &cl->coefficient_list;
          lex_match (lexer, T_EQUALS);

	  ll_init (coefficient_list);

          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
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
		  destroy_coeff_list (cl);
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }

	  ll_push_tail (&oneway.contrast_list, &cl->ll);
	}
      else if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
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

  oneway_cleanup (&oneway);
  free (oneway.vars);
  return CMD_SUCCESS;

 error:
  oneway_cleanup (&oneway);
  free (oneway.vars);
  return CMD_FAILURE;
}





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

static void
dd_destroy (struct descriptive_data *dd)
{
  moments1_destroy (dd->mom);
  free (dd);
}

static void *
makeit (const void *aux1, void *aux2 UNUSED)
{
  const struct variable *var = aux1;

  struct descriptive_data *dd = dd_create (var);

  return dd;
}

static void 
killit (const void *aux1 UNUSED, void *aux2 UNUSED, void *user_data)
{
  struct descriptive_data *dd = user_data;

  dd_destroy (dd);
}


static void 
updateit (const void *aux1, void *aux2, void *user_data,
	  const struct ccase *c, double weight)
{
  struct descriptive_data *dd = user_data;

  const struct variable *varp = aux1;

  const union value *valx = case_data (c, varp);

  struct descriptive_data *dd_total = aux2;

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
  ws.vws = xzalloc (cmd->n_vars * sizeof (*ws.vws));
  ws.dd_total = xmalloc (sizeof (struct descriptive_data) * cmd->n_vars);

  for (v = 0 ; v < cmd->n_vars; ++v)
    ws.dd_total[v] = dd_create (cmd->vars[v]);

  for (v = 0; v < cmd->n_vars; ++v)
    {
      struct payload payload;
      payload.create = makeit;
      payload.update = updateit;
      payload.calculate = NULL;
      payload.destroy = killit;

      ws.vws[v].iact = interaction_create (cmd->indep_var);
      ws.vws[v].cat = categoricals_create (&ws.vws[v].iact, 1, cmd->wv,
                                           cmd->exclude, cmd->exclude);

      categoricals_set_payload (ws.vws[v].cat, &payload, 
				CONST_CAST (struct variable *, cmd->vars[v]),
				ws.dd_total[v]);


      ws.vws[v].cov = covariance_2pass_create (1, &cmd->vars[v],
					       ws.vws[v].cat, 
					       cmd->wv, cmd->exclude);
      ws.vws[v].nl = levene_create (var_get_width (cmd->indep_var), NULL);
    }

  c = casereader_peek (input, 0);
  if (c == NULL)
    {
      casereader_destroy (input);
      goto finish;
    }
  output_split_file_values (ds, c);
  case_unref (c);

  taint = taint_clone (casereader_get_taint (input));

  input = casereader_create_filter_missing (input, &cmd->indep_var, 1,
                                            cmd->exclude, NULL, NULL);
  if (cmd->missing_type == MISS_LISTWISE)
    input = casereader_create_filter_missing (input, cmd->vars, cmd->n_vars,
                                              cmd->exclude, NULL, NULL);
  input = casereader_create_filter_weight (input, dict, NULL, NULL);

  reader = casereader_clone (input);
  for (; (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      int i;
      double w = dict_get_case_weight (dict, c, NULL);

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

	  covariance_accumulate_pass1 (pvw->cov, c);
	  levene_pass_one (pvw->nl, val->f, w, case_data (c, cmd->indep_var));
	}
    }
  casereader_destroy (reader);

  reader = casereader_clone (input);
  for ( ; (c = casereader_read (reader) ); case_unref (c))
    {
      int i;
      double w = dict_get_case_weight (dict, c, NULL);
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
	  levene_pass_two (pvw->nl, val->f, w, case_data (c, cmd->indep_var));
	}
    }
  casereader_destroy (reader);

  reader = casereader_clone (input);
  for ( ; (c = casereader_read (reader) ); case_unref (c))
    {
      int i;
      double w = dict_get_case_weight (dict, c, NULL);

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

	  levene_pass_three (pvw->nl, val->f, w, case_data (c, cmd->indep_var));
	}
    }
  casereader_destroy (reader);


  for (v = 0; v < cmd->n_vars; ++v)
    {
      gsl_matrix *cm;
      struct per_var_ws *pvw = &ws.vws[v];
      const struct categoricals *cats = covariance_get_categoricals (pvw->cov);
      const bool ok = categoricals_sane (cats);

      if ( ! ok)
	{
	  msg (MW, 
	       _("Dependent variable %s has no non-missing values.  No analysis for this variable will be done."),
	       var_get_name (cmd->vars[v]));
	  continue;
	}

      cm = covariance_calculate_unnormalized (pvw->cov);

      moments1_calculate (ws.dd_total[v]->mom, &pvw->n, NULL, NULL, NULL, NULL);

      pvw->sst = gsl_matrix_get (cm, 0, 0);

      reg_sweep (cm, 0);

      pvw->sse = gsl_matrix_get (cm, 0, 0);

      pvw->ssa = pvw->sst - pvw->sse;

      pvw->n_groups = categoricals_n_total (cats);

      pvw->mse = (pvw->sst - pvw->ssa) / (pvw->n - pvw->n_groups);

      gsl_matrix_free (cm);
    }

  for (v = 0; v < cmd->n_vars; ++v)
    {
      const struct categoricals *cats = covariance_get_categoricals (ws.vws[v].cov);

      if ( ! categoricals_is_complete (cats))
	{
	  continue;
	}

      if (categoricals_n_total (cats) > ws.actual_number_of_groups)
	ws.actual_number_of_groups = categoricals_n_total (cats);
    }

  casereader_destroy (input);

  if (!taint_has_tainted_successor (taint))
    output_oneway (cmd, &ws);

  taint_destroy (taint);

 finish:

  for (v = 0; v < cmd->n_vars; ++v)
    {
      covariance_destroy (ws.vws[v].cov);
      levene_destroy (ws.vws[v].nl);
      dd_destroy (ws.dd_total[v]);
      interaction_destroy (ws.vws[v].iact);
    }

  free (ws.vws);
  free (ws.dd_total);
}

static void show_contrast_coeffs (const struct oneway_spec *cmd, const struct oneway_workspace *ws);
static void show_contrast_tests (const struct oneway_spec *cmd, const struct oneway_workspace *ws);
static void show_comparisons (const struct oneway_spec *cmd, const struct oneway_workspace *ws, int depvar);

static void
output_oneway (const struct oneway_spec *cmd, struct oneway_workspace *ws)
{
  size_t i = 0;

  /* Check the sanity of the given contrast values */
  struct contrasts_node *coeff_list  = NULL;
  struct contrasts_node *coeff_next  = NULL;
  ll_for_each_safe (coeff_list, coeff_next, struct contrasts_node, ll, &cmd->contrast_list)
    {
      struct coeff_node *cn = NULL;
      double sum = 0;
      struct ll_list *cl = &coeff_list->coefficient_list;
      ++i;

      if (ll_count (cl) != ws->actual_number_of_groups)
	{
	  msg (SW,
	       _("In contrast list %zu, the number of coefficients (%zu) does not equal the number of groups (%d). This contrast list will be ignored."),
	       i, ll_count (cl), ws->actual_number_of_groups);

	  ll_remove (&coeff_list->ll);
	  destroy_coeff_list (coeff_list);
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

  if ( cmd->posthoc )
    {
      int v;
      for (v = 0 ; v < cmd->n_vars; ++v)
	{
	  const struct categoricals *cats = covariance_get_categoricals (ws->vws[v].cov);

	  if ( categoricals_is_complete (cats))
	    show_comparisons (cmd, ws, v);
	}
    }
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

      for (count = 0; count < categoricals_n_total (cats); ++count)
	{
	  double T;
	  double n, mean, variance;
	  double std_dev, std_error ;

	  struct string vstr;

	  const struct ccase *gcc = categoricals_get_case_by_category (cats, count);
	  const struct descriptive_data *dd = categoricals_get_user_data_by_category (cats, count);

	  moments1_calculate (dd->mom, &n, &mean, &variance, NULL, NULL);

	  std_dev = sqrt (variance);
	  std_error = std_dev / sqrt (n) ;

	  ds_init_empty (&vstr);

	  var_append_value_name (cmd->indep_var, case_data (gcc, cmd->indep_var), &vstr);

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

      if (categoricals_is_complete (cats))
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

      row += categoricals_n_total (cats) + 1;
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
      double F = levene_calculate (pvw->nl);

      const struct variable *var = cmd->vars[v];
      const char *s = var_to_string (var);
      double df1, df2;

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
	  const struct ccase *gcc = categoricals_get_case_by_category (cats, count);
	  struct coeff_node *coeffn = ll_data (coeffi, struct coeff_node, ll);
	  struct string vstr;

	  ds_init_empty (&vstr);

	  var_append_value_name (cmd->indep_var, case_data (gcc, cmd->indep_var), &vstr);

	  tab_text (t, count + 2, 1, TAB_CENTER | TAT_TITLE, ds_cstr (&vstr));

	  ds_destroy (&vstr);

	  tab_text_format (t, count + 2, c_num + 2, TAB_RIGHT, "%g", coeffn->coeff);
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

	  for (coeffi = ll_head (&cn->coefficient_list);
	       coeffi != ll_null (&cn->coefficient_list);
	       ++ci, coeffi = ll_next (coeffi))
	    {
	      double n, mean, variance;
	      const struct descriptive_data *dd = categoricals_get_user_data_by_category (cats, ci);
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



static void
show_comparisons (const struct oneway_spec *cmd, const struct oneway_workspace *ws, int v)
{
  const int n_cols = 8;
  const int heading_rows = 2;
  const int heading_cols = 3;

  int p;
  int r = heading_rows ;

  const struct per_var_ws *pvw = &ws->vws[v];
  const struct categoricals *cat = pvw->cat;
  const int n_rows = heading_rows + cmd->n_posthoc * pvw->n_groups * (pvw->n_groups - 1);

  struct tab_table *t = tab_create (n_cols, n_rows);

  tab_headers (t, heading_cols, 0, heading_rows, 0);

  /* Put a frame around the entire box, and vertical lines inside */
  tab_box (t,
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   n_cols - 1, n_rows - 1);

  tab_box (t,
	   -1, -1,
	   -1, TAL_1,
	   heading_cols, 0,
	   n_cols - 1, n_rows - 1);

  tab_vline (t, TAL_2, heading_cols, 0, n_rows - 1);

  tab_title (t, _("Multiple Comparisons (%s)"), var_to_string (cmd->vars[v]));

  tab_text_format (t,  1, 1, TAB_LEFT | TAT_TITLE, _("(I) %s"), var_to_string (cmd->indep_var));
  tab_text_format (t,  2, 1, TAB_LEFT | TAT_TITLE, _("(J) %s"), var_to_string (cmd->indep_var));
  tab_text (t,  3, 0, TAB_CENTER | TAT_TITLE, _("Mean Difference"));
  tab_text (t,  3, 1, TAB_CENTER | TAT_TITLE, _("(I - J)"));
  tab_text (t,  4, 1, TAB_CENTER | TAT_TITLE, _("Std. Error"));
  tab_text (t,  5, 1, TAB_CENTER | TAT_TITLE, _("Sig."));

  tab_joint_text_format (t, 6, 0, 7, 0, TAB_CENTER | TAT_TITLE,
                         _("%g%% Confidence Interval"),
                         (1 - cmd->alpha) * 100.0);

  tab_text (t,  6, 1, TAB_CENTER | TAT_TITLE, _("Lower Bound"));
  tab_text (t,  7, 1, TAB_CENTER | TAT_TITLE, _("Upper Bound"));


  for (p = 0; p < cmd->n_posthoc; ++p)
    {
      int i;
      const struct posthoc *ph = &ph_tests[cmd->posthoc[p]];

      tab_hline (t, TAL_2, 0, n_cols - 1, r);

      tab_text (t, 0, r, TAB_LEFT | TAT_TITLE, gettext (ph->label));

      for (i = 0; i < pvw->n_groups ; ++i)
	{
	  double weight_i, mean_i, var_i;
	  int rx = 0;
	  struct string vstr;
	  int j;
	  struct descriptive_data *dd_i = categoricals_get_user_data_by_category (cat, i);
	  const struct ccase *gcc = categoricals_get_case_by_category (cat, i);
	  

	  ds_init_empty (&vstr);
	  var_append_value_name (cmd->indep_var, case_data (gcc, cmd->indep_var), &vstr);

	  if ( i != 0)
	    tab_hline (t, TAL_1, 1, n_cols - 1, r);
	  tab_text (t, 1, r, TAB_LEFT | TAT_TITLE, ds_cstr (&vstr));

	  moments1_calculate (dd_i->mom, &weight_i, &mean_i, &var_i, 0, 0);

	  for (j = 0 ; j < pvw->n_groups; ++j)
	    {
	      double std_err;
	      double weight_j, mean_j, var_j;
	      double half_range;
	      const struct ccase *cc;
	      struct descriptive_data *dd_j = categoricals_get_user_data_by_category (cat, j);
	      if (j == i)
		continue;

	      ds_clear (&vstr);
	      cc = categoricals_get_case_by_category (cat, j);
	      var_append_value_name (cmd->indep_var, case_data (cc, cmd->indep_var), &vstr);
	      tab_text (t, 2, r + rx, TAB_LEFT | TAT_TITLE, ds_cstr (&vstr));

	      moments1_calculate (dd_j->mom, &weight_j, &mean_j, &var_j, 0, 0);

	      tab_double  (t, 3, r + rx, 0, mean_i - mean_j, 0);

	      std_err = pvw->mse;
	      std_err *= weight_i + weight_j;
	      std_err /= weight_i * weight_j;
	      std_err = sqrt (std_err);

	      tab_double  (t, 4, r + rx, 0, std_err, 0);
	  
	      tab_double (t, 5, r + rx, 0, 2 * multiple_comparison_sig (std_err, pvw, dd_i, dd_j, ph), 0);

	      half_range = mc_half_range (cmd, pvw, std_err, dd_i, dd_j, ph);

	      tab_double (t, 6, r + rx, 0,
			   (mean_i - mean_j) - half_range, 0 );

	      tab_double (t, 7, r + rx, 0,
			   (mean_i - mean_j) + half_range, 0 );

	      rx++;
	    }
	  ds_destroy (&vstr);
	  r += pvw->n_groups - 1;
	}
    }

  tab_submit (t);
}
