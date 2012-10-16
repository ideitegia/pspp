/* pspp - a program for statistical analysis.
   Copyright (C) 2012 Free Software Foundation, Inc.

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


/* 
   References: 
   1. "Coding Logistic Regression with Newton-Raphson", James McCaffrey
   http://msdn.microsoft.com/en-us/magazine/jj618304.aspx

   2. "SPSS Statistical Algorithms" Chapter LOGISTIC REGRESSION Algorithms


   The Newton Raphson method finds successive approximations to $\bf b$ where 
   approximation ${\bf b}_t$ is (hopefully) better than the previous ${\bf b}_{t-1}$.

   $ {\bf b}_t = {\bf b}_{t -1} + ({\bf X}^T{\bf W}_{t-1}{\bf X})^{-1}{\bf X}^T({\bf y} - {\bf \pi}_{t-1})$
   where:

   $\bf X$ is the $n \times p$ design matrix, $n$ being the number of cases, 
   $p$ the number of parameters, \par
   $\bf W$ is the diagonal matrix whose diagonal elements are
   $\hat{\pi}_0(1 - \hat{\pi}_0), \, \hat{\pi}_1(1 - \hat{\pi}_2)\dots \hat{\pi}_{n-1}(1 - \hat{\pi}_{n-1})$
   \par

*/

#include <config.h>

#include <gsl/gsl_blas.h> 

#include <gsl/gsl_linalg.h>
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>
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
#include "libpspp/assertion.h"
#include "libpspp/ll.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "math/categoricals.h"
#include "output/tab.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)




#define   PRINT_EACH_STEP  0x01
#define   PRINT_SUMMARY    0x02
#define   PRINT_CORR       0x04
#define   PRINT_ITER       0x08
#define   PRINT_GOODFIT    0x10
#define   PRINT_CI         0x20


#define PRINT_DEFAULT (PRINT_SUMMARY | PRINT_EACH_STEP)

/*
  The constant parameters of the procedure.
  That is, those which are set by the user.
*/
struct lr_spec
{
  /* The dependent variable */
  const struct variable *dep_var;

  size_t n_predictor_vars;
  const struct variable **predictor_vars;

  /* Which classes of missing vars are to be excluded */
  enum mv_class exclude;

  /* The weight variable */
  const struct variable *wv;

  const struct dictionary *dict;

  /* True iff the constant (intercept) is to be included in the model */
  bool constant;

  /* Ths maximum number of iterations */
  int max_iter;

  /* Other iteration limiting conditions */
  double bcon;
  double min_epsilon;
  double lcon;

  /* The confidence interval (in percent) */
  int confidence;

  /* What results should be presented */
  unsigned int print;

  double cut_point;
};

/* The results and intermediate result of the procedure.
   These are mutated as the procedure runs. Used for
   temporary variables etc.
*/
struct lr_result
{
  bool warn_bad_weight;


  /* The two values of the dependent variable. */
  union value y0;
  union value y1;


  /* The sum of caseweights */
  double cc;

  casenumber n_missing;
  casenumber n_nonmissing;
};


/*
  Convert INPUT into a dichotomous scalar.  For simple cases, this is a 1:1 mapping
  The return value is always either 0 or 1
*/
static double
map_dependent_var (const struct lr_spec *cmd, const struct lr_result *res, const union value *input)
{
  int width = var_get_width (cmd->dep_var);
  if (value_equal (input, &res->y0, width))
    return 0;

  if (value_equal (input, &res->y1, width))
    return 1;
	  
  NOT_REACHED ();

  return SYSMIS;
}



static void output_depvarmap (const struct lr_spec *cmd, const struct lr_result *);

static void output_variables (const struct lr_spec *cmd, 
			      const gsl_vector *,
			      const gsl_vector *);

static void output_model_summary (const struct lr_result *,
				  double initial_likelihood, double likelihood);

static void case_processing_summary (const struct lr_result *);


/*
  Return the probability estimator (that is the estimator of logit(y) )
  corresponding to the coefficient estimator beta_hat for case C
*/
static double 
pi_hat (const struct lr_spec *cmd, 
	const gsl_vector *beta_hat,
	const struct variable **x, size_t n_x,
	const struct ccase *c)
{
  int v0;
  double pi = 0;
  for (v0 = 0; v0 < n_x; ++v0)
    {
      pi += gsl_vector_get (beta_hat, v0) * 
	case_data (c, x[v0])->f;
    }

  if (cmd->constant)
    pi += gsl_vector_get (beta_hat, beta_hat->size - 1);

  pi = 1.0 / (1.0 + exp(-pi));

  return pi;
}


/*
  Calculates the Hessian matrix X' V  X,
  where: X is the n by N_X matrix comprising the n cases in INPUT
  V is a diagonal matrix { (pi_hat_0)(1 - pi_hat_0), (pi_hat_1)(1 - pi_hat_1), ... (pi_hat_{N-1})(1 - pi_hat_{N-1})} 
  (the partial derivative of the predicted values)

  If ALL predicted values derivatives are close to zero or one, then CONVERGED
  will be set to true.
*/
static gsl_matrix *
hessian (const struct lr_spec *cmd, 
	 struct lr_result *res,
	 struct casereader *input,
	 const struct variable **x, size_t n_x,
	 const gsl_vector *beta_hat,
	 bool *converged
	 )
{
  struct casereader *reader;
  struct ccase *c;
  gsl_matrix *output = gsl_matrix_calloc (beta_hat->size, beta_hat->size);

  double max_w = -DBL_MAX;

  for (reader = casereader_clone (input);
       (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      int v0, v1;
      double pi = pi_hat (cmd, beta_hat, x, n_x, c);

      double weight = dict_get_case_weight (cmd->dict, c, &res->warn_bad_weight);
      double w = pi * (1 - pi);
      if (w > max_w)
	max_w = w;
      w *= weight;

      for (v0 = 0; v0 < beta_hat->size; ++v0)
	{
	  double in0 = v0 < n_x ? case_data (c, x[v0])->f : 1.0;
	  for (v1 = 0; v1 < beta_hat->size; ++v1)
	    {
	      double in1 = v1 < n_x ? case_data (c, x[v1])->f : 1.0 ;
	      double *o = gsl_matrix_ptr (output, v0, v1);
	      *o += in0 * w * in1;
	    }
	}
    }
  casereader_destroy (reader);


  if ( max_w < cmd->min_epsilon)
    {
      *converged = true;
      msg (MN, _("All predicted values are either 1 or 0"));
    }

  return output;
}


/* Calculates the value  X' (y - pi)
   where X is the design model, 
   y is the vector of observed independent variables
   pi is the vector of estimates for y

   As a side effect, the likelihood is stored in LIKELIHOOD
*/
static gsl_vector *
xt_times_y_pi (const struct lr_spec *cmd,
	       struct lr_result *res,
	       struct casereader *input,
	       const struct variable **x, size_t n_x,
	       const struct variable *y_var,
	       const gsl_vector *beta_hat,
	       double *likelihood)
{
  struct casereader *reader;
  struct ccase *c;
  gsl_vector *output = gsl_vector_calloc (beta_hat->size);

  *likelihood = 1.0;
  for (reader = casereader_clone (input);
       (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      int v0;
      double pi = pi_hat (cmd, beta_hat, x, n_x, c);
      double weight = dict_get_case_weight (cmd->dict, c, &res->warn_bad_weight);


      double y = map_dependent_var (cmd, res, case_data (c, y_var));

      *likelihood *= pow (pi, weight * y) * pow (1 - pi, weight * (1 - y));

      for (v0 = 0; v0 < beta_hat->size; ++v0)
	{
	  double in0 = v0 < n_x ? case_data (c, x[v0])->f : 1.0;
	  double *o = gsl_vector_ptr (output, v0);
      	  *o += in0 * (y - pi) * weight;
	}
    }

  casereader_destroy (reader);

  return output;
}


/* 
   Makes an initial pass though the data, checks that the dependent variable is
   dichotomous, and calculates necessary initial values.

   Returns an initial value for \hat\beta the vector of estimators of \beta
*/
static gsl_vector * 
beta_hat_initial (const struct lr_spec *cmd, struct lr_result *res, struct casereader *input)
{
  const int width = var_get_width (cmd->dep_var);

  struct ccase *c;
  struct casereader *reader;
  gsl_vector *b0 ;
  double sum;
  double sumA = 0.0;
  double sumB = 0.0;

  bool v0set = false;
  bool v1set = false;

  size_t n_coefficients = cmd->n_predictor_vars;
  if (cmd->constant)
    n_coefficients++;

  b0 = gsl_vector_calloc (n_coefficients);

  res->cc = 0;
  for (reader = casereader_clone (input);
       (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      int v;
      bool missing = false;
      double weight = dict_get_case_weight (cmd->dict, c, &res->warn_bad_weight);
      const union value *depval = case_data (c, cmd->dep_var);

      for (v = 0; v < cmd->n_predictor_vars; ++v)
	{
	  const union value *val = case_data (c, cmd->predictor_vars[v]);
	  if (var_is_value_missing (cmd->predictor_vars[v], val, cmd->exclude))
	    {
	      missing = true;
	      break;
	    }
	}

      if (missing)
	{
	  res->n_missing++;
	  continue;
	}

      res->n_nonmissing++;

      if (!v0set)
	{
	  value_clone (&res->y0, depval, width);
	  v0set = true;
	}
      else if (!v1set)
	{
	  if ( !value_equal (&res->y0, depval, width))
	    {
	      value_clone (&res->y1, depval, width);
	      v1set = true;
	    }
	}
      else
	{
	  if (! value_equal (&res->y0, depval, width)
	      &&
	      ! value_equal (&res->y1, depval, width)
	      )
	    {
	      msg (ME, _("Dependent variable's values are not dichotomous."));
	      goto error;
	    }
	}

      if (value_equal (&res->y0, depval, width))
	  sumA += weight;

      if (value_equal (&res->y1, depval, width))
	  sumB += weight;


      res->cc += weight;
    }
  casereader_destroy (reader);

  sum = sumB;

  /* Ensure that Y0 is less than Y1.  Otherwise the mapping gets
     inverted, which is confusing to users */
  if (var_is_numeric (cmd->dep_var) && value_compare_3way (&res->y0, &res->y1, width) > 0)
    {
      union value tmp;
      value_clone (&tmp, &res->y0, width);
      value_copy (&res->y0, &res->y1, width);
      value_copy (&res->y1, &tmp, width);
      value_destroy (&tmp, width);
      sum = sumA;
    }

  if ( cmd->constant)
    {
      double mean = sum / res->cc;
      gsl_vector_set (b0, b0->size - 1, log (mean / (1 - mean)));
    }

  return b0;

 error:
  casereader_destroy (reader);
  return NULL;
}



static bool
run_lr (const struct lr_spec *cmd, struct casereader *input,
	const struct dataset *ds UNUSED)
{
  int i,j;

  gsl_vector *beta_hat;
  gsl_vector *se ;

  bool converged = false;
  double likelihood;
  double prev_likelihood = -1;
  double initial_likelihood ;

  struct lr_result work;
  work.n_missing = 0;
  work.n_nonmissing = 0;
  work.warn_bad_weight = true;


  /* Get the initial estimates of \beta and their standard errors */
  beta_hat = beta_hat_initial (cmd, &work, input);
  if (NULL == beta_hat)
    return false;

  output_depvarmap (cmd, &work);

  se = gsl_vector_alloc (beta_hat->size);

  case_processing_summary (&work);


  input = casereader_create_filter_missing (input,
					    cmd->predictor_vars,
					    cmd->n_predictor_vars,
					    cmd->exclude,
					    NULL,
					    NULL);


  /* Start the Newton Raphson iteration process... */
  for( i = 0 ; i < cmd->max_iter ; ++i)
    {
      double min, max;
      gsl_matrix *m ;
      gsl_vector *v ;

      m = hessian (cmd, &work, input,
		   cmd->predictor_vars, cmd->n_predictor_vars,
		   beta_hat,
		   &converged);

      gsl_linalg_cholesky_decomp (m);
      gsl_linalg_cholesky_invert (m);

      v = xt_times_y_pi (cmd, &work, input,
			 cmd->predictor_vars, cmd->n_predictor_vars,
			 cmd->dep_var,
			 beta_hat,
			 &likelihood);

      {
	/* delta = M.v */
	gsl_vector *delta = gsl_vector_alloc (v->size);
	gsl_blas_dgemv (CblasNoTrans, 1.0, m, v, 0, delta);
	gsl_vector_free (v);

	for (j = 0; j < se->size; ++j)
	  {
	    double *ptr = gsl_vector_ptr (se, j);
	    *ptr = gsl_matrix_get (m, j, j);
	  }

	gsl_matrix_free (m);

	gsl_vector_add (beta_hat, delta);

	gsl_vector_minmax (delta, &min, &max);

	if ( fabs (min) < cmd->bcon && fabs (max) < cmd->bcon)
	  {
	    msg (MN, _("Estimation terminated at iteration number %d because parameter estimates changed by less than %g"),
		 i + 1, cmd->bcon);
	    converged = true;
	  }

	gsl_vector_free (delta);
      }

      if ( prev_likelihood >= 0)
	{
	  if (-log (likelihood) > -(1.0 - cmd->lcon) * log (prev_likelihood))
	    {
	      msg (MN, _("Estimation terminated at iteration number %d because Log Likelihood decreased by less than %g%%"), i + 1, 100 * cmd->lcon);
	      converged = true;
	    }
	}
      if (i == 0)
	initial_likelihood = likelihood;
      prev_likelihood = likelihood;

      if (converged)
	break;
    }
  casereader_destroy (input);

  for (i = 0; i < se->size; ++i)
    {
      double *ptr = gsl_vector_ptr (se, i);
      *ptr = sqrt (*ptr);
    }

  output_model_summary (&work, initial_likelihood, likelihood);
  output_variables (cmd, beta_hat, se);

  gsl_vector_free (beta_hat); 
  gsl_vector_free (se);

  return true;
}

/* Parse the LOGISTIC REGRESSION command syntax */
int
cmd_logistic (struct lexer *lexer, struct dataset *ds)
{
  struct lr_spec lr;
  lr.dict = dataset_dict (ds);
  lr.n_predictor_vars = 0;
  lr.predictor_vars = NULL;
  lr.exclude = MV_ANY;
  lr.wv = dict_get_weight (lr.dict);
  lr.max_iter = 20;
  lr.lcon = 0.0000;
  lr.bcon = 0.001;
  lr.min_epsilon = 0.00000001;
  lr.cut_point = 0.5;
  lr.constant = true;
  lr.confidence = 95;
  lr.print = PRINT_DEFAULT;


  if (lex_match_id (lexer, "VARIABLES"))
    lex_match (lexer, T_EQUALS);

  if (! (lr.dep_var = parse_variable_const (lexer, lr.dict)))
    goto error;

  lex_force_match (lexer, T_WITH);

  if (!parse_variables_const (lexer, lr.dict,
			      &lr.predictor_vars, &lr.n_predictor_vars,
			      PV_NO_DUPLICATE | PV_NUMERIC))
    goto error;


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
		  lr.exclude = MV_SYSTEM;
		}
	      else if (lex_match_id (lexer, "EXCLUDE"))
		{
		  lr.exclude = MV_ANY;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "ORIGIN"))
	{
	  lr.constant = false;
	}
      else if (lex_match_id (lexer, "NOORIGIN"))
	{
	  lr.constant = true;
	}
      else if (lex_match_id (lexer, "NOCONST"))
	{
	  lr.constant = false;
	}
      else if (lex_match_id (lexer, "EXTERNAL"))
	{
	  /* This is for compatibility.  It does nothing */
	}
      else if (lex_match_id (lexer, "PRINT"))
	{
	  lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "DEFAULT"))
		{
		  lr.print |= PRINT_DEFAULT;
		}
	      else if (lex_match_id (lexer, "SUMMARY"))
		{
		  lr.print |= PRINT_SUMMARY;
		}
#if 0
	      else if (lex_match_id (lexer, "CORR"))
		{
		  lr.print |= PRINT_CORR;
		}
	      else if (lex_match_id (lexer, "ITER"))
		{
		  lr.print |= PRINT_ITER;
		}
	      else if (lex_match_id (lexer, "GOODFIT"))
		{
		  lr.print |= PRINT_GOODFIT;
		}
#endif
	      else if (lex_match_id (lexer, "CI"))
		{
		  lr.print |= PRINT_CI;
		  if (lex_force_match (lexer, T_LPAREN))
		    {
		      if (! lex_force_int (lexer))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		      lr.confidence = lex_integer (lexer);
		      lex_get (lexer);
		      if ( ! lex_force_match (lexer, T_RPAREN))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		    }
		}
	      else if (lex_match_id (lexer, "ALL"))
		{
		  lr.print = ~0x0000;
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
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "BCON"))
		{
		  if (lex_force_match (lexer, T_LPAREN))
		    {
		      if (! lex_force_num (lexer))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		      lr.bcon = lex_number (lexer);
		      lex_get (lexer);
		      if ( ! lex_force_match (lexer, T_RPAREN))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		    }
		}
	      else if (lex_match_id (lexer, "ITERATE"))
		{
		  if (lex_force_match (lexer, T_LPAREN))
		    {
		      if (! lex_force_int (lexer))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		      lr.max_iter = lex_integer (lexer);
		      lex_get (lexer);
		      if ( ! lex_force_match (lexer, T_RPAREN))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		    }
		}
	      else if (lex_match_id (lexer, "LCON"))
		{
		  if (lex_force_match (lexer, T_LPAREN))
		    {
		      if (! lex_force_num (lexer))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		      lr.lcon = lex_number (lexer);
		      lex_get (lexer);
		      if ( ! lex_force_match (lexer, T_RPAREN))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		    }
		}
	      else if (lex_match_id (lexer, "EPS"))
		{
		  if (lex_force_match (lexer, T_LPAREN))
		    {
		      if (! lex_force_num (lexer))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		      lr.min_epsilon = lex_number (lexer);
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

    grouper = casegrouper_create_splits (proc_open (ds), lr.dict);
    while (casegrouper_get_next_group (grouper, &group))
      ok = run_lr (&lr, group, ds);
    ok = casegrouper_destroy (grouper);
    ok = proc_commit (ds) && ok;
  }

  free (lr.predictor_vars);
  return CMD_SUCCESS;

 error:

  free (lr.predictor_vars);
  return CMD_FAILURE;
}




/* Show the Dependent Variable Encoding box.
   This indicates how the dependent variable
   is mapped to the internal zero/one values.
*/
static void
output_depvarmap (const struct lr_spec *cmd, const struct lr_result *res)
{
  const int heading_columns = 0;
  const int heading_rows = 1;
  struct tab_table *t;
  struct string str;

  const int nc = 2;
  int nr = heading_rows + 2;

  t = tab_create (nc, nr);
  tab_title (t, _("Dependent Variable Encoding"));

  tab_headers (t, heading_columns, 0, heading_rows, 0);

  tab_box (t, TAL_2, TAL_2, -1, TAL_1, 0, 0, nc - 1, nr - 1);

  tab_hline (t, TAL_2, 0, nc - 1, heading_rows);
  tab_vline (t, TAL_2, heading_columns, 0, nr - 1);

  tab_text (t,  0, 0, TAB_CENTER | TAT_TITLE, _("Original Value"));
  tab_text (t,  1, 0, TAB_CENTER | TAT_TITLE, _("Internal Value"));



  ds_init_empty (&str);
  var_append_value_name (cmd->dep_var, &res->y0, &str);
  tab_text (t,  0, 0 + heading_rows,  0, ds_cstr (&str));

  ds_clear (&str);
  var_append_value_name (cmd->dep_var, &res->y1, &str);
  tab_text (t,  0, 1 + heading_rows,  0, ds_cstr (&str));


  tab_double (t, 1, 0 + heading_rows, 0, map_dependent_var (cmd, res, &res->y0), &F_8_0);
  tab_double (t, 1, 1 + heading_rows, 0, map_dependent_var (cmd, res, &res->y1), &F_8_0);
  ds_destroy (&str);

  tab_submit (t);
}


/* Show the Variables in the Equation box */
static void
output_variables (const struct lr_spec *cmd, 
		  const gsl_vector *beta, 
		  const gsl_vector *se)
{
  int row = 0;
  const int heading_columns = 1;
  int heading_rows = 1;
  struct tab_table *t;

  int idx;
  int n_rows = cmd->n_predictor_vars;

  int nc = 8;
  int nr ;
  if (cmd->print & PRINT_CI)
    {
      nc += 2;
      heading_rows += 1;
      row++;
    }
  nr = heading_rows + cmd->n_predictor_vars;
  if (cmd->constant)
    nr++;

  t = tab_create (nc, nr);
  tab_title (t, _("Variables in the Equation"));

  tab_headers (t, heading_columns, 0, heading_rows, 0);

  tab_box (t, TAL_2, TAL_2, -1, TAL_1, 0, 0, nc - 1, nr - 1);

  tab_hline (t, TAL_2, 0, nc - 1, heading_rows);
  tab_vline (t, TAL_2, heading_columns, 0, nr - 1);

  tab_text (t,  0, row + 1, TAB_CENTER | TAT_TITLE, _("Step 1"));

  tab_text (t,  2, row, TAB_CENTER | TAT_TITLE, _("B"));
  tab_text (t,  3, row, TAB_CENTER | TAT_TITLE, _("S.E."));
  tab_text (t,  4, row, TAB_CENTER | TAT_TITLE, _("Wald"));
  tab_text (t,  5, row, TAB_CENTER | TAT_TITLE, _("df"));
  tab_text (t,  6, row, TAB_CENTER | TAT_TITLE, _("Sig."));
  tab_text (t,  7, row, TAB_CENTER | TAT_TITLE, _("Exp(B)"));

  if (cmd->print & PRINT_CI)
    {
      tab_joint_text_format (t, 8, 0, 9, 0,
			     TAB_CENTER | TAT_TITLE, _("%d%% CI for Exp(B)"), cmd->confidence);

      tab_text (t,  8, row, TAB_CENTER | TAT_TITLE, _("Lower"));
      tab_text (t,  9, row, TAB_CENTER | TAT_TITLE, _("Upper"));
    }
 
  if (cmd->constant)
    n_rows++;

  for (idx = 0 ; idx < n_rows; ++idx)
    {
      const int r = idx + heading_rows;

      const double b = gsl_vector_get (beta, idx);
      const double sigma = gsl_vector_get (se, idx);
      const double wald = pow2 (b / sigma);
      const double df = 1;

      if (idx < cmd->n_predictor_vars)
	tab_text (t, 1, r, TAB_LEFT | TAT_TITLE, 
		  var_to_string (cmd->predictor_vars[idx]));

      tab_double (t, 2, r, 0, b, 0);
      tab_double (t, 3, r, 0, sigma, 0);
      tab_double (t, 4, r, 0, wald, 0);
      tab_double (t, 5, r, 0, df, &F_8_0);
      tab_double (t, 6, r, 0,  gsl_cdf_chisq_Q (wald, df), 0);
      tab_double (t, 7, r, 0, exp (b), 0);

      if (cmd->print & PRINT_CI)
	{
	  double wc = gsl_cdf_ugaussian_Pinv (0.5 + cmd->confidence / 200.0);
	  wc *= sigma;

	  if (idx < cmd->n_predictor_vars)
	    {
	      tab_double (t, 8, r, 0, exp (b - wc), 0);
	      tab_double (t, 9, r, 0, exp (b + wc), 0);
	    }
	}
    }

  if ( cmd->constant)
    tab_text (t, 1, nr - 1, TAB_LEFT | TAT_TITLE, _("Constant"));

  tab_submit (t);
}


/* Show the model summary box */
static void
output_model_summary (const struct lr_result *res,
		      double initial_likelihood, double likelihood)
{
  const int heading_columns = 0;
  const int heading_rows = 1;
  struct tab_table *t;

  const int nc = 4;
  int nr = heading_rows + 1;
  double cox;

  t = tab_create (nc, nr);
  tab_title (t, _("Model Summary"));

  tab_headers (t, heading_columns, 0, heading_rows, 0);

  tab_box (t, TAL_2, TAL_2, -1, TAL_1, 0, 0, nc - 1, nr - 1);

  tab_hline (t, TAL_2, 0, nc - 1, heading_rows);
  tab_vline (t, TAL_2, heading_columns, 0, nr - 1);

  tab_text (t,  0, 0, TAB_LEFT | TAT_TITLE, _("Step 1"));
  tab_text (t,  1, 0, TAB_CENTER | TAT_TITLE, _("-2 Log likelihood"));
  tab_double (t,  1, 1, 0, -2 * log (likelihood), 0);


  tab_text (t,  2, 0, TAB_CENTER | TAT_TITLE, _("Cox & Snell R Square"));
  cox =  1.0 - pow (initial_likelihood /likelihood, 2 / res->cc);
  tab_double (t,  2, 1, 0, cox, 0);

  tab_text (t,  3, 0, TAB_CENTER | TAT_TITLE, _("Nagelkerke R Square"));
  tab_double (t,  3, 1, 0, cox / ( 1.0 - pow (initial_likelihood, 2 / res->cc)), 0);


  tab_submit (t);
}

/* Show the case processing summary box */
static void
case_processing_summary (const struct lr_result *res)
{
  const int heading_columns = 1;
  const int heading_rows = 1;
  struct tab_table *t;

  const int nc = 3;
  const int nr = heading_rows + 3;
  casenumber total;

  t = tab_create (nc, nr);
  tab_title (t, _("Case Processing Summary"));

  tab_headers (t, heading_columns, 0, heading_rows, 0);

  tab_box (t, TAL_2, TAL_2, -1, TAL_1, 0, 0, nc - 1, nr - 1);

  tab_hline (t, TAL_2, 0, nc - 1, heading_rows);
  tab_vline (t, TAL_2, heading_columns, 0, nr - 1);

  tab_text (t,  0, 0, TAB_LEFT | TAT_TITLE, _("Unweighted Cases"));
  tab_text (t,  1, 0, TAB_CENTER | TAT_TITLE, _("N"));
  tab_text (t,  2, 0, TAB_CENTER | TAT_TITLE, _("Percent"));


  tab_text (t,  0, 1, TAB_LEFT | TAT_TITLE, _("Included in Analysis"));
  tab_text (t,  0, 2, TAB_LEFT | TAT_TITLE, _("Missing Cases"));
  tab_text (t,  0, 3, TAB_LEFT | TAT_TITLE, _("Total"));

  tab_double (t,  1, 1, 0, res->n_nonmissing, &F_8_0);
  tab_double (t,  1, 2, 0, res->n_missing, &F_8_0);

  total = res->n_nonmissing + res->n_missing;
  tab_double (t,  1, 3, 0, total , &F_8_0);

  tab_double (t,  2, 1, 0, 100 * res->n_nonmissing / (double) total, 0);
  tab_double (t,  2, 2, 0, 100 * res->n_missing / (double) total, 0);
  tab_double (t,  2, 3, 0, 100 * total / (double) total, 0);

  tab_submit (t);
}

