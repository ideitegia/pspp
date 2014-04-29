/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/dictionary/split-file.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/assertion.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "math/correlation.h"
#include "math/covariance.h"
#include "math/moments.h"
#include "output/tab.h"

#include "gl/xalloc.h"
#include "gl/minmax.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


struct corr
{
  size_t n_vars_total;
  size_t n_vars1;

  const struct variable **vars;
};


/* Handling of missing values. */
enum corr_missing_type
  {
    CORR_PAIRWISE,       /* Handle missing values on a per-variable-pair basis. */
    CORR_LISTWISE        /* Discard entire case if any variable is missing. */
  };

enum stats_opts
  {
    STATS_DESCRIPTIVES = 0x01,
    STATS_XPROD = 0x02,
    STATS_ALL = STATS_XPROD | STATS_DESCRIPTIVES
  };

struct corr_opts
{
  enum corr_missing_type missing_type;
  enum mv_class exclude;      /* Classes of missing values to exclude. */

  bool sig;   /* Flag significant values or not */
  int tails;  /* Report significance with how many tails ? */
  enum stats_opts statistics;

  const struct variable *wv;  /* The weight variable (if any) */
};


static void
output_descriptives (const struct corr *corr, const gsl_matrix *means,
		     const gsl_matrix *vars, const gsl_matrix *ns)
{
  const int nr = corr->n_vars_total + 1;
  const int nc = 4;
  int c, r;

  const int heading_columns = 1;
  const int heading_rows = 1;

  struct tab_table *t = tab_create (nc, nr);
  tab_title (t, _("Descriptive Statistics"));

  tab_headers (t, heading_columns, 0, heading_rows, 0);

  /* Outline the box */
  tab_box (t,
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   nc - 1, nr - 1);

  /* Vertical lines */
  tab_box (t,
	   -1, -1,
	   -1, TAL_1,
	   heading_columns, 0,
	   nc - 1, nr - 1);

  tab_vline (t, TAL_2, heading_columns, 0, nr - 1);
  tab_hline (t, TAL_1, 0, nc - 1, heading_rows);

  tab_text (t, 1, 0, TAB_CENTER | TAT_TITLE, _("Mean"));
  tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("Std. Deviation"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("N"));

  for (r = 0 ; r < corr->n_vars_total ; ++r)
    {
      const struct variable *v = corr->vars[r];
      tab_text (t, 0, r + heading_rows, TAB_LEFT | TAT_TITLE, var_to_string (v));

      for (c = 1 ; c < nc ; ++c)
	{
	  double x ;
	  double n;
	  switch (c)
	    {
	    case 1:
	      x = gsl_matrix_get (means, r, 0);
	      break;
	    case 2:
	      x = gsl_matrix_get (vars, r, 0);

	      /* Here we want to display the non-biased estimator */
	      n = gsl_matrix_get (ns, r, 0);
	      x *= n / (n -1);

	      x = sqrt (x);
	      break;
	    case 3:
	      x = gsl_matrix_get (ns, r, 0);
	      break;
	    default: 
	      NOT_REACHED ();
	    };
	  
	  tab_double (t, c, r + heading_rows, 0, x, NULL, RC_OTHER);
	}
    }

  tab_submit (t);
}

static void
output_correlation (const struct corr *corr, const struct corr_opts *opts,
		    const gsl_matrix *cm, const gsl_matrix *samples,
		    const gsl_matrix *cv)
{
  int r, c;
  struct tab_table *t;
  int matrix_cols;
  int nr = corr->n_vars1;
  int nc = matrix_cols = corr->n_vars_total > corr->n_vars1 ?
    corr->n_vars_total - corr->n_vars1 : corr->n_vars1;

  const struct fmt_spec *wfmt = opts->wv ? var_get_print_format (opts->wv) : & F_8_0;

  const int heading_columns = 2;
  const int heading_rows = 1;

  int rows_per_variable = opts->missing_type == CORR_LISTWISE ? 2 : 3;

  if (opts->statistics & STATS_XPROD)
    rows_per_variable += 2;

  /* Two header columns */
  nc += heading_columns;

  /* Three data per variable */
  nr *= rows_per_variable;

  /* One header row */
  nr += heading_rows;

  t = tab_create (nc, nr);
  tab_set_format (t, RC_WEIGHT, wfmt);
  tab_title (t, _("Correlations"));

  tab_headers (t, heading_columns, 0, heading_rows, 0);

  /* Outline the box */
  tab_box (t,
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   nc - 1, nr - 1);

  /* Vertical lines */
  tab_box (t,
	   -1, -1,
	   -1, TAL_1,
	   heading_columns, 0,
	   nc - 1, nr - 1);

  tab_vline (t, TAL_2, heading_columns, 0, nr - 1);

  tab_vline (t, TAL_1, 1, heading_rows, nr - 1);

  /* Row Headers */
  for (r = 0 ; r < corr->n_vars1 ; ++r)
    {
      tab_text (t, 0, 1 + r * rows_per_variable, TAB_LEFT | TAT_TITLE, 
		var_to_string (corr->vars[r]));

      tab_text (t, 1, 1 + r * rows_per_variable, TAB_LEFT | TAT_TITLE, _("Pearson Correlation"));
      tab_text (t, 1, 2 + r * rows_per_variable, TAB_LEFT | TAT_TITLE, 
		(opts->tails == 2) ? _("Sig. (2-tailed)") : _("Sig. (1-tailed)"));

      if (opts->statistics & STATS_XPROD)
	{
	  tab_text (t, 1, 3 + r * rows_per_variable, TAB_LEFT | TAT_TITLE, _("Cross-products"));
	  tab_text (t, 1, 4 + r * rows_per_variable, TAB_LEFT | TAT_TITLE, _("Covariance"));
	}

      if ( opts->missing_type != CORR_LISTWISE )
	tab_text (t, 1, rows_per_variable + r * rows_per_variable, TAB_LEFT | TAT_TITLE, _("N"));

      tab_hline (t, TAL_1, 0, nc - 1, r * rows_per_variable + 1);
    }

  /* Column Headers */
  for (c = 0 ; c < matrix_cols ; ++c)
    {
      const struct variable *v = corr->n_vars_total > corr->n_vars1 ?
	corr->vars[corr->n_vars1 + c] : corr->vars[c];
      tab_text (t, heading_columns + c, 0, TAB_LEFT | TAT_TITLE, var_to_string (v));      
    }

  for (r = 0 ; r < corr->n_vars1 ; ++r)
    {
      const int row = r * rows_per_variable + heading_rows;
      for (c = 0 ; c < matrix_cols ; ++c)
	{
	  unsigned char flags = 0; 
	  const int col_index = corr->n_vars_total > corr->n_vars1 ? 
	    corr->n_vars1 + c : 
	    c;
	  double pearson = gsl_matrix_get (cm, r, col_index);
	  double w = gsl_matrix_get (samples, r, col_index);
	  double sig = opts->tails * significance_of_correlation (pearson, w);

	  if ( opts->missing_type != CORR_LISTWISE )
	    tab_double (t, c + heading_columns, row + rows_per_variable - 1, 0, w, NULL, RC_WEIGHT);

	  if ( col_index != r)
	    tab_double (t, c + heading_columns, row + 1, 0,  sig, NULL, RC_PVALUE);

	  if ( opts->sig && col_index != r && sig < 0.05)
	    flags = TAB_EMPH;
	  
	  tab_double (t, c + heading_columns, row, flags, pearson, NULL, RC_OTHER);

	  if (opts->statistics & STATS_XPROD)
	    {
	      double cov = gsl_matrix_get (cv, r, col_index);
	      const double xprod_dev = cov * w;
	      cov *= w / (w - 1.0);

	      tab_double (t, c + heading_columns, row + 2, 0, xprod_dev, NULL, RC_OTHER);
	      tab_double (t, c + heading_columns, row + 3, 0, cov, NULL, RC_OTHER);
	    }
	}
    }

  tab_submit (t);
}


static void
run_corr (struct casereader *r, const struct corr_opts *opts, const struct corr *corr)
{
  struct ccase *c;
  const gsl_matrix *var_matrix,  *samples_matrix, *mean_matrix;
  gsl_matrix *cov_matrix;
  gsl_matrix *corr_matrix;
  struct covariance *cov = covariance_2pass_create (corr->n_vars_total, corr->vars,
						    NULL,
						    opts->wv, opts->exclude);

  struct casereader *rc = casereader_clone (r);
  for ( ; (c = casereader_read (r) ); case_unref (c))
    {
      covariance_accumulate_pass1 (cov, c);
    }

  for ( ; (c = casereader_read (rc) ); case_unref (c))
    {
      covariance_accumulate_pass2 (cov, c);
    }

  cov_matrix = covariance_calculate (cov);

  casereader_destroy (rc);

  samples_matrix = covariance_moments (cov, MOMENT_NONE);
  var_matrix = covariance_moments (cov, MOMENT_VARIANCE);
  mean_matrix = covariance_moments (cov, MOMENT_MEAN);

  corr_matrix = correlation_from_covariance (cov_matrix, var_matrix);

  if ( opts->statistics & STATS_DESCRIPTIVES) 
    output_descriptives (corr, mean_matrix, var_matrix, samples_matrix);

  output_correlation (corr, opts, corr_matrix,
		      samples_matrix, cov_matrix);

  covariance_destroy (cov);
  gsl_matrix_free (corr_matrix);
  gsl_matrix_free (cov_matrix);
}

int
cmd_correlation (struct lexer *lexer, struct dataset *ds)
{
  int i;
  int n_all_vars = 0; /* Total number of variables involved in this command */
  const struct variable **all_vars ;
  const struct dictionary *dict = dataset_dict (ds);
  bool ok = true;

  struct casegrouper *grouper;
  struct casereader *group;

  struct corr *corr = NULL;
  size_t n_corrs = 0;

  struct corr_opts opts;
  opts.missing_type = CORR_PAIRWISE;
  opts.wv = dict_get_weight (dict);
  opts.tails = 2;
  opts.sig = false;
  opts.exclude = MV_ANY;
  opts.statistics = 0;

  /* Parse CORRELATIONS. */
  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);
      if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "PAIRWISE"))
                opts.missing_type = CORR_PAIRWISE;
              else if (lex_match_id (lexer, "LISTWISE"))
                opts.missing_type = CORR_LISTWISE;

              else if (lex_match_id (lexer, "INCLUDE"))
                opts.exclude = MV_SYSTEM;
              else if (lex_match_id (lexer, "EXCLUDE"))
		opts.exclude = MV_ANY;
              else
                {
                  lex_error (lexer, NULL);
                  goto error;
                }
              lex_match (lexer, T_COMMA);
            }
        }
      else if (lex_match_id (lexer, "PRINT"))
	{
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      if ( lex_match_id (lexer, "TWOTAIL"))
		opts.tails = 2;
	      else if (lex_match_id (lexer, "ONETAIL"))
		opts.tails = 1;
	      else if (lex_match_id (lexer, "SIG"))
		opts.sig = false;
	      else if (lex_match_id (lexer, "NOSIG"))
		opts.sig = true;
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}

              lex_match (lexer, T_COMMA);
	    }
	}
      else if (lex_match_id (lexer, "STATISTICS"))
	{
	  lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      if ( lex_match_id (lexer, "DESCRIPTIVES"))
		opts.statistics = STATS_DESCRIPTIVES;
	      else if (lex_match_id (lexer, "XPROD"))
		opts.statistics = STATS_XPROD;
	      else if (lex_token (lexer) == T_ALL)
		{
		  opts.statistics = STATS_ALL;
		  lex_get (lexer);
		}
	      else 
		{
		  lex_error (lexer, NULL);
		  goto error;
		}

              lex_match (lexer, T_COMMA);
	    }
	}
      else
	{
	  if (lex_match_id (lexer, "VARIABLES"))
	    {
	      lex_match (lexer, T_EQUALS);
	    }

	  corr = xrealloc (corr, sizeof (*corr) * (n_corrs + 1));
	  corr[n_corrs].n_vars_total = corr[n_corrs].n_vars1 = 0;
      
	  if ( ! parse_variables_const (lexer, dict, &corr[n_corrs].vars, 
					&corr[n_corrs].n_vars_total,
					PV_NUMERIC))
	    {
	      ok = false;
	      break;
	    }


	  corr[n_corrs].n_vars1 = corr[n_corrs].n_vars_total;

	  if ( lex_match (lexer, T_WITH))
	    {
	      if ( ! parse_variables_const (lexer, dict,
					    &corr[n_corrs].vars, &corr[n_corrs].n_vars_total,
					    PV_NUMERIC | PV_APPEND))
		{
		  ok = false;
		  break;
		}
	    }

	  n_all_vars += corr[n_corrs].n_vars_total;

	  n_corrs++;
	}
    }

  if (n_corrs == 0)
    {
      msg (SE, _("No variables specified."));
      goto error;
    }


  all_vars = xmalloc (sizeof (*all_vars) * n_all_vars);

  {
    /* FIXME:  Using a hash here would make more sense */
    const struct variable **vv = all_vars;

    for (i = 0 ; i < n_corrs; ++i)
      {
	int v;
	const struct corr *c = &corr[i];
	for (v = 0 ; v < c->n_vars_total; ++v)
	  *vv++ = c->vars[v];
      }
  }

  grouper = casegrouper_create_splits (proc_open (ds), dict);

  while (casegrouper_get_next_group (grouper, &group))
    {
      for (i = 0 ; i < n_corrs; ++i)
	{
	  /* FIXME: No need to iterate the data multiple times */
	  struct casereader *r = casereader_clone (group);

	  if ( opts.missing_type == CORR_LISTWISE)
	    r = casereader_create_filter_missing (r, all_vars, n_all_vars,
						  opts.exclude, NULL, NULL);


	  run_corr (r, &opts,  &corr[i]);
	  casereader_destroy (r);
	}
      casereader_destroy (group);
    }

  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  free (all_vars);


  /* Done. */
  free (corr->vars);
  free (corr);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;

 error:
  free (corr->vars);
  free (corr);
  return CMD_FAILURE;
}
