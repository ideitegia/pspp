/* PSPP - a program for statistical analysis.
   Copyright (C) 2008 Free Software Foundation, Inc.

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
#include "helpers.h"
#include <gsl/gsl_roots.h>
#include <gsl/gsl_sf.h>
#include <libpspp/assertion.h>
#include <libpspp/pool.h>
#include "private.h"

const struct substring empty_string = {NULL, 0};

static void
expr_error (void *aux UNUSED, const char *format, ...)
{
  struct msg m;
  va_list args;

  m.category = MSG_C_SYNTAX;
  m.severity = MSG_S_ERROR;
  va_start (args, format);
  m.text = xvasprintf (format, args);
  m.where.file_name = NULL;
  m.where.line_number = 0;
  va_end (args);

  msg_emit (&m);
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

/* A date unit. */
enum date_unit
  {
    DATE_YEARS,
    DATE_QUARTERS,
    DATE_MONTHS,
    DATE_WEEKS,
    DATE_DAYS,
    DATE_HOURS,
    DATE_MINUTES,
    DATE_SECONDS
  };

/* Stores in *UNIT the unit whose name is NAME.
   Return success. */
static enum date_unit
recognize_unit (struct substring name, enum date_unit *unit)
{
  struct unit_name
    {
      enum date_unit unit;
      const struct substring name;
    };
  static const struct unit_name unit_names[] =
    {
      { DATE_YEARS, SS_LITERAL_INITIALIZER ("years") },
      { DATE_QUARTERS, SS_LITERAL_INITIALIZER ("quarters") },
      { DATE_MONTHS, SS_LITERAL_INITIALIZER ("months") },
      { DATE_WEEKS, SS_LITERAL_INITIALIZER ("weeks") },
      { DATE_DAYS, SS_LITERAL_INITIALIZER ("days") },
      { DATE_HOURS, SS_LITERAL_INITIALIZER ("hours") },
      { DATE_MINUTES, SS_LITERAL_INITIALIZER ("minutes") },
      { DATE_SECONDS, SS_LITERAL_INITIALIZER ("seconds") },
    };
  const int unit_name_cnt = sizeof unit_names / sizeof *unit_names;

  const struct unit_name *un;

  for (un = unit_names; un < &unit_names[unit_name_cnt]; un++)
    if (ss_equals_case (un->name, name))
      {
        *unit = un->unit;
        return true;
      }

  /* TRANSLATORS: Don't translate the the actual unit names `weeks', `days' etc
	They must remain in their original English. */
  msg (SE, _("Unrecognized date unit `%.*s'.  "
             "Valid date units are `years', `quarters', `months', "
             "`weeks', `days', `hours', `minutes', and `seconds'."),
       (int) ss_length (name), ss_data (name));
  return false;
}

/* Returns the number of whole years from DATE1 to DATE2,
   where a year is defined as the same or later month, day, and
   time of day. */
static int
year_diff (double date1, double date2)
{
  int y1, m1, d1, yd1;
  int y2, m2, d2, yd2;
  int diff;

  assert (date2 >= date1);
  calendar_offset_to_gregorian (date1 / DAY_S, &y1, &m1, &d1, &yd1);
  calendar_offset_to_gregorian (date2 / DAY_S, &y2, &m2, &d2, &yd2);

  diff = y2 - y1;
  if (diff > 0)
    {
      int yd1 = 32 * m1 + d1;
      int yd2 = 32 * m2 + d2;
      if (yd2 < yd1
          || (yd2 == yd1 && fmod (date2, DAY_S) < fmod (date1, DAY_S)))
        diff--;
    }
  return diff;
}

/* Returns the number of whole months from DATE1 to DATE2,
   where a month is defined as the same or later day and time of
   day. */
static int
month_diff (double date1, double date2)
{
  int y1, m1, d1, yd1;
  int y2, m2, d2, yd2;
  int diff;

  assert (date2 >= date1);
  calendar_offset_to_gregorian (date1 / DAY_S, &y1, &m1, &d1, &yd1);
  calendar_offset_to_gregorian (date2 / DAY_S, &y2, &m2, &d2, &yd2);

  diff = ((y2 * 12) + m2) - ((y1 * 12) + m1);
  if (diff > 0
      && (d2 < d1
          || (d2 == d1 && fmod (date2, DAY_S) < fmod (date1, DAY_S))))
    diff--;
  return diff;
}

/* Returns the number of whole quarter from DATE1 to DATE2,
   where a quarter is defined as three months. */
static int
quarter_diff (double date1, double date2)
{
  return month_diff (date1, date2) / 3;
}

/* Returns the number of seconds in the given UNIT. */
static int
date_unit_duration (enum date_unit unit)
{
  switch (unit)
    {
    case DATE_WEEKS:
      return WEEK_S;

    case DATE_DAYS:
      return DAY_S;

    case DATE_HOURS:
      return H_S;

    case DATE_MINUTES:
      return MIN_S;

    case DATE_SECONDS:
      return 1;

    default:
      NOT_REACHED ();
    }
}

/* Returns the span from DATE1 to DATE2 in terms of UNIT_NAME. */
double
expr_date_difference (double date1, double date2, struct substring unit_name)
{
  enum date_unit unit;

  if (!recognize_unit (unit_name, &unit))
    return SYSMIS;

  switch (unit)
    {
    case DATE_YEARS:
      return (date2 >= date1
              ? year_diff (date1, date2)
              : -year_diff (date2, date1));

    case DATE_QUARTERS:
      return (date2 >= date1
              ? quarter_diff (date1, date2)
              : -quarter_diff (date2, date1));

    case DATE_MONTHS:
      return (date2 >= date1
              ? month_diff (date1, date2)
              : -month_diff (date2, date1));

    case DATE_WEEKS:
    case DATE_DAYS:
    case DATE_HOURS:
    case DATE_MINUTES:
    case DATE_SECONDS:
      return trunc ((date2 - date1) / date_unit_duration (unit));
    }

  NOT_REACHED ();
}

/* How to deal with days out of range for a given month. */
enum date_sum_method
  {
    SUM_ROLLOVER,       /* Roll them over to the next month. */
    SUM_CLOSEST         /* Use the last day of the month. */
  };

/* Stores in *METHOD the method whose name is NAME.
   Return success. */
static bool
recognize_method (struct substring method_name, enum date_sum_method *method)
{
  if (ss_equals_case (method_name, ss_cstr ("closest")))
    {
      *method = SUM_CLOSEST;
      return true;
    }
  else if (ss_equals_case (method_name, ss_cstr ("rollover")))
    {
      *method = SUM_ROLLOVER;
      return true;
    }
  else
    {
      msg (SE, _("Invalid DATESUM method.  "
                 "Valid choices are `closest' and `rollover'."));
      return false;
    }
}

/* Returns DATE advanced by the given number of MONTHS, with
   day-of-month overflow resolved using METHOD. */
static double
add_months (double date, int months, enum date_sum_method method)
{
  int y, m, d, yd;
  double output;

  calendar_offset_to_gregorian (date / DAY_S, &y, &m, &d, &yd);
  y += months / 12;
  m += months % 12;
  if (m < 1)
    {
      m += 12;
      y--;
    }
  else if (m > 12)
    {
      m -= 12;
      y++;
    }
  assert (m >= 1 && m <= 12);

  if (method == SUM_CLOSEST && d > calendar_days_in_month (y, m))
    d = calendar_days_in_month (y, m);

  output = calendar_gregorian_to_offset (y, m, d, expr_error, NULL);
  if (output != SYSMIS)
    output = (output * DAY_S) + fmod (date, DAY_S);
  return output;
}

/* Returns DATE advanced by the given QUANTITY of units given in
   UNIT_NAME, with day-of-month overflow resolved using
   METHOD_NAME. */
double
expr_date_sum (double date, double quantity, struct substring unit_name,
               struct substring method_name)
{
  enum date_unit unit;
  enum date_sum_method method;

  if (!recognize_unit (unit_name, &unit)
      || !recognize_method (method_name, &method))
    return SYSMIS;

  switch (unit)
    {
    case DATE_YEARS:
      return add_months (date, trunc (quantity) * 12, method);

    case DATE_QUARTERS:
      return add_months (date, trunc (quantity) * 3, method);

    case DATE_MONTHS:
      return add_months (date, trunc (quantity), method);

    case DATE_WEEKS:
    case DATE_DAYS:
    case DATE_HOURS:
    case DATE_MINUTES:
    case DATE_SECONDS:
      return date + quantity * date_unit_duration (unit);
    }

  NOT_REACHED ();
}

int
compare_string_3way (const struct substring *a, const struct substring *b)
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

struct substring
alloc_string (struct expression *e, size_t length)
{
  struct substring s;
  s.length = length;
  s.string = pool_alloc (e->eval_pool, length);
  return s;
}

struct substring
copy_string (struct expression *e, const char *old, size_t length)
{
  struct substring s = alloc_string (e, length);
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
  double z = pow2 (x0) - 2. * r * x0 * x1 + pow2 (x1);
  return exp (-z / (2. * (1 - r * r))) * (2. * M_PI * sqrt (1 - r * r));
}

double
idf_fdist (double P, double df1, double df2)
{
  double temp = gsl_cdf_beta_Pinv (P, df1 / 2, df2 / 2);
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
