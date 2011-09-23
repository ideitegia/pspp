/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011 Free Software Foundation, Inc.

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
#include <gsl/gsl_combination.h>
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
#include "math/categoricals.h"
#include "math/covariance.h"
#include "math/interaction.h"
#include "math/moments.h"
#include "output/tab.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct glm_spec
{
  size_t n_dep_vars;
  const struct variable **dep_vars;

  size_t n_factor_vars;
  const struct variable **factor_vars;

  size_t n_interactions;
  struct interaction **interactions;

  enum mv_class exclude;

  /* The weight variable */
  const struct variable *wv;

  const struct dictionary *dict;

  bool intercept;

  double alpha;
};

struct glm_workspace
{
  double total_ssq;
  struct moments *totals;

  struct categoricals *cats;

  /* 
     Sums of squares due to different variables. Element 0 is the SSE
     for the entire model. For i > 0, element i is the SS due to
     variable i.
   */
  gsl_vector *ssq;
};


/* Default design: all possible interactions */
static void
design_full (struct glm_spec *glm)
{
  int sz;
  int i = 0;
  glm->n_interactions = (1 << glm->n_factor_vars) - 1;

  glm->interactions = xcalloc (glm->n_interactions, sizeof *glm->interactions);

  /* All subsets, with exception of the empty set, of [0, glm->n_factor_vars) */
  for (sz = 1; sz <= glm->n_factor_vars; ++sz)
    {
      gsl_combination *c = gsl_combination_calloc (glm->n_factor_vars, sz);

      do
	{
	  struct interaction *iact = interaction_create (NULL);
	  int e;
	  for (e = 0 ; e < gsl_combination_k (c); ++e)
	    interaction_add_variable (iact, glm->factor_vars [gsl_combination_get (c, e)]);

	  glm->interactions[i++] = iact;
	}
      while (gsl_combination_next (c) == GSL_SUCCESS);

      gsl_combination_free (c);
    }
}

static void output_glm (const struct glm_spec *,
			const struct glm_workspace *ws);
static void run_glm (struct glm_spec *cmd, struct casereader *input,
		     const struct dataset *ds);


static bool parse_design_spec (struct lexer *lexer, struct glm_spec *glm);


int
cmd_glm (struct lexer *lexer, struct dataset *ds)
{
  int i;
  struct const_var_set *factors = NULL;
  struct glm_spec glm;
  bool design = false;
  glm.dict = dataset_dict (ds);
  glm.n_dep_vars = 0;
  glm.n_factor_vars = 0;
  glm.n_interactions = 0;
  glm.interactions = NULL;
  glm.dep_vars = NULL;
  glm.factor_vars = NULL;
  glm.exclude = MV_ANY;
  glm.intercept = true;
  glm.wv = dict_get_weight (glm.dict);
  glm.alpha = 0.05;

  if (!parse_variables_const (lexer, glm.dict,
			      &glm.dep_vars, &glm.n_dep_vars,
			      PV_NO_DUPLICATE | PV_NUMERIC))
    goto error;

  lex_force_match (lexer, T_BY);

  if (!parse_variables_const (lexer, glm.dict,
			      &glm.factor_vars, &glm.n_factor_vars,
			      PV_NO_DUPLICATE | PV_NUMERIC))
    goto error;

  if (glm.n_dep_vars > 1)
    {
      msg (ME, _("Multivariate analysis is not yet implemented"));
      return CMD_FAILURE;
    }

  factors =
    const_var_set_create_from_array (glm.factor_vars, glm.n_factor_vars);

  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "MISSING"))
	{
	  lex_match (lexer, T_EQUALS);
	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
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
	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
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
      else if (lex_match_id (lexer, "CRITERIA"))
	{
	  lex_match (lexer, T_EQUALS);
	  if (lex_match_id (lexer, "ALPHA"))
	    {
	      if (lex_force_match (lexer, T_LPAREN))
		{
		  if (! lex_force_num (lexer))
		    {
		      lex_error (lexer, NULL);
		      goto error;
		    }
		  
		  glm.alpha = lex_number (lexer);
		  lex_get (lexer);
		  if ( ! lex_force_match (lexer, T_RPAREN))
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
      else if (lex_match_id (lexer, "METHOD"))
	{
	  lex_match (lexer, T_EQUALS);
	  if ( !lex_force_match_id (lexer, "SSTYPE"))
	    {
	      lex_error (lexer, NULL);
	      goto error;
	    }

	  if ( ! lex_force_match (lexer, T_LPAREN))
	    {
	      lex_error (lexer, NULL);
	      goto error;
	    }

	  if ( ! lex_force_int (lexer))
	    {
	      lex_error (lexer, NULL);
	      goto error;
	    }

	  if (3 != lex_integer (lexer))
	    {
	      msg (ME, _("Only type 3 sum of squares are currently implemented"));
	      goto error;
	    }

	  lex_get (lexer);

	  if ( ! lex_force_match (lexer, T_RPAREN))
	    {
	      lex_error (lexer, NULL);
	      goto error;
	    }
	}
      else if (lex_match_id (lexer, "DESIGN"))
	{
	  lex_match (lexer, T_EQUALS);

	  if (! parse_design_spec (lexer, &glm))
	    goto error;

	  if (glm.n_interactions > 0)
	    design = true;
	}
      else
	{
	  lex_error (lexer, NULL);
	  goto error;
	}
    }

  if ( ! design )
    {
      design_full (&glm);
    }

  {
    struct casegrouper *grouper;
    struct casereader *group;
    bool ok;

    grouper = casegrouper_create_splits (proc_open (ds), glm.dict);
    while (casegrouper_get_next_group (grouper, &group))
      run_glm (&glm, group, ds);
    ok = casegrouper_destroy (grouper);
    ok = proc_commit (ds) && ok;
  }

  const_var_set_destroy (factors);
  free (glm.factor_vars);
  for (i = 0 ; i < glm.n_interactions; ++i)
    interaction_destroy (glm.interactions[i]);
  free (glm.interactions);
  free (glm.dep_vars);


  return CMD_SUCCESS;

error:

  const_var_set_destroy (factors);
  free (glm.factor_vars);
  for (i = 0 ; i < glm.n_interactions; ++i)
    interaction_destroy (glm.interactions[i]);

  free (glm.interactions);
  free (glm.dep_vars);

  return CMD_FAILURE;
}

static void get_ssq (struct covariance *, gsl_vector *,
		     const struct glm_spec *);

static bool
not_dropped (size_t j, const size_t *dropped, size_t n_dropped)
{
  size_t i;

  for (i = 0; i < n_dropped; i++)
    {
      if (j == dropped[i])
	return false;
    }
  return true;
}

static void
fill_submatrix (gsl_matrix * cov, gsl_matrix * submatrix, size_t * dropped,
		size_t n_dropped)
{
  size_t i;
  size_t j;
  size_t n = 0;
  size_t m = 0;
  
  for (i = 0; i < cov->size1; i++)
    {
      if (not_dropped (i, dropped, n_dropped))
	{	  
	  m = 0;
	  for (j = 0; j < cov->size2; j++)
	    {
	      if (not_dropped (j, dropped, n_dropped))
		{
		  gsl_matrix_set (submatrix, n, m,
				  gsl_matrix_get (cov, i, j));
		  m++;
		}	
	    }
	  n++;
	}
    }
}
	      
static void
get_ssq (struct covariance *cov, gsl_vector *ssq, const struct glm_spec *cmd)
{
  gsl_matrix *cm = covariance_calculate_unnormalized (cov);
  size_t i;
  size_t k;
  size_t *model_dropped = xcalloc (covariance_dim (cov), sizeof (*model_dropped));
  size_t *submodel_dropped = xcalloc (covariance_dim (cov), sizeof (*submodel_dropped));
  const struct categoricals *cats = covariance_get_categoricals (cov);

  for (k = 0; k < cmd->n_interactions; k++)
    {
      gsl_matrix *model_cov = NULL;
      gsl_matrix *submodel_cov = NULL;
      size_t n_dropped_model = 0;
      size_t n_dropped_submodel = 0;
      for (i = cmd->n_dep_vars; i < covariance_dim (cov); i++)
	{
	  const struct interaction * x = 
	    categoricals_get_interaction_by_subscript (cats, i - cmd->n_dep_vars);
	  if (interaction_is_proper_subset (cmd->interactions [k], x))
	    {
	      assert (n_dropped_model < covariance_dim (cov));
	      model_dropped[n_dropped_model++] = i;
	    }
	  if (interaction_is_subset (cmd->interactions [k], x))
	    {
	      assert (n_dropped_submodel < covariance_dim (cov));
	      submodel_dropped[n_dropped_submodel++] = i;
	    }
	}
      model_cov = 
	gsl_matrix_alloc (cm->size1 - n_dropped_model, cm->size2 - n_dropped_model);
      gsl_matrix_set (model_cov, 0, 0, gsl_matrix_get (cm, 0, 0));
      submodel_cov = 
	gsl_matrix_calloc (cm->size1 - n_dropped_submodel, cm->size2 - n_dropped_submodel);
      fill_submatrix (cm, model_cov, model_dropped, n_dropped_model);
      fill_submatrix (cm, submodel_cov, submodel_dropped, n_dropped_submodel);

      reg_sweep (model_cov, 0);
      reg_sweep (submodel_cov, 0);
      gsl_vector_set (ssq, k + 1,
		      gsl_matrix_get (submodel_cov, 0, 0)
		      - gsl_matrix_get (model_cov, 0, 0));
      gsl_matrix_free (model_cov);
      gsl_matrix_free (submodel_cov);
    }

  free (model_dropped);
  free (submodel_dropped);
  gsl_matrix_free (cm);
}

//static  void dump_matrix (const gsl_matrix *m);

static void
run_glm (struct glm_spec *cmd, struct casereader *input,
	 const struct dataset *ds)
{
  bool warn_bad_weight = true;
  int v;
  struct taint *taint;
  struct dictionary *dict = dataset_dict (ds);
  struct casereader *reader;
  struct ccase *c;

  struct glm_workspace ws;
  struct covariance *cov;

  ws.cats = categoricals_create (cmd->interactions, cmd->n_interactions,
				 cmd->wv, cmd->exclude,
				 NULL, NULL, NULL, NULL);

  cov = covariance_2pass_create (cmd->n_dep_vars, cmd->dep_vars,
				 ws.cats, cmd->wv, cmd->exclude);


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

  for (reader = casereader_clone (input);
       (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      double weight = dict_get_case_weight (dict, c, &warn_bad_weight);

      for (v = 0; v < cmd->n_dep_vars; ++v)
	moments_pass_one (ws.totals, case_data (c, cmd->dep_vars[v])->f,
			  weight);

      covariance_accumulate_pass1 (cov, c);
    }
  casereader_destroy (reader);

  for (reader = input;
       (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      double weight = dict_get_case_weight (dict, c, &warn_bad_weight);

      for (v = 0; v < cmd->n_dep_vars; ++v)
	moments_pass_two (ws.totals, case_data (c, cmd->dep_vars[v])->f,
			  weight);

      covariance_accumulate_pass2 (cov, c);
    }
  casereader_destroy (reader);

  {
    gsl_matrix *cm = covariance_calculate_unnormalized (cov);

    //    dump_matrix (cm);

    ws.total_ssq = gsl_matrix_get (cm, 0, 0);

    reg_sweep (cm, 0);

    /*
      Store the overall SSE.
    */
    ws.ssq = gsl_vector_alloc (cm->size1);
    gsl_vector_set (ws.ssq, 0, gsl_matrix_get (cm, 0, 0));
    get_ssq (cov, ws.ssq, cmd);
    //    dump_matrix (cm);

    gsl_matrix_free (cm);
  }

  if (!taint_has_tainted_successor (taint))
    output_glm (cmd, &ws);

  gsl_vector_free (ws.ssq);

  covariance_destroy (cov);
  moments_destroy (ws.totals);

  taint_destroy (taint);
}

static void
output_glm (const struct glm_spec *cmd, const struct glm_workspace *ws)
{
  const struct fmt_spec *wfmt =
    cmd->wv ? var_get_print_format (cmd->wv) : &F_8_0;

  double n_total, mean;
  double df_corr = 0.0;
  double mse = 0;

  int f;
  int r;
  const int heading_columns = 1;
  const int heading_rows = 1;
  struct tab_table *t;

  const int nc = 6;
  int nr = heading_rows + 4 + cmd->n_interactions;
  if (cmd->intercept)
    nr++;

  t = tab_create (nc, nr);
  tab_title (t, _("Tests of Between-Subjects Effects"));

  tab_headers (t, heading_columns, 0, heading_rows, 0);

  tab_box (t, TAL_2, TAL_2, -1, TAL_1, 0, 0, nc - 1, nr - 1);

  tab_hline (t, TAL_2, 0, nc - 1, heading_rows);
  tab_vline (t, TAL_2, heading_columns, 0, nr - 1);

  tab_text (t, 0, 0, TAB_CENTER | TAT_TITLE, _("Source"));

  /* TRANSLATORS: The parameter is a roman numeral */
  tab_text_format (t, 1, 0, TAB_CENTER | TAT_TITLE,
		   _("Type %s Sum of Squares"), "III");
  tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("df"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("Mean Square"));
  tab_text (t, 4, 0, TAB_CENTER | TAT_TITLE, _("F"));
  tab_text (t, 5, 0, TAB_CENTER | TAT_TITLE, _("Sig."));

  moments_calculate (ws->totals, &n_total, &mean, NULL, NULL, NULL);

  if (cmd->intercept)
    df_corr += 1.0;

  df_corr += categoricals_df_total (ws->cats);

  mse = gsl_vector_get (ws->ssq, 0) / (n_total - df_corr);

  r = heading_rows;
  tab_text (t, 0, r, TAB_LEFT | TAT_TITLE, _("Corrected Model"));

  r++;

  if (cmd->intercept)
    {
      const double intercept = pow2 (mean * n_total) / n_total;
      const double df = 1.0;
      const double F = intercept / df / mse;
      tab_text (t, 0, r, TAB_LEFT | TAT_TITLE, _("Intercept"));
      tab_double (t, 1, r, 0, intercept, NULL);
      tab_double (t, 2, r, 0, 1.00, wfmt);
      tab_double (t, 3, r, 0, intercept / df, NULL);
      tab_double (t, 4, r, 0, F, NULL);
      tab_double (t, 5, r, 0, gsl_cdf_fdist_Q (F, df, n_total - df_corr),
		  NULL);
      r++;
    }

  for (f = 0; f < cmd->n_interactions; ++f)
    {
      struct string str = DS_EMPTY_INITIALIZER;
      const double df = categoricals_df (ws->cats, f);
      const double ssq = gsl_vector_get (ws->ssq, f + 1);
      const double F = ssq / df / mse;
      interaction_to_string (cmd->interactions[f], &str);
      tab_text (t, 0, r, TAB_LEFT | TAT_TITLE, ds_cstr (&str));
      ds_destroy (&str);

      tab_double (t, 1, r, 0, ssq, NULL);
      tab_double (t, 2, r, 0, df, wfmt);
      tab_double (t, 3, r, 0, ssq / df, NULL);
      tab_double (t, 4, r, 0, F, NULL);

      tab_double (t, 5, r, 0, gsl_cdf_fdist_Q (F, df, n_total - df_corr),
		  NULL);
      r++;
    }

  {
    /* Corrected Model */
    const double df = df_corr - 1.0;
    const double ssq = ws->total_ssq - gsl_vector_get (ws->ssq, 0);
    const double F = ssq / df / mse;
    tab_double (t, 1, heading_rows, 0, ssq, NULL);
    tab_double (t, 2, heading_rows, 0, df, wfmt);
    tab_double (t, 3, heading_rows, 0, ssq / df, NULL);
    tab_double (t, 4, heading_rows, 0, F, NULL);

    tab_double (t, 5, heading_rows, 0,
		gsl_cdf_fdist_Q (F, df, n_total - df_corr), NULL);
  }

  {
    const double df = n_total - df_corr;
    const double ssq = gsl_vector_get (ws->ssq, 0);
    const double mse = ssq / df;
    tab_text (t, 0, r, TAB_LEFT | TAT_TITLE, _("Error"));
    tab_double (t, 1, r, 0, ssq, NULL);
    tab_double (t, 2, r, 0, df, wfmt);
    tab_double (t, 3, r++, 0, mse, NULL);
  }

  if (cmd->intercept)
    {
      const double intercept = pow2 (mean * n_total) / n_total;
      const double ssq = intercept + ws->total_ssq;

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

#if 0
static void
dump_matrix (const gsl_matrix * m)
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
#endif




/* Match a variable.
   If the match succeeds, the variable will be placed in VAR.
   Returns true if successful */
static bool
lex_match_variable (struct lexer *lexer, const struct glm_spec *glm, const struct variable **var)
{
  if (lex_token (lexer) !=  T_ID)
    return false;

  *var = parse_variable_const  (lexer, glm->dict);

  if ( *var == NULL)
    return false;
  return true;
}

/* An interaction is a variable followed by {*, BY} followed by an interaction */
static bool
parse_design_interaction (struct lexer *lexer, struct glm_spec *glm, struct interaction **iact)
{
  const struct variable *v = NULL;
  assert (iact);

  switch  (lex_next_token (lexer, 1))
    {
    case T_ENDCMD:
    case T_SLASH:
    case T_COMMA:
    case T_ID:
    case T_BY:
    case T_ASTERISK:
      break;
    default:
      return false;
      break;
    }

  if (! lex_match_variable (lexer, glm, &v))
    {
      interaction_destroy (*iact);
      *iact = NULL;
      return false;
    }
  
  assert (v);

  if ( *iact == NULL)
    *iact = interaction_create (v);
  else
    interaction_add_variable (*iact, v);

  if ( lex_match (lexer, T_ASTERISK) || lex_match (lexer, T_BY))
    {
      return parse_design_interaction (lexer, glm, iact);
    }

  return true;
}

static bool
parse_nested_variable (struct lexer *lexer, struct glm_spec *glm)
{
  const struct variable *v = NULL;
  if ( ! lex_match_variable (lexer, glm, &v))
    return false;

  if (lex_match (lexer, T_LPAREN))
    {
      if ( ! parse_nested_variable (lexer, glm))
	return false;

      if ( ! lex_force_match (lexer, T_RPAREN))
	return false;
    }

  lex_error (lexer, "Nested variables are not yet implemented"); return false;  
  return true;
}

/* A design term is an interaction OR a nested variable */
static bool
parse_design_term (struct lexer *lexer, struct glm_spec *glm)
{
  struct interaction *iact = NULL;
  if (parse_design_interaction (lexer, glm, &iact))
    {
      /* Interaction parsing successful.  Add to list of interactions */
      glm->interactions = xrealloc (glm->interactions, sizeof *glm->interactions * ++glm->n_interactions);
      glm->interactions[glm->n_interactions - 1] = iact;
      return true;
    }

  if ( parse_nested_variable (lexer, glm))
    return true;

  return false;
}



/* Parse a complete DESIGN specification.
   A design spec is a design term, optionally followed by a comma,
   and another design spec.
*/
static bool
parse_design_spec (struct lexer *lexer, struct glm_spec *glm)
{
  if  (lex_token (lexer) == T_ENDCMD || lex_token (lexer) == T_SLASH)
    return true;

  if ( ! parse_design_term (lexer, glm))
    return false;

  lex_match (lexer, T_COMMA);

  return parse_design_spec (lexer, glm);
}

