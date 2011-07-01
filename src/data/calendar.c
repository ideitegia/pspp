/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2007, 2008, 2010, 2011 Free Software Foundation, Inc.

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

#include "data/calendar.h"

#include <assert.h>
#include <stdbool.h>

#include "data/settings.h"
#include "data/val-type.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* 14 Oct 1582. */
#define EPOCH (-577734)

/* Calculates and returns floor(a/b) for integer b > 0. */
static int
floor_div (int a, int b)
{
  assert (b > 0);
  return (a >= 0 ? a : a - b + 1) / b;
}

/* Calculates floor(a/b) and the corresponding remainder and
   stores them into *Q and *R. */
static void
floor_divmod (int a, int b, int *q, int *r)
{
  *q = floor_div (a, b);
  *r = a - b * *q;
}

/* Returns true if Y is a leap year, false otherwise. */
static bool
is_leap_year (int y)
{
  return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
}

static int
raw_gregorian_to_offset (int y, int m, int d)
{
  return (EPOCH - 1
          + 365 * (y - 1)
          + floor_div (y - 1, 4)
          - floor_div (y - 1, 100)
          + floor_div (y - 1, 400)
          + floor_div (367 * m - 362, 12)
          + (m <= 2 ? 0 : (m >= 2 && is_leap_year (y) ? -1 : -2))
          + d);
}

/* Returns the number of days from 14 Oct 1582 to (Y,M,D) in the
   Gregorian calendar.  Returns SYSMIS for dates before 14 Oct
   1582. */
double
calendar_gregorian_to_offset (int y, int m, int d, char **errorp)
{
  /* Normalize year. */
  if (y >= 0 && y < 100)
    {
      int epoch = settings_get_epoch ();
      int century = epoch / 100 + (y < epoch % 100);
      y += century * 100;
    }

  /* Normalize month. */
  if (m < 1 || m > 12)
    {
      if (m == 0)
        {
          y--;
          m = 12;
        }
      else if (m == 13)
        {
          y++;
          m = 1;
        }
      else
        {
          if (errorp != NULL)
            *errorp = xasprintf (_("Month %d is not in acceptable range of "
                                   "0 to 13."), m);
          return SYSMIS;
        }
    }

  /* Normalize day. */
  if (d < 0 || d > 31)
    {
      if (errorp != NULL)
        *errorp = xasprintf (_("Day %d is not in acceptable range of "
                               "0 to 31."), d);
      return SYSMIS;
    }

  /* Validate date. */
  if (y < 1582 || (y == 1582 && (m < 10 || (m == 10 && d < 15))))
    {
      if (errorp != NULL)
        *errorp = xasprintf (_("Date %04d-%d-%d is before the earliest "
                               "acceptable date of 1582-10-15."), y, m, d);
      return SYSMIS;
    }

  /* Calculate offset. */
  if (errorp != NULL)
    *errorp = NULL;
  return raw_gregorian_to_offset (y, m, d);
}

/* Returns the number of days in the given YEAR from January 1 up
   to (but not including) the first day of MONTH. */
static int
cum_month_days (int year, int month)
{
  static const int cum_month_days[12] =
    {
      0,
      31, /* Jan */
      31 + 28, /* Feb */
      31 + 28 + 31, /* Mar */
      31 + 28 + 31 + 30, /* Apr */
      31 + 28 + 31 + 30 + 31, /* May */
      31 + 28 + 31 + 30 + 31 + 30, /* Jun */
      31 + 28 + 31 + 30 + 31 + 30 + 31, /* Jul */
      31 + 28 + 31 + 30 + 31 + 30 + 31 + 31, /* Aug */
      31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30, /* Sep */
      31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31, /* Oct */
      31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31 + 30, /* Nov */
    };

  assert (month >= 1 && month <= 12);
  return cum_month_days[month - 1] + (month >= 3 && is_leap_year (year));
}

/* Takes a count of days from 14 Oct 1582 and returns the
   Gregorian calendar year it is in.  Dates both before and after
   the epoch are supported. */
int
calendar_offset_to_year (int ofs)
{
  int d0;
  int n400, d1;
  int n100, d2;
  int n4, d3;
  int n1;
  int y;

  d0 = ofs - EPOCH;
  floor_divmod (d0, 365 * 400 + 100 - 3, &n400, &d1);
  floor_divmod (d1, 365 * 100 + 25 - 1, &n100, &d2);
  floor_divmod (d2, 365 * 4 + 1, &n4, &d3);
  n1 = floor_div (d3, 365);
  y = 400 * n400 + 100 * n100 + 4 * n4 + n1;
  if (n100 != 4 && n1 != 4)
    y++;

  return y;
}

/* Takes a count of days from 14 Oct 1582 and translates it into
   a Gregorian calendar date in (*Y,*M,*D).  Also stores the
   year-relative day number into *YD.  Dates both before and
   after the epoch are supported. */
void
calendar_offset_to_gregorian (int ofs, int *y, int *m, int *d, int *yd)
{
  int year = *y = calendar_offset_to_year (ofs);
  int january1 = raw_gregorian_to_offset (year, 1, 1);
  int yday = *yd = ofs - january1 + 1;
  int march1 = january1 + cum_month_days (year, 3);
  int correction = ofs < march1 ? 0 : (is_leap_year (year) ? 1 : 2);
  int month = *m = (12 * (yday - 1 + correction) + 373) / 367;
  *d = yday - cum_month_days (year, month);
}

/* Takes a count of days from 14 Oct 1582 and returns the 1-based
   year-relative day number, that is, the number of days from the
   beginning of the year. */
int
calendar_offset_to_yday (int ofs)
{
  int year = calendar_offset_to_year (ofs);
  int january1 = raw_gregorian_to_offset (year, 1, 1);
  int yday = ofs - january1 + 1;
  return yday;
}

/* Takes a count of days from 14 Oct 1582 and returns the
   corresponding weekday 1...7, with 1=Sunday. */
int
calendar_offset_to_wday (int ofs)
{
  int wday = (ofs - EPOCH + 1) % 7 + 1;
  if (wday <= 0)
    wday += 7;
  return wday;
}

/* Takes a count of days from 14 Oct 1582 and returns the month
   it is in. */
int
calendar_offset_to_month (int ofs)
{
  int y, m, d, yd;
  calendar_offset_to_gregorian (ofs, &y, &m, &d, &yd);
  return m;
}

/* Takes a count of days from 14 Oct 1582 and returns the
   corresponding day of the month. */
int
calendar_offset_to_mday (int ofs)
{
  int y, m, d, yd;
  calendar_offset_to_gregorian (ofs, &y, &m, &d, &yd);
  return d;
}

/* Returns the number of days in the specified month. */
int
calendar_days_in_month (int y, int m)
{
  static const int days_per_month[12]
    = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

  assert (m >= 1 && m <= 12);
  return m == 2 && is_leap_year (y) ? 29 : days_per_month[m - 1];
}
