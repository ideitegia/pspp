/* PSPP - a program for statistical analysis.
   Copyright (C) 2005 Free Software Foundation, Inc.

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

#include "sweep.h"

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
 */

int
reg_sweep (gsl_matrix * A)
{
  double sweep_element;
  double tmp;
  int i;
  int j;
  int k;
  gsl_matrix *B;

  if (A != NULL)
    {
      if (A->size1 == A->size2)
	{
	  B = gsl_matrix_alloc (A->size1, A->size2);
	  for (k = 0; k < (A->size1 - 1); k++)
	    {
	      sweep_element = gsl_matrix_get (A, k, k);
	      if (fabs (sweep_element) > GSL_DBL_MIN)
		{
		  tmp = -1.0 / sweep_element;
		  gsl_matrix_set (B, k, k, tmp);
		  /*
		     Rows before current row k.
		   */
		  for (i = 0; i < k; i++)
		    {
		      for (j = i; j < A->size2; j++)
			{
			  /*
			     Use only the upper triangle of A.
			   */
			  if (j < k)
			    {
			      tmp = gsl_matrix_get (A, i, j) -
				gsl_matrix_get (A, i, k)
				* gsl_matrix_get (A, j, k) / sweep_element;
			      gsl_matrix_set (B, i, j, tmp);
			    }
			  else if (j > k)
			    {
			      tmp = gsl_matrix_get (A, i, j) -
				gsl_matrix_get (A, i, k)
				* gsl_matrix_get (A, k, j) / sweep_element;
			      gsl_matrix_set (B, i, j, tmp);
			    }
			  else
			    {
			      tmp = gsl_matrix_get (A, i, k) / sweep_element;
			      gsl_matrix_set (B, i, j, tmp);
			    }
			}
		    }
		  /*
		     Current row k.
		   */
		  for (j = k + 1; j < A->size1; j++)
		    {
		      tmp = gsl_matrix_get (A, k, j) / sweep_element;
		      gsl_matrix_set (B, k, j, tmp);
		    }
		  /*
		     Rows after the current row k.
		   */
		  for (i = k + 1; i < A->size1; i++)
		    {
		      for (j = i; j < A->size2; j++)
			{
			  tmp = gsl_matrix_get (A, i, j) -
			    gsl_matrix_get (A, k, i)
			    * gsl_matrix_get (A, k, j) / sweep_element;
			  gsl_matrix_set (B, i, j, tmp);
			}
		    }
		}
	      for (i = 0; i < A->size1; i++)
		for (j = i; j < A->size2; j++)
		  {
		    gsl_matrix_set (A, i, j, gsl_matrix_get (B, i, j));
		  }
	    }
	  gsl_matrix_free (B);
	  return GSL_SUCCESS;
	}
      return GSL_ENOTSQR;
    }
  return GSL_EFAULT;
}
