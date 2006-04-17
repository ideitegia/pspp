#include <config.h>
#include "helpers.h"
#include <gsl/gsl_roots.h>
#include <gsl/gsl_sf.h>
#include <libpspp/pool.h>
#include "private.h"

const struct fixed_string empty_string = {NULL, 0};

static void
expr_error (void *aux UNUSED, const char *format, ...) 
{
  struct error e;
  va_list args;

  /* FIXME: we can do better about saying where the error
     occurred. */
  e.category = MSG_SYNTAX;
  e.severity = MSG_ERROR;
  err_location (&e.where);
  e.title = NULL;

  va_start (args, format);
  err_vmsg (&e, format, args);
  va_end (args);
}

double
expr_ymd_to_ofs (double year, double month, double day)
{
  int y = year;
  int m = month;
  int d = day;

  if (y != year || m != month || d != day) 
    { 
      msg (SE, _("One of the arguments to a DATE function is not an integer.  "
                 "The result will be system-missing."));
      return SYSMIS;
    }

  return calendar_gregorian_to_offset (y, m, d, expr_error, NULL);
}

double
expr_ymd_to_date (double year, double month, double day)
{
  double ofs = expr_ymd_to_ofs (year, month, day);
  return ofs != SYSMIS ? ofs * DAY_S : SYSMIS;
}

double
expr_wkyr_to_date (double week, double year) 
{
  int w = week;

  if (w != week) 
    {
      msg (SE, _("The week argument to DATE.WKYR is not an integer.  "
                 "The result will be system-missing."));
      return SYSMIS;
    }
  else if (w < 1 || w > 53) 
    {
      msg (SE, _("The week argument to DATE.WKYR is outside the acceptable "
                 "range of 1 to 53.  "
                 "The result will be system-missing."));
      return SYSMIS;
    }
  else 
    {
      double yr_1_1 = expr_ymd_to_ofs (year, 1, 1);
      if (yr_1_1 != SYSMIS)
        return DAY_S * (yr_1_1 + WEEK_DAY * (w - 1));
      else
        return SYSMIS;
    }
}

double
expr_yrday_to_date (double year, double yday) 
{
  int yd = yday;

  if (yd != yday) 
    {
      msg (SE, _("The day argument to DATE.YRDAY is not an integer.  "
                 "The result will be system-missing."));
      return SYSMIS;
    }
  else if (yd < 1 || yd > 366) 
    {
      msg (SE, _("The day argument to DATE.YRDAY is outside the acceptable "
                 "range of 1 to 366.  "
                 "The result will be system-missing."));
      return SYSMIS;
    }
  else 
    {
      double yr_1_1 = expr_ymd_to_ofs (year, 1, 1);
      if (yr_1_1 != SYSMIS)
        return DAY_S * (yr_1_1 + yd - 1.);
      else
        return SYSMIS;
    }
}

double
expr_yrmoda (double year, double month, double day)
{ 
  if (year >= 0 && year <= 99)
    year += 1900;
  else if (year != (int) year && year > 47516) 
    {
      msg (SE, _("The year argument to YRMODA is greater than 47516.  "
                 "The result will be system-missing."));
      return SYSMIS;
    }

  return expr_ymd_to_ofs (year, month, day);
}

int
compare_string (const struct fixed_string *a, const struct fixed_string *b) 
{
  size_t i;

  for (i = 0; i < a->length && i < b->length; i++)
    if (a->string[i] != b->string[i]) 
      return a->string[i] < b->string[i] ? -1 : 1;
  for (; i < a->length; i++)
    if (a->string[i] != ' ')
      return 1;
  for (; i < b->length; i++)
    if (b->string[i] != ' ')
      return -1;
  return 0;
}

size_t
count_valid (double *d, size_t d_cnt) 
{
  size_t valid_cnt;
  size_t i;

  valid_cnt = 0;
  for (i = 0; i < d_cnt; i++)
    valid_cnt += is_valid (d[i]);
  return valid_cnt;
}

struct fixed_string
alloc_string (struct expression *e, size_t length) 
{
  struct fixed_string s;
  s.length = length;
  s.string = pool_alloc (e->eval_pool, length);
  return s;
}

struct fixed_string
copy_string (struct expression *e, const char *old, size_t length) 
{
  struct fixed_string s = alloc_string (e, length);
  memcpy (s.string, old, length);
  return s;
}

/* Returns the noncentral beta cumulative distribution function
   value for the given arguments.

   FIXME: The accuracy of this function is not entirely
   satisfactory.  We only match the example values given in AS
   310 to the first 5 significant digits. */
double
ncdf_beta (double x, double a, double b, double lambda) 
{
  double c;
  
  if (x <= 0. || x >= 1. || a <= 0. || b <= 0. || lambda <= 0.)
    return SYSMIS;

  c = lambda / 2.;
  if (lambda < 54.)
    {
      /* Algorithm AS 226. */
      double x0, a0, beta, temp, gx, q, ax, sumq, sum;
      double err_max = 2 * DBL_EPSILON;
      double err_bound;
      int iter_max = 100;
      int iter;

      x0 = floor (c - 5.0 * sqrt (c));
      if (x0 < 0.)
        x0 = 0.;
      a0 = a + x0;
      beta = (gsl_sf_lngamma (a0)
              + gsl_sf_lngamma (b)
              - gsl_sf_lngamma (a0 + b));
      temp = gsl_sf_beta_inc (a0, b, x);
      gx = exp (a0 * log (x) + b * log (1. - x) - beta - log (a0));
      if (a0 >= a)
        q = exp (-c + x0 * log (c)) - gsl_sf_lngamma (x0 + 1.);
      else
        q = exp (-c);
      ax = q * temp;
      sumq = 1. - q;
      sum = ax;

      iter = 0;
      do 
        {
          iter++;
          temp -= gx;
          gx = x * (a + b + iter - 1.) * gx / (a + iter);
          q *= c / iter;
          sumq -= q;
          ax = temp * q;
          sum += ax;

          err_bound = (temp - gx) * sumq;
        }
      while (iter < iter_max && err_bound > err_max);
      
      return sum;
    }
  else 
    {
      /* Algorithm AS 310. */
      double m, m_sqrt;
      int iter, iter_lower, iter_upper, iter1, iter2, j;
      double t, q, r, psum, beta, s1, gx, fx, temp, ftemp, t0, s0, sum, s;
      double err_bound;
      double err_max = 2 * DBL_EPSILON;
      
      iter = 0;
      
      m = floor (c + .5);
      m_sqrt = sqrt (m);
      iter_lower = m - 5. * m_sqrt;
      iter_upper = m + 5. * m_sqrt;
      
      t = -c + m * log (c) - gsl_sf_lngamma (m + 1.);
      q = exp (t);
      r = q;
      psum = q;
      beta = (gsl_sf_lngamma (a + m)
              + gsl_sf_lngamma (b)
              - gsl_sf_lngamma (a + m + b));
      s1 = (a + m) * log (x) + b * log (1. - x) - log (a + m) - beta;
      fx = gx = exp (s1);
      ftemp = temp = gsl_sf_beta_inc (a + m, b, x);
      iter++;
      sum = q * temp;
      iter1 = m;

      while (iter1 >= iter_lower && q >= err_max) 
        {
          q = q * iter1 / c;
          iter++;
          gx = (a + iter1) / (x * (a + b + iter1 - 1.)) * gx;
          iter1--;
          temp += gx;
          psum += q;
          sum += q * temp;
        }

      t0 = (gsl_sf_lngamma (a + b)
            - gsl_sf_lngamma (a + 1.)
            - gsl_sf_lngamma (b));
      s0 = a * log (x) + b * log (1. - x);

      s = 0.;
      for (j = 0; j < iter1; j++) 
        {
          double t1;
          s += exp (t0 + s0 + j * log (x));
          t1 = log (a + b + j) - log (a + 1. + j) + t0;
          t0 = t1;
        }

      err_bound = (1. - gsl_sf_gamma_inc_P (iter1, c)) * (temp + s);
      q = r;
      temp = ftemp;
      gx = fx;
      iter2 = m;
      for (;;) 
        {
          double ebd = err_bound + (1. - psum) * temp;
          if (ebd < err_max || iter >= iter_upper)
            break;

          iter2++;
          iter++;
          q = q * c / iter2;
          psum += q;
          temp -= gx;
          gx = x * (a + b + iter2 - 1.) / (a + iter2) * gx;
          sum += q * temp;
        }

      return sum;
    }
}

double
cdf_bvnor (double x0, double x1, double r) 
{
  double z = x0 * x0 - 2. * r * x0 * x1 + x1 * x1;
  return exp (-z / (2. * (1 - r * r))) * (2. * M_PI * sqrt (1 - r * r));
}

double
idf_fdist (double P, double df1, double df2) 
{
  double temp = gslextras_cdf_beta_Pinv (P, df1 / 2, df2 / 2);
  return temp * df2 / ((1. - temp) * df1);
}

/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
 *
 *  This program is free software; you can redistribute it and/or
 *  modify
 *  it under the terms of the GNU General Public License as
 *  published by
 *  the Free Software Foundation; either version 2 of the
 *  License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be
 *  useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty
 *  of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301 USA.
 */

/* Returns the density of the noncentral beta distribution with
   noncentrality parameter LAMBDA. */
double
npdf_beta (double x, double a, double b, double lambda) 
{
  if (lambda < 0. || a <= 0. || b <= 0.)
    return SYSMIS;
  else if (lambda == 0.)
    return gsl_ran_beta_pdf (x, a, b);
  else 
    {
      double max_error = 2 * DBL_EPSILON;
      int max_iter = 200;
      double term = gsl_ran_beta_pdf (x, a, b);
      double lambda2 = 0.5 * lambda;
      double weight = exp (-lambda2);
      double sum = weight * term;
      double psum = weight;
      int k;
      for (k = 1; k <= max_iter && 1 - psum < max_error; k++) { 
        weight *= lambda2 / k;
        term *= x * (a + b) / a;
        sum += weight * term;
        psum += weight;
        a += 1;
      } 
      return sum;
    }
}
