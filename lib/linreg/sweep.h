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

#ifndef SWEEP_H
#define SWEEP_H

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

#include <gsl/gsl_matrix.h>
#include <gsl/gsl_math.h>

int reg_sweep (gsl_matrix *, int);

#endif
