/* PSPP - a program for statistical analysis.
   Copyright (C) 2007 Free Software Foundation, Inc.

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
#include <data/category.h>
#include <data/casegrouper.h>
#include <data/casereader.h>
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
#include <libpspp/hash.h>
#include <libpspp/message.h>
#include <math/covariance-matrix.h>
#include <math/coefficient.h>
#include <math/linreg.h>
#include <math/moments.h>
#include <output/table.h>

#include "xalloc.h"
#include "gettext.h"

/* (headers) */

/* (specification)
   "GLM" (glm_):
   *dependent=custom;
   by=varlist;
   with=varlist.
*/
/* (declarations) */
/* (functions) */
static struct cmd_glm cmd;

/*
  Moments for each of the variables used.
 */
struct moments_var
{
  struct moments1 *m;
  double *weight;
  double *mean;
  double *variance;
  const struct variable *v;
};


/*
  Dependent variable used.
 */
static const struct variable **v_dependent;

/*
  Number of dependent variables.
 */
static size_t n_dependent;

#if 0
/*
  Return value for the procedure.
 */
static int pspp_glm_rc = CMD_SUCCESS;
#else
int cmd_glm (struct lexer *lexer, struct dataset *ds);
#endif

static bool run_glm (struct casereader *,
		     struct cmd_glm *,
		     const struct dataset *);

int
cmd_glm (struct lexer *lexer, struct dataset *ds)
{
  struct casegrouper *grouper;
  struct casereader *group;

  bool ok;

  if (!parse_glm (lexer, ds, &cmd, NULL))
    return CMD_FAILURE;

  /* Data pass. */
  grouper = casegrouper_create_splits (proc_open (ds), dataset_dict (ds));
  while (casegrouper_get_next_group (grouper, &group))
    {
      run_glm (group, &cmd, ds);
    }
  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  free (v_dependent);
  return ok ? CMD_SUCCESS : CMD_FAILURE;
}

/* Parser for the dependent sub command */
static int
glm_custom_dependent (struct lexer *lexer, struct dataset *ds,
		      struct cmd_glm *cmd UNUSED, void *aux UNUSED)
{
  const struct dictionary *dict = dataset_dict (ds);

  if ((lex_token (lexer) != T_ID
       || dict_lookup_var (dict, lex_tokid (lexer)) == NULL)
      && lex_token (lexer) != T_ALL)
    return 2;

  if (!parse_variables_const
      (lexer, dict, &v_dependent, &n_dependent, PV_NONE))
    {
      free (v_dependent);
      return 0;
    }
  assert (n_dependent);
  if (n_dependent > 1)
    msg (SE, _("Multivariate GLM not yet supported"));
  n_dependent = 1;		/* Drop this line after adding support for multivariate GLM. */

  return 1;
}

/*
  COV is the covariance matrix for variables included in the
  model. That means the dependent variable is in there, too.
 */
static void
coeff_init (pspp_linreg_cache * c, const struct design_matrix *cov)
{
  c->coeff = xnmalloc (cov->m->size2, sizeof (*c->coeff));
  c->n_coeffs = cov->m->size2 - 1;
  pspp_coeff_init (c->coeff, cov);
}

/* Encode categorical variables.
   Returns number of valid cases. */
static int
data_pass_one (struct casereader *input,
	       const struct variable **vars, size_t n_vars,
	       struct moments_var **mom)
{
  int n_data;
  struct ccase c;
  size_t i;

  for (i = 0; i < n_vars; i++)
    {
      mom[i] = xmalloc (sizeof (*mom[i]));
      mom[i]->v = vars[i];
      mom[i]->mean = xmalloc (sizeof (*mom[i]->mean));
      mom[i]->variance = xmalloc (sizeof (*mom[i]->mean));
      mom[i]->weight = xmalloc (sizeof (*mom[i]->weight));
      mom[i]->m = moments1_create (MOMENT_VARIANCE);
      if (var_is_alpha (vars[i]))
	cat_stored_values_create (vars[i]);
    }

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
	    moments1_add (mom[i]->m, val->f, 1.0);
	}
      n_data++;
    }
  casereader_destroy (input);
  for (i = 0; i < n_vars; i++)
    {
      if (var_is_numeric (mom[i]->v))
	{
	  moments1_calculate (mom[i]->m, mom[i]->weight, mom[i]->mean,
			      mom[i]->variance, NULL, NULL);
	}
    }

  return n_data;
}

static pspp_linreg_cache *
fit_model (const struct design_matrix *cov, const struct moments1 **mom, 
	   const struct variable *dep_var, 
	   const struct variable ** indep_vars, 
	   size_t n_data, size_t n_indep)
{
  pspp_linreg_cache *result = NULL;
  result = pspp_linreg_cache_alloc (dep_var, indep_vars, n_data, n_indep);
  coeff_init (result, cov);
  pspp_linreg_with_cov (cov, result);  
  
  return result;
}

static bool
run_glm (struct casereader *input,
	 struct cmd_glm *cmd,
	 const struct dataset *ds)
{
  casenumber row;
  const struct variable **indep_vars;
  const struct variable **all_vars;
  int n_indep = 0;
  pspp_linreg_cache *model = NULL; 
  pspp_linreg_opts lopts;
  struct ccase c;
  size_t i;
  size_t n_all_vars;
  size_t n_data;		/* Number of valid cases. */
  struct casereader *reader;
  struct design_matrix *cov;
  struct hsh_table *cov_hash;
  struct moments1 **mom;

  if (!casereader_peek (input, 0, &c))
    {
      casereader_destroy (input);
      return true;
    }
  output_split_file_values (ds, &c);
  case_destroy (&c);

  if (!v_dependent)
    {
      dict_get_vars (dataset_dict (ds), &v_dependent, &n_dependent,
		     1u << DC_SYSTEM);
    }

  lopts.get_depvar_mean_std = 1;

  lopts.get_indep_mean_std = xnmalloc (n_dependent, sizeof (int));
  indep_vars = xnmalloc (cmd->n_by, sizeof *indep_vars);
  n_all_vars = cmd->n_by + n_dependent;
  all_vars = xnmalloc (n_all_vars, sizeof *all_vars);

  for (i = 0; i < n_dependent; i++)
    {
      all_vars[i] = v_dependent[i];
    }
  for (i = 0; i < cmd->n_by; i++)
    {
      indep_vars[i] = cmd->v_by[i];
      all_vars[i + n_dependent] = cmd->v_by[i];
    }
  n_indep = cmd->n_by;
  mom = xnmalloc (n_all_vars, sizeof (*mom));
  for (i = 0; i < n_all_vars; i++)
    mom[i] = moments1_create (MOMENT_MEAN);

  reader = casereader_clone (input);
  reader = casereader_create_filter_missing (reader, indep_vars, n_indep,
					     MV_ANY, NULL, NULL);
  reader = casereader_create_filter_missing (reader, v_dependent, 1,
					     MV_ANY, NULL, NULL);

  if (n_indep > 0)
    {
      for (i = 0; i < n_all_vars; i++)
	if (var_is_alpha (all_vars[i]))
	  cat_stored_values_create (all_vars[i]);
      
      cov_hash = covariance_hsh_create (n_all_vars);
      reader = casereader_create_counter (reader, &row, -1);
      for (; casereader_read (reader, &c); case_destroy (&c))
	{
	  /* 
	     Accumulate the covariance matrix.
	  */
	  covariance_accumulate (cov_hash, mom, &c, all_vars, n_all_vars);
	  n_data++;
	}
      cov = covariance_accumulator_to_matrix (cov_hash, mom, all_vars, n_all_vars, n_data);

      hsh_destroy (cov_hash);
      for (i = 0; i < n_dependent; i++)
	{
	  model = fit_model (cov, mom, v_dependent[i], indep_vars, n_data, n_indep);
	  pspp_linreg_cache_free (model);
	}

      casereader_destroy (reader);
      for (i = 0; i < n_all_vars; i++)
	{
	  moments1_destroy (mom[i]);
	}
      free (mom);
      covariance_matrix_destroy (cov);
    }
  else
    {
      msg (SE, gettext ("No valid data found. This command was skipped."));
    }
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
