/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#if !statistics_h
#define statistics_h 1

/* These are all sample statistics except for mean since uses
   population statistics for whatever reason. */

/* Define pi to the maximum precision available. */
#include <math.h>		/* defines M_PI on many systems */
#ifndef PI
#ifdef M_PI
#define PI M_PI
#else /* !PI && !M_PI */
#define PI 3.14159265358979323846264338327
#endif /* !PI && !M_PI */
#endif /* !PI */

extern double pow4 (double);
extern double cube (double);
extern double sqr (double);

/* Returns the fourth power of its argument. */
extern inline double
pow4 (double x)
{
  x *= x;
  return x * x;
}

/* Returns the cube of its argument. */
extern inline double
cube (double x)
{
  return x * x * x;
}

/* Returns the square of its argument. */
extern inline double
sqr (double x)
{
  return x * x;
}

/* Mean, standard error of mean. */
#define calc_mean(D, N) 					\
	((D)[0] / (N))
#define calc_semean(STDDEV, N) 			\
	((STDDEV) / sqrt (N))

/* Variance, standard deviation, coefficient of variance. */
#define calc_variance(D, N) 				\
	( ((D)[1] - sqr ((D)[0])/(N)) / ((N)-1) )
#define calc_stddev(VARIANCE) 			\
	(sqrt (VARIANCE))
#define calc_cfvar(D, N) 					\
	( calc_stddev (calc_variance (D, N)) / calc_mean (D, N) )

/* Kurtosis, standard error of kurtosis. */
double calc_kurt (const double d[4], double n, double variance);
double calc_sekurt (double n);

/* Skewness, standard error of skewness. */
double calc_skew (const double d[3], double n, double stddev);
double calc_seskew (double n);

/* Significance. */
double normal_sig (double x);
double chisq_sig (double chisq, int df);

#endif /* !statistics_h */
