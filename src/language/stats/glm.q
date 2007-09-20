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
#include <libpspp/alloc.h>
#include <libpspp/compiler.h>
#include <libpspp/message.h>
#include <math/design-matrix.h>
#include <math/coefficient.h>
#include <math/linreg/linreg.h>
#include <math/moments.h>
#include <output/table.h>

#include "gettext.h"

#define GLM_LARGE_DATA 1000

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

static bool run_glm (struct casereader*,
		     struct cmd_glm *,
		     const struct dataset *,
		     pspp_linreg_cache *);

int
cmd_glm (struct lexer *lexer, struct dataset *ds)
{
  struct casegrouper *grouper;
  struct casereader *group;
  pspp_linreg_cache *model = NULL;

  bool ok;

  model = xmalloc (sizeof *model);

  if (!parse_glm (lexer, ds, &cmd, NULL))
    return CMD_FAILURE;

  /* Data pass. */
  grouper = casegrouper_create_splits (proc_open (ds), dataset_dict (ds));
  while (casegrouper_get_next_group (grouper, &group))
    {
      run_glm (group, &cmd, ds, model);
    }
  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  free (model);
  free (v_dependent);
  return ok ? CMD_SUCCESS : CMD_FAILURE;
}

/* Parser for the dependent sub command */
static int
glm_custom_dependent (struct lexer *lexer, struct dataset *ds,
		      struct cmd_glm *cmd UNUSED,
		      void *aux UNUSED)
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
  n_dependent = 1; /* Drop this line after adding support for multivariate GLM. */

  return 1;
}

static void
coeff_init (pspp_linreg_cache * c, struct design_matrix *dm)
{
  c->coeff = xnmalloc (dm->m->size2 + 1, sizeof (*c->coeff));
  c->coeff[0] = xmalloc (sizeof (*(c->coeff[0])));	/* The first coefficient is the intercept. */
  c->coeff[0]->v_info = NULL;	/* Intercept has no associated variable. */
  pspp_coeff_init (c->coeff + 1, dm);
}

/*
  Put the moments in the linreg cache.
 */
static void
compute_moments (pspp_linreg_cache * c, struct moments_var *mom,
		 struct design_matrix *dm, size_t n)
{
  size_t i;
  size_t j;
  double weight;
  double mean;
  double variance;
  double skewness;
  double kurtosis;
  /*
     Scan the variable names in the columns of the design matrix.
     When we find the variable we need, insert its mean in the cache.
   */
  for (i = 0; i < dm->m->size2; i++)
    {
      for (j = 0; j < n; j++)
	{
	  if (design_matrix_col_to_var (dm, i) == (mom + j)->v)
	    {
	      moments1_calculate ((mom + j)->m, &weight, &mean, &variance,
				  &skewness, &kurtosis);
	      gsl_vector_set (c->indep_means, i, mean);
	      gsl_vector_set (c->indep_std, i, sqrt (variance));
	    }
	}
    }
}
/* Encode categorical variables.
   Returns number of valid cases. */
static int
prepare_categories (struct casereader *input,
                    const struct variable **vars, size_t n_vars,
                    struct moments_var *mom)
{
  int n_data;
  struct ccase c;
  size_t i;

  for (i = 0; i < n_vars; i++)
    if (var_is_alpha (vars[i]))
      cat_stored_values_create (vars[i]);

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
            moments1_add (mom[i].m, val->f, 1.0);
        }
      n_data++;
   }
  casereader_destroy (input);

  return n_data;
}

static bool
run_glm (struct casereader *input,
	 struct cmd_glm *cmd,
	 const struct dataset *ds,
	 pspp_linreg_cache *model)
{
  size_t i;
  int n_indep = 0;
  struct ccase c;
  const struct variable **indep_vars;
  struct design_matrix *X;
  struct moments_var *mom;
  gsl_vector *Y;
  struct casereader *reader;
  casenumber row;
  size_t n_data;		/* Number of valid cases. */

  pspp_linreg_opts lopts;

  assert (model != NULL);

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

  for (i = 0; i < n_dependent; i++)
    {
      if (!var_is_numeric (v_dependent[i]))
	{
	  msg (SE, _("Dependent variable must be numeric."));
	  return false;
	}
    }

  mom = xnmalloc (n_dependent, sizeof (*mom));
  mom->m = moments1_create (MOMENT_VARIANCE);
  mom->v = v_dependent[0];
  lopts.get_depvar_mean_std = 1;

  lopts.get_indep_mean_std = xnmalloc (n_dependent, sizeof (int));
  indep_vars = xnmalloc (cmd->n_by, sizeof *indep_vars);

  for (i = 0; i < cmd->n_by; i++)
    {
      indep_vars[i] = cmd->v_by[i];
    }
  n_indep = cmd->n_by;
  
  reader = casereader_clone (input);
  reader = casereader_create_filter_missing (reader, indep_vars, n_indep,
					     MV_ANY, NULL);
  reader = casereader_create_filter_missing (reader, v_dependent, 1,
					     MV_ANY, NULL);
  n_data = prepare_categories (casereader_clone (reader),
			       indep_vars, n_indep, mom);

  if ((n_data > 0) && (n_indep > 0))
    {
      Y = gsl_vector_alloc (n_data);
      X =
	design_matrix_create (n_indep,
			      (const struct variable **) indep_vars,
			      n_data);
      for (i = 0; i < X->m->size2; i++)
	{
	  lopts.get_indep_mean_std[i] = 1;
	}
      model = pspp_linreg_cache_alloc (X->m->size1, X->m->size2);
      model->indep_means = gsl_vector_alloc (X->m->size2);
      model->indep_std = gsl_vector_alloc (X->m->size2);
      model->depvar = v_dependent[0];
      reader = casereader_create_counter (reader, &row, -1);
      for (; casereader_read (reader, &c); case_destroy (&c))
	{
	  for (i = 0; i < n_indep; ++i)
	    {
	      const struct variable *v = indep_vars[i];
	      const union value *val = case_data (&c, v);
	      if (var_is_alpha (v))
		design_matrix_set_categorical (X, row, v, val);
	      else
		design_matrix_set_numeric (X, row, v, val);
	    }
          gsl_vector_set (Y, row, case_num (&c, model->depvar));
	}
      casereader_destroy (reader);
      /*
	Now that we know the number of coefficients, allocate space
	and store pointers to the variables that correspond to the
	coefficients.
      */
      coeff_init (model, X);
      
      /*
	Find the least-squares estimates and other statistics.
      */
      pspp_linreg ((const gsl_vector *) Y, X->m, &lopts, model);
      compute_moments (model, mom, X, n_dependent);
      
      gsl_vector_free (Y);
      design_matrix_destroy (X);
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
