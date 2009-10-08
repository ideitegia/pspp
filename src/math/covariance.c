/* PSPP - a program for statistical analysis.
   Copyright (C) 2009 Free Software Foundation, Inc.

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

#include "covariance.h"
#include <gl/xalloc.h>
#include "moments.h"
#include <gsl/gsl_matrix.h>
#include <data/case.h>
#include <data/variable.h>
#include <libpspp/misc.h>

#define n_MOMENTS (MOMENT_VARIANCE + 1)


struct covariance
{
  /* The variables for which the covariance matrix is to be calculated. */
  size_t n_vars;
  const struct variable **vars;

  /* Categorical variables. */
  size_t n_catvars;
  const struct variable **catvars;

  /* Array containing number of categories per categorical variable. */
  size_t *n_categories;

  /* Dimension of the covariance matrix. */
  size_t dim;

  /* The weight variable (or NULL if none) */
  const struct variable *wv;

  /* A set of matrices containing the 0th, 1st and 2nd moments */
  gsl_matrix **moments;

  /* The class of missing values to exclude */
  enum mv_class exclude;

  /* An array of doubles representing the covariance matrix.
     Only the top triangle is included, and no diagonals */
  double *cm;
  int n_cm;
};



/* Return a matrix containing the M th moments.
   The matrix is of size  NxN where N is the number of variables.
   Each row represents the moments of a variable.
   In the absence of missing values, the columns of this matrix will
   be identical.  If missing values are involved, then element (i,j)
   is the moment of the i th variable, when paired with the j th variable.
 */
const gsl_matrix *
covariance_moments (const struct covariance *cov, int m)
{
  return cov->moments[m];
}



/* Create a covariance struct.
 */
struct covariance *
covariance_create (size_t n_vars, const struct variable **vars,
		   const struct variable *weight, enum mv_class exclude)
{
  size_t i;
  struct covariance *cov = xmalloc (sizeof *cov);
  cov->vars = xmalloc (sizeof *cov->vars * n_vars);

  cov->wv = weight;
  cov->n_vars = n_vars;
  cov->dim = n_vars;

  for (i = 0; i < n_vars; ++i)
    cov->vars[i] = vars[i];

  cov->moments = xmalloc (sizeof *cov->moments * n_MOMENTS);
  
  for (i = 0; i < n_MOMENTS; ++i)
    cov->moments[i] = gsl_matrix_calloc (n_vars, n_vars);

  cov->exclude = exclude;

  cov->n_cm = (n_vars * (n_vars - 1)  ) / 2;

  cov->cm = xcalloc (sizeof *cov->cm, cov->n_cm);

  return cov;
}

/*
  Create a covariance struct for a two-pass algorithm. If categorical
  variables are involed, the dimension cannot be know until after the
  first data pass, so the actual covariances will not be allocated
  until then.
 */
struct covariance *
covariance_2pass_create (size_t n_vars, const struct variable **vars,
			 size_t n_catvars, const struct variable **catvars, 
			 const struct variable *weight, enum mv_class exclude)
{
  size_t i;
  struct covariance *cov = xmalloc (sizeof *cov);
  cov->vars = xmalloc (sizeof *cov->vars * n_vars);
  cov->catvars = xnmalloc (n_catvars, sizeof (*cov->catvars));
  cov->n_categories = xnmalloc (n_catvars, sizeof (cov->n_categories));

  cov->wv = weight;
  cov->n_vars = n_vars;
  cov->n_catvars = n_catvars;

  for (i = 0; i < n_vars; ++i)
    cov->vars[i] = vars[i];

  for (i = 0; i < n_catvars; i++)
    {
      cov->catvars[i] = catvars[i];
      cov->n_categories[i] = 0;
    }

  cov->moments = xmalloc (sizeof *cov->moments * n_MOMENTS);
  
  cov->exclude = exclude;

  return cov;
}

/* Return an integer, which can be used to index 
   into COV->cm, to obtain the I, J th element
   of the covariance matrix.  If COV->cm does not
   contain that element, then a negative value
   will be returned.
*/
static int
cm_idx (const struct covariance *cov, int i, int j)
{
  int as;
  const int n2j = cov->n_vars - 2 - j;
  const int nj = cov->n_vars - 2 ;
  
  assert (i >= 0);
  assert (j < cov->n_vars);

  if ( i == 0)
    return -1;

  if (j >= cov->n_vars - 1)
    return -1;

  if ( i <= j) 
    return -1 ;

  as = nj * (nj + 1) ;
  as -= n2j * (n2j + 1) ; 
  as /= 2;

  return i - 1 + as;
}


/* Call this function for every case in the data set */
void
covariance_accumulate (struct covariance *cov, const struct ccase *c)
{
  size_t i, j, m;
  const double weight = cov->wv ? case_data (c, cov->wv)->f : 1.0;

  for (i = 0 ; i < cov->n_vars; ++i)
    {
      const union value *val1 = case_data (c, cov->vars[i]);

      if ( var_is_value_missing (cov->vars[i], val1, cov->exclude))
	continue;

      for (j = 0 ; j < cov->n_vars; ++j)
	{
	  double pwr = 1.0;
	  int idx;
	  const union value *val2 = case_data (c, cov->vars[j]);

	  if ( var_is_value_missing (cov->vars[j], val2, cov->exclude))
	    continue;

	  idx = cm_idx (cov, i, j);
	  if (idx >= 0)
	    {
	      cov->cm [idx] += val1->f * val2->f * weight;
	    }

	  for (m = 0 ; m < n_MOMENTS; ++m)
	    {
	      double *x = gsl_matrix_ptr (cov->moments[m], i, j);

	      *x += pwr * weight;
	      pwr *= val1->f;
	    }
	}
    }
}


/* 
   Allocate and return a gsl_matrix containing the covariances of the
   data.
*/
static gsl_matrix *
cm_to_gsl (struct covariance *cov)
{
  int i, j;
  gsl_matrix *m = gsl_matrix_calloc (cov->n_vars, cov->n_vars);

  /* Copy the non-diagonal elements from cov->cm */
  for ( j = 0 ; j < cov->n_vars - 1; ++j)
    {
      for (i = j+1 ; i < cov->n_vars; ++i)
	{
	  double x = cov->cm [cm_idx (cov, i, j)];
	  gsl_matrix_set (m, i, j, x);
	  gsl_matrix_set (m, j, i, x);
	}
    }

  /* Copy the diagonal elements from cov->moments[2] */
  for (j = 0 ; j < cov->n_vars ; ++j)
    {
      double sigma = gsl_matrix_get (cov->moments[2], j, j);
      gsl_matrix_set (m, j, j, sigma);
    }

  return m;
}



/* 
   Return a pointer to gsl_matrix containing the pairwise covariances.
   The matrix remains owned by the COV object, and must not be freed.
   Call this function only after all data have been accumulated.
*/
const gsl_matrix *
covariance_calculate (struct covariance *cov)
{
  size_t i, j;
  size_t m;

  for (m = 0; m < n_MOMENTS; ++m)
    {
      /* Divide the moments by the number of samples */
      if ( m > 0)
	{
	  for (i = 0 ; i < cov->n_vars; ++i)
	    {
	      for (j = 0 ; j < cov->n_vars; ++j)
		{
		  double *x = gsl_matrix_ptr (cov->moments[m], i, j);
		  *x /= gsl_matrix_get (cov->moments[0], i, j);

		  if ( m == MOMENT_VARIANCE)
		    *x -= pow2 (gsl_matrix_get (cov->moments[1], i, j));
		}
	    }
	}
    }

  /* Centre the moments */
  for ( j = 0 ; j < cov->n_vars - 1; ++j)
    {
      for (i = j + 1 ; i < cov->n_vars; ++i)
	{
	  double *x = &cov->cm [cm_idx (cov, i, j)];
	  
	  *x /= gsl_matrix_get (cov->moments[0], i, j);

	  *x -=
	    gsl_matrix_get (cov->moments[MOMENT_MEAN], i, j) 
	    *
	    gsl_matrix_get (cov->moments[MOMENT_MEAN], j, i); 
	}
    }

  return cm_to_gsl (cov);
}


/* Destroy the COV object */
void
covariance_destroy (struct covariance *cov)
{
  size_t i;
  free (cov->vars);

  for (i = 0; i < n_MOMENTS; ++i)
    gsl_matrix_free (cov->moments[i]);

  free (cov->moments);
  free (cov->cm);
  free (cov);
}
