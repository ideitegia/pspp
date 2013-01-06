/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2010, 2011 Free Software Foundation, Inc.

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

#include "math/moments.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include "data/val-type.h"
#include "data/value.h"
#include "libpspp/misc.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Calculates variance, skewness, and kurtosis into *VARIANCE,
   *SKEWNESS, and *KURTOSIS if they are non-null and not greater
   moments than MAX_MOMENT.  Accepts W as the total weight, D1 as
   the total deviation from the estimated mean, and D2, D3, and
   D4 as the sum of the squares, cubes, and 4th powers,
   respectively, of the deviation from the estimated mean. */
static void
calc_moments (enum moment max_moment,
              double w, double d1, double d2, double d3, double d4,
              double *variance, double *skewness, double *kurtosis)
{
  assert (w > 0.);

  if (max_moment >= MOMENT_VARIANCE && w > 1.)
    {
      double s2 = (d2 - pow2 (d1) / w) / (w - 1.);
      if (variance != NULL)
        *variance = s2;

      /* From _SPSS Statistical Algorithms, 2nd ed.,
         0-918469-89-9, section "DESCRIPTIVES". */
      if (fabs (s2) >= 1e-20)
        {
          if (max_moment >= MOMENT_SKEWNESS && skewness != NULL && w > 2.)
            {
              double s3 = s2 * sqrt (s2);
              double g1 = (w * d3) / ((w - 1.0) * (w - 2.0) * s3);
              if (isfinite (g1))
                *skewness = g1;
            }
          if (max_moment >= MOMENT_KURTOSIS && kurtosis != NULL && w > 3.)
            {
              double den = (w - 2.) * (w - 3.) * pow2 (s2);
              double g2 = (w * (w + 1) * d4 / (w - 1.) / den
                           - 3. * pow2 (d2) / den);
              if (isfinite (g2))
                *kurtosis = g2;
            }
        }
    }
}

/* Two-pass moments. */

/* A set of two-pass moments. */
struct moments
  {
    enum moment max_moment;     /* Highest-order moment we're computing. */
    int pass;                   /* Current pass (1 or 2). */

    /* Pass one. */
    double w1;                  /* Total weight for pass 1, so far. */
    double sum;                 /* Sum of values so far. */
    double mean;                /* Mean = sum / w1. */

    /* Pass two. */
    double w2;                  /* Total weight for pass 2, so far. */
    double d1;                  /* Sum of deviations from the mean. */
    double d2;                  /* Sum of squared deviations from the mean. */
    double d3;                  /* Sum of cubed deviations from the mean. */
    double d4;                  /* Sum of (deviations from the mean)**4. */
  };

/* Initializes moments M for calculating moment MAX_MOMENT and
   lower moments. */
static void
init_moments (struct moments *m, enum moment max_moment)
{
  assert (m != NULL);
  assert (max_moment == MOMENT_MEAN || max_moment == MOMENT_VARIANCE
          || max_moment == MOMENT_SKEWNESS || max_moment == MOMENT_KURTOSIS);
  m->max_moment = max_moment;
  moments_clear (m);
}

/* Clears out a set of moments so that it can be reused for a new
   set of values.  The moments to be calculated are not changed. */
void
moments_clear (struct moments *m)
{
  m->pass = 1;
  m->w1 = m->w2 = 0.;
  m->sum = 0.;
}

/* Creates and returns a data structure for calculating moment
   MAX_MOMENT and lower moments on a data series.  The user
   should call moments_pass_one() for each value in the series,
   then call moments_pass_two() for the same set of values in the
   same order, then call moments_calculate() to obtain the
   moments.  The user may ask for the mean at any time during the
   first pass (using moments_calculate()), but otherwise no
   statistics may be requested until the end of the second pass.
   Call moments_destroy() when the moments are no longer
   needed. */
struct moments *
moments_create (enum moment max_moment)
{
  struct moments *m = xmalloc (sizeof *m);
  init_moments (m, max_moment);
  return m;
}

/* Adds VALUE with the given WEIGHT to the calculation of
   moments for the first pass. */
void
moments_pass_one (struct moments *m, double value, double weight)
{
  assert (m != NULL);
  assert (m->pass == 1);

  if (value != SYSMIS && weight > 0.)
    {
      m->sum += value * weight;
      m->w1 += weight;
    }
}

/* Adds VALUE with the given WEIGHT to the calculation of
   moments for the second pass. */
void
moments_pass_two (struct moments *m, double value, double weight)
{
  assert (m != NULL);

  if (m->pass == 1)
    {
      m->pass = 2;
      m->mean = m->w1 != 0. ? m->sum / m->w1 : 0.;
      m->d1 = m->d2 = m->d3 = m->d4 = 0.;
    }

  if (value != SYSMIS && weight >= 0.)
    {
      double d = value - m->mean;
      double d1_delta = d * weight;
      m->d1 += d1_delta;
      if (m->max_moment >= MOMENT_VARIANCE)
        {
          double d2_delta = d1_delta * d;
          m->d2 += d2_delta;
          if (m->max_moment >= MOMENT_SKEWNESS)
            {
              double d3_delta = d2_delta * d;
              m->d3 += d3_delta;
              if (m->max_moment >= MOMENT_KURTOSIS)
                {
                  double d4_delta = d3_delta * d;
                  m->d4 += d4_delta;
                }
            }
        }
      m->w2 += weight;
    }
}

/* Calculates moments based on the input data.  Stores the total
   weight in *WEIGHT, the mean in *MEAN, the variance in
   *VARIANCE, the skewness in *SKEWNESS, and the kurtosis in
   *KURTOSIS.  Any of these result parameters may be null
   pointers, in which case the values are not calculated.  If any
   result cannot be calculated, either because they are undefined
   based on the input data or because their moments are higher
   than the maximum requested on moments_create(), then SYSMIS is
   stored into that result. */
void
moments_calculate (const struct moments *m,
                   double *weight,
                   double *mean, double *variance,
                   double *skewness, double *kurtosis)
{
  assert (m != NULL);

  if (mean != NULL)
    *mean = SYSMIS;
  if (variance != NULL)
    *variance = SYSMIS;
  if (skewness != NULL)
    *skewness = SYSMIS;
  if (kurtosis != NULL)
    *kurtosis = SYSMIS;

  if (weight != NULL)
    *weight = m->w1;

  /* How many passes so far? */
  if (m->pass == 1)
    {
      /* In the first pass we can only calculate the mean. */
      if (mean != NULL && m->w1 > 0.)
        *mean = m->sum / m->w1;
    }
  else
    {
      /* After the second pass we can calculate any stat.  */
      assert (m->pass == 2);

      if (m->w2 > 0.)
        {
          if (mean != NULL)
            *mean = m->mean;
          calc_moments (m->max_moment,
                        m->w2, m->d1, m->d2, m->d3, m->d4,
                        variance, skewness, kurtosis);
        }
    }
}

/* Destroys a set of moments. */
void
moments_destroy (struct moments *m)
{
  free (m);
}

/* Calculates the requested moments on the CNT values in ARRAY.
   Each value is given a weight of 1.  The total weight is stored
   into *WEIGHT (trivially) and the mean, variance, skewness, and
   kurtosis are stored into *MEAN, *VARIANCE, *SKEWNESS, and
   *KURTOSIS, respectively.  Any of the result pointers may be
   null, in which case no value is stored. */
void
moments_of_doubles (const double *array, size_t cnt,
                    double *weight,
                    double *mean, double *variance,
                    double *skewness, double *kurtosis)
{
  enum moment max_moment;
  struct moments m;
  size_t idx;

  if (kurtosis != NULL)
    max_moment = MOMENT_KURTOSIS;
  else if (skewness != NULL)
    max_moment = MOMENT_SKEWNESS;
  else if (variance != NULL)
    max_moment = MOMENT_VARIANCE;
  else
    max_moment = MOMENT_MEAN;

  init_moments (&m, max_moment);
  for (idx = 0; idx < cnt; idx++)
    moments_pass_one (&m, array[idx], 1.);
  for (idx = 0; idx < cnt; idx++)
    moments_pass_two (&m, array[idx], 1.);
  moments_calculate (&m, weight, mean, variance, skewness, kurtosis);
}

/* Calculates the requested moments on the CNT numeric values in
   ARRAY.  Each value is given a weight of 1.  The total weight
   is stored into *WEIGHT (trivially) and the mean, variance,
   skewness, and kurtosis are stored into *MEAN, *VARIANCE,
   *SKEWNESS, and *KURTOSIS, respectively.  Any of the result
   pointers may be null, in which case no value is stored. */
void
moments_of_values (const union value *array, size_t cnt,
                   double *weight,
                   double *mean, double *variance,
                   double *skewness, double *kurtosis)
{
  enum moment max_moment;
  struct moments m;
  size_t idx;

  if (kurtosis != NULL)
    max_moment = MOMENT_KURTOSIS;
  else if (skewness != NULL)
    max_moment = MOMENT_SKEWNESS;
  else if (variance != NULL)
    max_moment = MOMENT_VARIANCE;
  else
    max_moment = MOMENT_MEAN;

  init_moments (&m, max_moment);
  for (idx = 0; idx < cnt; idx++)
    moments_pass_one (&m, array[idx].f, 1.);
  for (idx = 0; idx < cnt; idx++)
    moments_pass_two (&m, array[idx].f, 1.);
  moments_calculate (&m, weight, mean, variance, skewness, kurtosis);
}

/* One-pass moments. */

/* A set of one-pass moments. */
struct moments1
  {
    enum moment max_moment;     /* Highest-order moment we're computing. */
    double w;                   /* Total weight so far. */
    double d1;                  /* Sum of deviations from the mean. */
    double d2;                  /* Sum of squared deviations from the mean. */
    double d3;                  /* Sum of cubed deviations from the mean. */
    double d4;                  /* Sum of (deviations from the mean)**4. */
  };

/* Initializes one-pass moments M for calculating moment
   MAX_MOMENT and lower moments. */
static void
init_moments1 (struct moments1 *m, enum moment max_moment)
{
  assert (m != NULL);
  assert (max_moment == MOMENT_MEAN || max_moment == MOMENT_VARIANCE
          || max_moment == MOMENT_SKEWNESS || max_moment == MOMENT_KURTOSIS);
  m->max_moment = max_moment;
  moments1_clear (m);
}

/* Clears out a set of one-pass moments so that it can be reused
   for a new set of values.  The moments to be calculated are not
   changed. */
void
moments1_clear (struct moments1 *m)
{
  m->w = 0.;
  m->d1 = m->d2 = m->d3 = m->d4 = 0.;
}

/* Creates and returns a data structure for calculating moment
   MAX_MOMENT and lower moments on a data series in a single
   pass.  The user should call moments1_add() for each value in
   the series.  The user may call moments1_calculate() to obtain
   the current moments at any time.  Call moments1_destroy() when
   the moments are no longer needed.

   One-pass moments should only be used when two passes over the
   data are impractical. */
struct moments1 *
moments1_create (enum moment max_moment)
{
  struct moments1 *m = xmalloc (sizeof *m);
  init_moments1 (m, max_moment);
  return m;
}

/* Adds VALUE with the given WEIGHT to the calculation of
   one-pass moments. */
void
moments1_add (struct moments1 *m, double value, double weight)
{
  assert (m != NULL);

  if (value != SYSMIS && weight > 0.)
    {
      double prev_w, v1;

      prev_w = m->w;
      m->w += weight;
      v1 = (weight / m->w) * (value - m->d1);
      m->d1 += v1;

      if (m->max_moment >= MOMENT_VARIANCE)
        {
          double v2 = v1 * v1;
          double w_prev_w = m->w * prev_w;
          double prev_m2 = m->d2;

          m->d2 += w_prev_w / weight * v2;
          if (m->max_moment >= MOMENT_SKEWNESS)
            {
              double w2 = weight * weight;
              double v3 = v2 * v1;
              double prev_m3 = m->d3;

              m->d3 += (-3. * v1 * prev_m2
                         + w_prev_w / w2 * (m->w - 2. * weight) * v3);
              if (m->max_moment >= MOMENT_KURTOSIS)
                {
                  double w3 = w2 * weight;
                  double v4 = v2 * v2;

                  m->d4 += (-4. * v1 * prev_m3
                             + 6. * v2 * prev_m2
                             + ((pow2 (m->w) - 3. * weight * prev_w)
                                * v4 * w_prev_w / w3));
                }
            }
        }
    }
}

/* Calculates one-pass moments based on the input data.  Stores
   the total weight in *WEIGHT, the mean in *MEAN, the variance
   in *VARIANCE, the skewness in *SKEWNESS, and the kurtosis in
   *KURTOSIS.  Any of these result parameters may be null
   pointers, in which case the values are not calculated.  If any
   result cannot be calculated, either because they are undefined
   based on the input data or because their moments are higher
   than the maximum requested on moments_create(), then SYSMIS is
   stored into that result. */
void
moments1_calculate (const struct moments1 *m,
                    double *weight,
                    double *mean, double *variance,
                    double *skewness, double *kurtosis)
{
  assert (m != NULL);

  if (mean != NULL)
    *mean = SYSMIS;
  if (variance != NULL)
    *variance = SYSMIS;
  if (skewness != NULL)
    *skewness = SYSMIS;
  if (kurtosis != NULL)
    *kurtosis = SYSMIS;

  if (weight != NULL)
    *weight = m->w;

  if (m->w > 0.)
    {
      if (mean != NULL)
        *mean = m->d1;

      calc_moments (m->max_moment,
                    m->w, 0., m->d2, m->d3, m->d4,
                    variance, skewness, kurtosis);
    }
}

/* Destroy one-pass moments M. */
void
moments1_destroy (struct moments1 *m)
{
  free (m);
}


double
calc_semean (double var, double W)
{
  return sqrt (var / W);
}

/* Returns the standard error of the skewness for the given total
   weight W.

   From _SPSS Statistical Algorithms, 2nd ed., 0-918469-89-9,
   section "DESCRIPTIVES". */
double
calc_seskew (double W)
{
  return sqrt ((6. * W * (W - 1.)) / ((W - 2.) * (W + 1.) * (W + 3.)));
}

/* Returns the standard error of the kurtosis for the given total
   weight W.

   From _SPSS Statistical Algorithms, 2nd ed., 0-918469-89-9,
   section "DESCRIPTIVES", except that the sqrt symbol is omitted
   there. */
double
calc_sekurt (double W)
{
  return sqrt ((4. * (pow2 (W) - 1.) * pow2 (calc_seskew (W)))
               / ((W - 3.) * (W + 5.)));
}
