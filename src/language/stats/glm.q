/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009 Free Software Foundation, Inc.

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


static pspp_linreg_cache *
fit_model (const struct covariance_matrix *cov,
	   const struct variable *dep_var, 
	   const struct variable ** indep_vars, 
	   size_t n_data, size_t n_indep)
{
  pspp_linreg_cache *result = NULL;
  result = pspp_linreg_cache_alloc (dep_var, indep_vars, n_data, n_indep);
  coeff_init (result, covariance_to_design (cov));
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
  struct ccase *c;
  size_t i;
  size_t n_all_vars;
  size_t n_data;		/* Number of valid cases. */
  struct casereader *reader;
  struct covariance_matrix *cov;

  c = casereader_peek (input, 0);
  if (c == NULL)
    {
      casereader_destroy (input);
      return true;
    }
  output_split_file_values (ds, c);
  case_unref (c);

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
      
      cov = covariance_matrix_init (n_all_vars, all_vars, ONE_PASS, PAIRWISE, MV_ANY);
      reader = casereader_create_counter (reader, &row, -1);
      for (; (c = casereader_read (reader)) != NULL; case_unref (c))
	{
	  /* 
	     Accumulate the covariance matrix.
	  */
	  covariance_matrix_accumulate (cov, c, NULL, 0);
	  n_data++;
	}
      covariance_matrix_compute (cov);

      for (i = 0; i < n_dependent; i++)
	{
	  model = fit_model (cov, v_dependent[i], indep_vars, n_data, n_indep);
	  pspp_linreg_cache_free (model);
	}

      casereader_destroy (reader);
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
