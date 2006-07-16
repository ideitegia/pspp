/*
  src/math/ts/innovations.h
  
  Copyright (C) 2006 Free Software Foundation, Inc. Written by Jason H. Stover.
  
  This program is free software; you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free
  Software Foundation; either version 2 of the License, or (at your option)
  any later version.
  
  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.
  
  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 51
  Franklin Street, Fifth Floor, Boston, MA 02111-1307, USA.
 */
/*
  Find preliminary ARMA coefficients via the innovations algorithm.
  Also compute the sample mean and covariance matrix for each series.

  Reference:

  P. J. Brockwell and R. A. Davis. Time Series: Theory and
  Methods. Second edition. Springer. New York. 1991. ISBN
  0-387-97429-6. Sections 5.2, 8.3 and 8.4.
 */
#ifndef INNOVATIONS_H
#define INNOVATIONS_H
#include <math/coefficient.h>
#include <math/design-matrix.h>

struct innovations_estimate
{
  const struct variable *variable;
  double mean;
  double *cov;
  double *scale;
  double n_obs;
  double max_lag;
  coefficient **coeff;
};
struct innovations_estimate ** pspp_innovations (const struct design_matrix *, size_t);
void pspp_innovations_free (struct innovations_estimate **, size_t);
#endif
