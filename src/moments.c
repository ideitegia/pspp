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

#include <config.h>
#include "moments.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include "alloc.h"
#include "misc.h"
#include "val.h"

/* FIXME?  _SPSS Statistical Algorithms_ in the DESCRIPTIVES
   second describes a "provisional means algorithm" that might be
   useful for improving accuracy when we only do one pass. */

/* A set of moments in process of calculation. */
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
   MAX_MOMENT and lower moments on a data series.  For greatest
   accuracy, the user should call moments_pass_one() for each
   value in the series, then call moments_pass_two() for the same
   set of values in the same order, then call moments_calculate()
   to obtain the moments.  At a cost of reduced accuracy, the
   first pass can be skipped.  In either case, moments_destroy()
   should be called when the moments are no longer needed. */
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

  if (value != SYSMIS && weight >= 0.) 
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
  double d, d_power;

  assert (m != NULL);

  if (m->pass == 1) 
    {
      m->pass = 2;
      m->mean = m->w1 != 0. ? m->sum / m->w1 : 0.;
      m->d1 = m->d2 = m->d3 = m->d4 = 0.;
    }

  if (value != SYSMIS && weight >= 0.) 
    {
      m->w2 += weight;

      d = d_power = value - m->mean;
      m->d1 += d_power * weight;

      if (m->max_moment >= MOMENT_VARIANCE) 
        {
          d_power *= d;
          m->d2 += d_power * weight;

          if (m->max_moment >= MOMENT_SKEWNESS)
            {
              d_power *= d;
              m->d3 += d_power * weight;

              if (m->max_moment >= MOMENT_KURTOSIS)
                {
                  d_power *= d;
                  m->d4 += d_power * weight;
                }
            }
        }
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
  double W;
  int one_pass;
  
  assert (m != NULL);
  assert (m->pass == 2);

  one_pass = m->w1 == 0.;
  
  /* If passes 1 and 2 are used, then w1 and w2 must agree. */
  assert (one_pass || m->w1 == m->w2);

  if (mean != NULL)
    *mean = SYSMIS;
  if (variance != NULL)
    *variance = SYSMIS;
  if (skewness != NULL)
    *skewness = SYSMIS;
  if (kurtosis != NULL)
    *kurtosis = SYSMIS;

  W = m->w2;
  if (weight != NULL)
    *weight = W;
  if (W == 0.)
    return;

  if (mean != NULL)
    *mean = m->mean + m->d1 / W;

  if (m->max_moment >= MOMENT_VARIANCE && W > 1.) 
    {
      double variance_tmp;

      /* From _Numerical Recipes in C_, 2nd ed., 0-521-43108-5,
         section 14.1. */
      if (variance == NULL)
        variance = &variance_tmp;
      *variance = (m->d2 - pow2 (m->d1) / W) / (W - 1.);

      /* From _SPSS Statistical Algorithms, 2nd ed.,
         0-918469-89-9, section "DESCRIPTIVES". */
      if (fabs (*variance) >= 1e-20) 
        {
          if (m->max_moment >= MOMENT_SKEWNESS && skewness != NULL && W > 2.)
            {
              *skewness = ((W * m->d3 - 3.0 * m->d1 * m->d2 + 2.0
                            * pow3 (m->d1) / W)
                           / ((W - 1.0) * (W - 2.0)
                              * *variance * sqrt (*variance)));
              if (!finite (*skewness))
                *skewness = SYSMIS; 
            }
          if (m->max_moment >= MOMENT_KURTOSIS && kurtosis != NULL && W > 3.)
            {
              *kurtosis = (((W + 1) * (W * m->d4
                                       - 4.0 * m->d1 * m->d3
                                       + 6.0 * pow2 (m->d1) * m->d2 / W
                                       - 3.0 * pow4 (m->d1) / pow2 (W)))
                           / ((W - 1.0) * (W - 2.0) * (W - 3.0)
                              * pow2 (*variance))
                           - (3.0 * pow2 (W - 1.0))
                           / ((W - 2.0) * (W - 3.)));
              if (!finite (*kurtosis))
                *kurtosis = SYSMIS; 
            }
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

#include <stdio.h>
#include "command.h"
#include "lexer.h"

static int
read_values (double **values, double **weights, size_t *cnt) 
{
  size_t cap = 0;

  *values = NULL;
  *weights = NULL;
  *cnt = 0;
  while (token == T_NUM) 
    {
      double value = tokval;
      double weight = 1.;
      lex_get ();
      if (lex_match ('*'))
        {
          if (token != T_NUM) 
            {
              lex_error (_("expecting weight value"));
              return 0;
            }
          weight = tokval;
          lex_get ();
        }

      if (*cnt >= cap) 
        {
          cap = 2 * (cap + 8);
          *values = xrealloc (*values, sizeof **values * cap);
          *weights = xrealloc (*weights, sizeof **weights * cap);
        }

      (*values)[*cnt] = value;
      (*weights)[*cnt] = weight;
      (*cnt)++;
    }

  return 1;
}

int
cmd_debug_moments (void) 
{
  int retval = CMD_FAILURE;
  struct moments *m = NULL;
  double *values = NULL;
  double *weights = NULL;
  double weight, M[4];
  int two_pass = 1;
  size_t cnt;
  size_t i;

  if (lex_match_id ("ONEPASS"))
    two_pass = 0;
  if (token != '/') 
    {
      lex_force_match ('/');
      goto done;
    }
  fprintf (stderr, "%s => ", lex_rest_of_line (NULL));
  lex_get ();

  m = moments_create (MOMENT_KURTOSIS);
  if (!read_values (&values, &weights, &cnt))
    goto done;
  if (two_pass) 
    {
      for (i = 0; i < cnt; i++)
        moments_pass_one (m, values[i], weights[i]); 
    }
  for (i = 0; i < cnt; i++)
    moments_pass_two (m, values[i], weights[i]);
  moments_calculate (m, &weight, &M[0], &M[1], &M[2], &M[3]);

  fprintf (stderr, "W=%.3f", weight);
  for (i = 0; i < 4; i++) 
    {
      fprintf (stderr, " M%d=", i + 1);
      if (M[i] == SYSMIS)
        fprintf (stderr, "sysmis");
      else if (fabs (M[i]) <= 0.0005)
        fprintf (stderr, "0.000");
      else
        fprintf (stderr, "%.3f", M[i]);
    }
  fprintf (stderr, "\n");

  retval = lex_end_of_command ();
  
 done:
  moments_destroy (m);
  free (values);
  free (weights);
  return retval;
}
