/* PSPP - a program for statistical analysis.
   Copyright (C) 2005, 2009, 2011 Free Software Foundation, Inc.

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
  Find the least-squares estimate of b for the linear model:

  Y = Xb + Z

  where Y is an n-by-1 column vector, X is an n-by-p matrix of
  independent variables, b is a p-by-1 vector of regression coefficients,
  and Z is an n-by-1 normally-distributed random vector with independent
  identically distributed components with mean 0.

  This estimate is found via the sweep operator, which is a modification
  of Gauss-Jordan pivoting.


  References:

  Matrix Computations, third edition. GH Golub and CF Van Loan.
  The Johns Hopkins University Press. 1996. ISBN 0-8018-5414-8.

  Numerical Analysis for Statisticians. K Lange. Springer. 1999.
  ISBN 0-387-94979-8.

  Numerical Linear Algebra for Applications in Statistics. JE Gentle.
  Springer. 1998. ISBN 0-387-98542-5.
*/

#include <config.h>

#include "sweep.h"

#include <assert.h>
/*
  The matrix A will be overwritten. In ordinary uses of the sweep
  operator, A will be the matrix

  __       __
  |X'X    X'Y|
  |          |
  |Y'X    Y'Y|
  --        --

  X refers to the design matrix and Y to the vector of dependent
  observations. reg_sweep sweeps on the diagonal elements of
  X'X.

  The matrix A is assumed to be symmetric, so the sweep operation is
  performed only for the upper triangle of A.

  LAST_COL is considered to be the final column in the augmented matrix,
  that is, the column to the right of the '=' sign of the system.
*/

int
reg_sweep (gsl_matrix * A, int last_col)
{
  int i;
  int j;
  int k;
  gsl_matrix *B;

  if (A == NULL)
    return GSL_EFAULT;

  if (A->size1 != A->size2)
    return GSL_ENOTSQR;

  assert (last_col < A->size1);
  gsl_matrix_swap_rows (A, A->size1 - 1, last_col);
  gsl_matrix_swap_columns (A, A->size1 - 1 , last_col);
	  
  B = gsl_matrix_alloc (A->size1, A->size2);
  for (k = 0; k < (A->size1 - 1); k++)
    {
      const double sweep_element = gsl_matrix_get (A, k, k);
      if (fabs (sweep_element) > GSL_DBL_MIN)
	{
	  gsl_matrix_set (B, k, k, -1.0 / sweep_element);
	  /*
	    Rows before current row k.
	  */
	  for (i = 0; i < k; i++)
	    {
	      for (j = i; j < A->size2; j++)
		{
		  /* Use only the upper triangle of A. */
		  double tmp;
		  if (j < k)
		    {
		      tmp = gsl_matrix_get (A, i, j) -
			gsl_matrix_get (A, i, k)
			* gsl_matrix_get (A, j, k) / sweep_element;
		    }
		  else if (j > k)
		    {
		      tmp = gsl_matrix_get (A, i, j) -
			gsl_matrix_get (A, i, k)
			* gsl_matrix_get (A, k, j) / sweep_element;
		    }
		  else
		    {
		      tmp = gsl_matrix_get (A, i, k) / sweep_element;
		    }
		  gsl_matrix_set (B, i, j, tmp);
		}
	    }
	  /*
	    Current row k.
	  */
	  for (j = k + 1; j < A->size1; j++)
	    {
	      double tmp = gsl_matrix_get (A, k, j) / sweep_element;
	      gsl_matrix_set (B, k, j, tmp);
	    }
	  /*
	    Rows after the current row k.
	  */
	  for (i = k + 1; i < A->size1; i++)
	    {
	      for (j = i; j < A->size2; j++)
		{
		  double tmp = gsl_matrix_get (A, i, j) -
		    gsl_matrix_get (A, k, i)
		    * gsl_matrix_get (A, k, j) / sweep_element;
		  gsl_matrix_set (B, i, j, tmp);
		}
	    }
	}
      gsl_matrix_memcpy (A, B);
    }
  gsl_matrix_free (B);

  gsl_matrix_swap_columns (A, A->size1 - 1 , last_col);
  gsl_matrix_swap_rows (A, A->size1 - 1, last_col);

  return GSL_SUCCESS;
}
