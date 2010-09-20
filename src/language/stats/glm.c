/* PSPP - a program for statistical analysis.
   Copyright (C) 2010 Free Software Foundation, Inc.

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
#include <libpspp/taint.h>
#include <libpspp/misc.h>

#include <gsl/gsl_cdf.h>
#include <math.h>
#include <data/format.h>

#include <libpspp/message.h>

#include <output/tab.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct glm_spec
{
  size_t n_dep_vars;
  const struct variable **dep_vars;

  size_t n_factor_vars;
  const struct variable **factor_vars;

  enum mv_class exclude;

  /* The weight variable */
  const struct variable *wv;

  bool intercept;
};

struct glm_workspace
{
  double total_ssq;
  struct moments *totals;
};

static void output_glm (const struct glm_spec *, const struct glm_workspace *ws);
static void run_glm (const struct glm_spec *cmd, struct casereader *input, const struct dataset *ds);

int
cmd_glm (struct lexer *lexer, struct dataset *ds)
{
  const struct dictionary *dict = dataset_dict (ds);  
  struct glm_spec glm ;
  glm.n_dep_vars = 0;
  glm.n_factor_vars = 0;
  glm.dep_vars = NULL;
  glm.factor_vars = NULL;
  glm.exclude = MV_ANY;
  glm.intercept = true;
  glm.wv = dict_get_weight (dict);

  
  if (!parse_variables_const (lexer, dict,
			      &glm.dep_vars, &glm.n_dep_vars,
			      PV_NO_DUPLICATE | PV_NUMERIC))
    goto error;

  lex_force_match (lexer, T_BY);

  if (!parse_variables_const (lexer, dict,
			      &glm.factor_vars, &glm.n_factor_vars,
			      PV_NO_DUPLICATE | PV_NUMERIC))
    goto error;

  if ( glm.n_dep_vars > 1)
    {
      msg (ME, _("Multivariate analysis is not yet implemented"));
      return CMD_FAILURE;
    }

  struct const_var_set *factors = const_var_set_create_from_array (glm.factor_vars, glm.n_factor_vars);


  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
	      if (lex_match_id (lexer, "INCLUDE"))
		{
		  glm.exclude = MV_SYSTEM;
		}
	      else if (lex_match_id (lexer, "EXCLUDE"))
		{
		  glm.exclude = MV_ANY;
		}
	      else
		{
                  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "INTERCEPT"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
	      if (lex_match_id (lexer, "INCLUDE"))
		{
		  glm.intercept = true;
		}
	      else if (lex_match_id (lexer, "EXCLUDE"))
		{
		  glm.intercept = false;
		}
	      else
		{
                  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "DESIGN"))
        {
	  size_t n_des;
	  const struct variable **des;
          lex_match (lexer, T_EQUALS);

	  parse_const_var_set_vars (lexer, factors, &des, &n_des, 0);
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
      run_glm (&glm, group, ds);
    ok = casegrouper_destroy (grouper);
    ok = proc_commit (ds) && ok;
  }

  return CMD_SUCCESS;

 error:
  return CMD_FAILURE;
}

static  void dump_matrix (const gsl_matrix *m);

static void
run_glm (const struct glm_spec *cmd, struct casereader *input, const struct dataset *ds)
{
  int v;
  struct taint *taint;
  struct dictionary *dict = dataset_dict (ds);
  struct casereader *reader;
  struct ccase *c;

  struct glm_workspace ws;

  struct categoricals *cats = categoricals_create (cmd->factor_vars, cmd->n_factor_vars,
						   cmd->wv, cmd->exclude, 
						   NULL, NULL,
						   NULL, NULL);
  
  struct covariance *cov = covariance_2pass_create (cmd->n_dep_vars, cmd->dep_vars,
					       cats, 
					       cmd->wv, cmd->exclude);


  c = casereader_peek (input, 0);
  if (c == NULL)
    {
      casereader_destroy (input);
      return;
    }
  output_split_file_values (ds, c);
  case_unref (c);

  taint = taint_clone (casereader_get_taint (input));

  ws.totals = moments_create (MOMENT_VARIANCE);

  bool warn_bad_weight = true;
  for (reader = casereader_clone (input);
       (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      double weight = dict_get_case_weight (dict, c, &warn_bad_weight);

      for ( v = 0; v < cmd->n_dep_vars; ++v)
	moments_pass_one (ws.totals, case_data (c, cmd->dep_vars[v])->f, weight);

      covariance_accumulate_pass1 (cov, c);
    }
  casereader_destroy (reader);

  categoricals_done (cats);

  for (reader = casereader_clone (input);
       (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      double weight = dict_get_case_weight (dict, c, &warn_bad_weight);

      for ( v = 0; v < cmd->n_dep_vars; ++v)
	moments_pass_two (ws.totals, case_data (c, cmd->dep_vars[v])->f, weight);

      covariance_accumulate_pass2 (cov, c);
    }
  casereader_destroy (reader);

  {
    gsl_matrix *cm = covariance_calculate_unnormalized (cov);

    dump_matrix (cm);

    ws.total_ssq = gsl_matrix_get (cm, 0, 0);

    reg_sweep (cm, 0);

    dump_matrix (cm);
  }

  if (!taint_has_tainted_successor (taint))
    output_glm (cmd, &ws);

  taint_destroy (taint);
}

static void
output_glm (const struct glm_spec *cmd, const struct glm_workspace *ws)
{
  const struct fmt_spec *wfmt = cmd->wv ? var_get_print_format (cmd->wv) : &F_8_0;

  int f;
  int r;
  const int heading_columns = 1;
  const int heading_rows = 1;
  struct tab_table *t ;

  const int nc = 6;
  int nr = heading_rows + 4 + cmd->n_factor_vars;
  if (cmd->intercept)
    nr++;

  t = tab_create (nc, nr);
  tab_title (t, _("Tests of Between-Subjects Effects"));

  tab_headers (t, heading_columns, 0, heading_rows, 0);

  tab_box (t,
	   TAL_2, TAL_2,
	   -1, TAL_1,
	   0, 0,
	   nc - 1, nr - 1);

  tab_hline (t, TAL_2, 0, nc - 1, heading_rows);
  tab_vline (t, TAL_2, heading_columns, 0, nr - 1);

  tab_text (t, 0, 0, TAB_CENTER | TAT_TITLE, _("Source"));

  /* TRANSLATORS: The parameter is a roman numeral */
  tab_text_format (t, 1, 0, TAB_CENTER | TAT_TITLE, _("Type %s Sum of Squares"), "III");
  tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("df"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("Mean Square"));
  tab_text (t, 4, 0, TAB_CENTER | TAT_TITLE, _("F"));
  tab_text (t, 5, 0, TAB_CENTER | TAT_TITLE, _("Sig."));

  r = heading_rows;
  tab_text (t, 0, r++, TAB_LEFT | TAT_TITLE, _("Corrected Model"));

  double intercept, n_total;
  if (cmd->intercept)
    {
      double mean;
      moments_calculate (ws->totals, &n_total, &mean, NULL, NULL, NULL);
      intercept = pow2 (mean * n_total) / n_total;

      tab_text (t, 0, r, TAB_LEFT | TAT_TITLE, _("Intercept"));
      tab_double (t, 1, r, 0, intercept, NULL);
      tab_double (t, 2, r, 0, 1.00, wfmt);

      tab_double (t, 3, r, 0, intercept / 1.0 , NULL);
      r++;
    }

  for (f = 0; f < cmd->n_factor_vars; ++f)
    {
      tab_text (t, 0, r++, TAB_LEFT | TAT_TITLE,
		var_to_string (cmd->factor_vars[f]));
    }

  tab_text (t, 0, r++, TAB_LEFT | TAT_TITLE, _("Error"));

  if (cmd->intercept)
    {
      double ssq = intercept + ws->total_ssq;
      double mse = ssq / n_total;
      tab_text (t, 0, r, TAB_LEFT | TAT_TITLE, _("Total"));
      tab_double (t, 1, r, 0, ssq, NULL);
      tab_double (t, 2, r, 0, n_total, wfmt);

      r++;
    }

  tab_text (t, 0, r, TAB_LEFT | TAT_TITLE, _("Corrected Total"));

  tab_double (t, 1, r, 0, ws->total_ssq, NULL);
  tab_double (t, 2, r, 0, n_total - 1.0, wfmt);

  tab_submit (t);
}

static 
void dump_matrix (const gsl_matrix *m)
{
  size_t i, j;
  for (i = 0; i < m->size1; ++i)
    {
      for (j = 0; j < m->size2; ++j)
	{
	  double x = gsl_matrix_get (m, i, j);
	  printf ("%.3f ", x);
	}
      printf ("\n");
    }
  printf ("\n");
}
