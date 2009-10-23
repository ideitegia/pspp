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
#include <math/covariance.h>
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
   design=custom;
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

size_t n_inter; /* Number of interactions. */
size_t n_members; /* Number of memebr variables in an interaction. */ 

struct interaction_variable **interactions;

int cmd_glm (struct lexer *lexer, struct dataset *ds);

static bool run_glm (struct casereader *,
		     struct cmd_glm *,
		     const struct dataset *);
/*
  If the DESIGN subcommand was not specified, specify all possible
  two-way interactions.
 */
static void
check_interactions (struct dataset *ds, struct cmd_glm *cmd)
{
  size_t i;
  size_t j;
  size_t k = 0;
  struct variable **interaction_vars;

  /* 
     User did not specify the design matrix, so we 
     specify it here.
  */
  n_inter = (cmd->n_by + cmd->n_with) * (cmd->n_by + cmd->n_with - 1) / 2;
  interactions = xnmalloc (n_inter, sizeof (*interactions));
  interaction_vars = xnmalloc (2, sizeof (*interaction_vars));
  for (i = 0; i < cmd->n_by; i++)
    {
      for (j = i + 1; j < cmd->n_by; j++)
	{
	  interaction_vars[0] = cmd->v_by[i];
	  interaction_vars[1] = cmd->v_by[j];
	  interactions[k] = interaction_variable_create (interaction_vars, 2);
	  k++;
	}
      for (j = 0; j < cmd->n_with; j++)
	{
	  interaction_vars[0] = cmd->v_by[i];
	  interaction_vars[1] = cmd->v_with[j];
	  interactions[k] = interaction_variable_create (interaction_vars, 2);
	  k++;
	}
    }
  for (i = 0; i < cmd->n_with; i++)
    {
      for (j = i + 1; j < cmd->n_with; j++)
	{
	  interaction_vars[0] = cmd->v_with[i];
	  interaction_vars[1] = cmd->v_with[j];
	  interactions[k] = interaction_variable_create (interaction_vars, 2);
	  k++;
	}
    }
}
int
cmd_glm (struct lexer *lexer, struct dataset *ds)
{
  struct casegrouper *grouper;
  struct casereader *group;

  bool ok;

  if (!parse_glm (lexer, ds, &cmd, NULL))
    return CMD_FAILURE;

  if (!lex_match_id (lexer, "DESIGN"))
    {
      check_interactions (ds, &cmd);
    }
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

static int
parse_interactions (struct lexer *lexer, const struct variable **interaction_vars, int n_members,
		    int max_members, struct dataset *ds)
{
  if (lex_match (lexer, '*'))
    {
      if (n_members > max_members)
	{
	  max_members *= 2;
	  xnrealloc (interaction_vars, max_members, sizeof (*interaction_vars));
	}
      interaction_vars[n_members] = parse_variable (lexer, dataset_dict (ds));
      parse_interactions (lexer, interaction_vars, n_members++, max_members, ds);
    }
  return n_members;
}
/* Parser for the design subcommand. */
static int
glm_custom_design (struct lexer *lexer, struct dataset *ds,
		   struct cmd_glm *cmd UNUSED, void *aux UNUSED)
{
  size_t n_allocated = 2;
  size_t n_members;
  struct variable **interaction_vars;
  struct variable *this_var;

  interactions = xnmalloc (n_allocated, sizeof (*interactions));
  n_inter = 1;
  while (lex_token (lexer) != T_STOP && lex_token (lexer) != '.')
    {
      this_var = parse_variable (lexer, dataset_dict (ds));
      if (lex_match (lexer, '('))
	{
	  lex_force_match (lexer, ')');
	}
      else if (lex_match (lexer, '*'))
	{
	  interaction_vars = xnmalloc (2 * n_inter, sizeof (*interaction_vars));
	  n_members = parse_interactions (lexer, interaction_vars, 1, 2 * n_inter, ds);
	  if (n_allocated < n_inter)
	    {
	      n_allocated *= 2;
	      xnrealloc (interactions, n_allocated, sizeof (*interactions));
	    }
	  interactions [n_inter - 1] = 
	    interaction_variable_create (interaction_vars, n_members);
	  n_inter++;
	  free (interaction_vars);
	}
    }
  return 1;
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
fit_model (const struct covariance *cov,
	   const struct variable *dep_var, 
	   const struct variable ** indep_vars, 
	   size_t n_data, size_t n_indep)
{
  pspp_linreg_cache *result = NULL;
  
  return result;
}

static bool
run_glm (struct casereader *input,
	 struct cmd_glm *cmd,
	 const struct dataset *ds)
{
  casenumber row;
  const struct variable **numerics = NULL;
  const struct variable **categoricals = NULL;
  int n_indep = 0;
  pspp_linreg_cache *model = NULL; 
  pspp_linreg_opts lopts;
  struct ccase *c;
  size_t i;
  size_t n_data;		/* Number of valid cases. */
  size_t n_categoricals = 0;
  size_t n_numerics;
  struct casereader *reader;
  struct covariance *cov;

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


  n_numerics = cmd->n_with + n_dependent;
  for (i = 0; i < cmd->n_by; i++)
    {
      if (var_is_alpha (cmd->v_by[i]))
	{
	  n_categoricals++;
	}
      else
	{
	  n_numerics++;
	}
    }
  numerics = xnmalloc (n_categoricals, sizeof *numerics);
  categoricals = xnmalloc (n_categoricals, sizeof (*categoricals));
  size_t k = 0;
  for (i = 0; i < cmd->n_by; i++)
    {
      if (var_is_alpha (cmd->v_by[i]))
	{
	  categoricals[k] = cmd->v_by[i];
	}
      else
	{
	  numerics[i] = cmd->v_by[i];
	  k++;
	}
    }
  for (i = 0; i < n_dependent; i++)
    {
      k++;
      numerics[k] = v_dependent[i];
    }
  for (i = 0; i < cmd->n_with; i++)
    {
      k++;
      numerics[k] = v_dependent[i];
    }

  covariance_2pass_create (n_numerics, numerics, n_categoricals, categoricals, NULL, MV_NEVER);

  reader = casereader_clone (input);
  reader = casereader_create_filter_missing (reader, numerics, n_numerics,
					     MV_ANY, NULL, NULL);
  reader = casereader_create_filter_missing (reader, categoricals, n_categoricals,
					     MV_ANY, NULL, NULL);
  struct casereader *r = casereader_clone (reader);

  reader = casereader_create_counter (reader, &row, -1);
  
  for (; (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      covariance_accumulate_pass1 (cov, c);
    }
  for (; (c = casereader_read (r)) != NULL; case_unref (c))
    {
      covariance_accumulate_pass1 (cov, c);
    }
  covariance_destroy (cov);
  casereader_destroy (reader);
  casereader_destroy (r);
  
  free (numerics);
  free (categoricals);
  free (lopts.get_indep_mean_std);
  casereader_destroy (input);

  return true;
}

/*
  Local Variables:   
  mode: c
  End:
*/
