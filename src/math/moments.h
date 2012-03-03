/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2011 Free Software Foundation, Inc.

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

#ifndef HEADER_MOMENTS
#define HEADER_MOMENTS

#include <stddef.h>
#include "data/value.h"

/* Moments of the mean.
   Higher-order moments have higher values. */
enum moment
  {
    MOMENT_NONE,                /* No moments. */
    MOMENT_MEAN,                /* First-order moment. */
    MOMENT_VARIANCE,            /* Second-order moment. */
    MOMENT_SKEWNESS,            /* Third-order moment. */
    MOMENT_KURTOSIS             /* Fourth-order moment. */
  };

struct moments;

/* Two-pass moments. */
struct moments *moments_create (enum moment max_moment);
void moments_clear (struct moments *);
void moments_pass_one (struct moments *, double value, double weight);
void moments_pass_two (struct moments *, double value, double weight);
void moments_calculate (const struct moments *,
                        double *weight,
                        double *mean, double *variance,
                        double *skewness, double *kurtosis);
void moments_destroy (struct moments *);

/* Convenience functions for two-pass moments. */
void moments_of_doubles (const double *array, size_t cnt,
                         double *weight,
                         double *mean, double *variance,
                         double *skewness, double *kurtosis);
void moments_of_values (const union value *array, size_t cnt,
                        double *weight,
                        double *mean, double *variance,
                        double *skewness, double *kurtosis);

/* One-pass moments.  Use only if two passes are impractical. */
struct moments1 *moments1_create (enum moment max_moment);
void moments1_clear (struct moments1 *);
void moments1_add (struct moments1 *, double value, double weight);
void moments1_calculate (const struct moments1 *,
                         double *weight,
                         double *mean, double *variance,
                         double *skewness, double *kurtosis);
void moments1_destroy (struct moments1 *);

/* Standard errors. */
double calc_semean (double var, double weight);
double calc_seskew (double weight);
double calc_sekurt (double weight);

#endif /* moments.h */
