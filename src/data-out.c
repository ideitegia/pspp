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
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <float.h>
#include <stdlib.h>
#include <time.h>
#include "approx.h"
#include "error.h"
#include "format.h"
#include "julcal/julcal.h"
#include "magic.h"
#include "misc.h"
#include "misc.h"
#include "settings.h"
#include "str.h"
#include "var.h"

#undef DEBUGGING
/*#define DEBUGGING 1*/
#include "debug-print.h"

/* In older versions, numbers got their trailing zeros stripped.
   Newer versions leave them on when there's room.  Comment this next
   line out for retro styling. */
#define NEW_STYLE 1

/* Public functions. */

typedef int convert_func (char *, const struct fmt_spec *,
			  const union value *);

static convert_func convert_F, convert_N, convert_E, convert_F_plus;
static convert_func convert_Z, convert_A, convert_AHEX, convert_IB;
static convert_func convert_P, convert_PIB, convert_PIBHEX, convert_PK;
static convert_func convert_RB, convert_RBHEX, convert_CCx, convert_date;
static convert_func convert_time, convert_WKDAY, convert_MONTH;
static convert_func try_F;

/* Converts binary value V into printable form in string S according
   to format specification FP.  The string as written has exactly
   FP->W characters.  It is not null-terminated.  Returns 1 on
   success, 0 on failure. */
int
data_out (char *s, const struct fmt_spec *fp, const union value *v)
{
  union value tmp_val;
  
  {
    int cat = formats[fp->type].cat;
    if ((cat & FCAT_BLANKS_SYSMIS) && v->f == SYSMIS)
      {
	memset (s, ' ', fp->w);
	s[fp->w - fp->d - 1] = '.';
	return 1;
      }
    if ((cat & FCAT_SHIFT_DECIMAL) && v->f != SYSMIS && fp->d)
      {
	tmp_val.f = v->f * pow (10.0, fp->d);
	v = &tmp_val;
      }
  }
  
  {
    static convert_func *const handlers[FMT_NUMBER_OF_FORMATS] =
      {
	convert_F, convert_N, convert_E, convert_F_plus,
	convert_F_plus, convert_F_plus, convert_F_plus,
	convert_Z, convert_A, convert_AHEX, convert_IB, convert_P, convert_PIB,
	convert_PIBHEX, convert_PK, convert_RB, convert_RBHEX,
	convert_CCx, convert_CCx, convert_CCx, convert_CCx, convert_CCx,
	convert_date, convert_date, convert_date, convert_date, convert_date,
	convert_date, convert_date, convert_date, convert_date,
	convert_time, convert_time,
	convert_WKDAY, convert_MONTH,
      };

    return handlers[fp->type] (s, fp, v);
  }
}

/* Converts V into S in F format with width W and D decimal places,
   then deletes trailing zeros.  S is not null-terminated. */
void
num_to_string (double v, char *s, int w, int d)
{
  /* Dummies to pass to convert_F. */
  union value val;
  struct fmt_spec f;

#if !NEW_STYLE
  /* Pointer to `.' in S. */
  char *decp;

  /* Pointer to `E' in S. */
  char *expp;

  /* Number of characters to delete. */
  int n = 0;
#endif

  f.w = w;
  f.d = d;
  val.f = v;

  /* Cut out the jokers. */
  if (!finite (v))
    {
      char temp[9];
      int len;

      if (isnan (v))
	{
	  memcpy (temp, "NaN", 3);
	  len = 3;
	}
      else if (isinf (v))
	{
	  memcpy (temp, "+Infinity", 9);
	  if (v < 0)
	    temp[0] = '-';
	  len = 9;
	}
      else
	{
	  memcpy (temp, _("Unknown"), 7);
	  len = 7;
	}
      if (w > len)
	{
	  int pad = w - len;
	  memset (s, ' ', pad);
	  s += pad;
	  w -= pad;
	}
      memcpy (s, temp, w);
      return;
    }

  try_F (s, &f, &val);

#if !NEW_STYLE
  decp = memchr (s, set_decimal, w);
  if (!decp)
    return;

  /* If there's an `E' we can only delete 0s before the E. */
  expp = memchr (s, 'E', w);
  if (expp)
    {
      while (expp[-n - 1] == '0')
	n++;
      if (expp[-n - 1] == set_decimal)
	n++;
      memmove (&s[n], s, expp - s - n);
      memset (s, ' ', n);
      return;
    }

  /* Otherwise delete all trailing 0s. */
  n++;
  while (s[w - n] == '0')
    n++;
  if (s[w - n] != set_decimal)
    {
      /* Avoid stripping `.0' to `'. */
      if (w == n || !isdigit ((unsigned char) s[w - n - 1]))
	n -= 2;
    }
  else
    n--;
  memmove (&s[n], s, w - n);
  memset (s, ' ', n);
#endif
}

/* Main conversion functions. */

static void insert_commas (char *dst, const char *src,
			   const struct fmt_spec *fp);
static int year4 (int year);
static int try_CCx (char *s, const struct fmt_spec *fp, double v);

#if FLT_RADIX!=2
#error Write your own floating-point output routines.
#endif

/* PORTME:

   Some of the routines in this file are likely very specific to
   base-2 representation of floating-point numbers, most notably the
   routines that use frexp() or ldexp().  These attempt to extract
   individual digits by setting the base-2 exponent and
   multiplying/dividing by powers of 2.  In base-2 numeration systems,
   this just nudges the exponent up or down, but in base-10 floating
   point, such multiplications/division can cause catastrophic loss of
   precision.

   The author has never personally used a machine that didn't use
   binary floating point formats, so he is unwilling, and perhaps
   unable, to code around this "problem".  */

/* Converts a number between 0 and 15 inclusive to a `hexit'
   [0-9A-F]. */
#define MAKE_HEXIT(X) ("0123456789ABCDEF"[X])

/* Table of powers of 10. */
static const double power10[] =
  {
    0,	/* Not used. */
    1e01, 1e02, 1e03, 1e04, 1e05, 1e06, 1e07, 1e08, 1e09, 1e10,
    1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20,
    1e21, 1e22, 1e23, 1e24, 1e25, 1e26, 1e27, 1e28, 1e29, 1e30,
    1e31, 1e32, 1e33, 1e34, 1e35, 1e36, 1e37, 1e38, 1e39, 1e40,
  };

/* Handles F format. */
static int
convert_F (char *dst, const struct fmt_spec *fp, const union value *v)
{
  if (!try_F (dst, fp, v))
    convert_E (dst, fp, v);
  return 1;
}

/* Handles N format. */
static int
convert_N (char *dst, const struct fmt_spec *fp, const union value *v)
{
  double d = floor (v->f);

  if (d < 0 || d == SYSMIS)
    {
      msg (ME, _("The N output format cannot be used to output a "
		 "negative number or the system-missing value."));
      return 0;
    }
  
  if (d < power10[fp->w])
    {
      char buf[128];
      sprintf (buf, "%0*.0f", fp->w, v->f);
      memcpy (dst, buf, fp->w);
    }
  else
    memset (dst, '*', fp->w);

  return 1;
}

/* Handles E format.  Also operates as fallback for some other
   formats. */
static int
convert_E (char *dst, const struct fmt_spec *fp, const union value *v)
{
  /* Temporary buffer. */
  char buf[128];
  
  /* Ranged number of decimal places. */
  int d;

  /* Check that the format is width enough.
     Although PSPP generally checks this, convert_E() can be called as
     a fallback from other formats which do not check. */
  if (fp->w < 6)
    {
      memset (dst, '*', fp->w);
      return 1;
    }

  /* Put decimal places in usable range. */
  d = min (fp->d, fp->w - 6);
  if (v->f < 0)
    d--;
  if (d < 0)
    d = 0;
  sprintf (buf, "%*.*E", fp->w, d, v->f);

  /* What we do here is force the exponent part to have four
     characters whenever possible.  That is, 1.00E+99 is okay (`E+99')
     but 1.00E+100 (`E+100') must be coerced to 1.00+100 (`+100').  On
     the other hand, 1.00E1000 (`E+100') cannot be canonicalized.
     Note that ANSI C guarantees at least two digits in the
     exponent. */
  if (fabs (v->f) > 1e99)
    {
      /* Pointer to the `E' in buf. */
      char *cp;

      cp = strchr (buf, 'E');
      if (cp)
	{
	  /* Exponent better not be bigger than an int. */
	  int exp = atoi (cp + 1); 

	  if (abs (exp) > 99 && abs (exp) < 1000)
	    {
	      /* Shift everything left one place: 1.00e+100 -> 1.00+100. */
	      cp[0] = cp[1];
	      cp[1] = cp[2];
	      cp[2] = cp[3];
	      cp[3] = cp[4];
	    }
	  else if (abs (exp) >= 1000)
	    memset (buf, '*', fp->w);
	}
    }

  /* The C locale always uses a period `.' as a decimal point.
     Translate to comma if necessary. */
  if ((set_decimal == ',' && fp->type != FMT_DOT)
      || (set_decimal == '.' && fp->type == FMT_DOT))
    {
      char *cp = strchr (buf, '.');
      if (cp)
	*cp = ',';
    }

  memcpy (dst, buf, fp->w);
  return 1;
}

/* Handles COMMA, DOT, DOLLAR, and PCT formats. */
static int
convert_F_plus (char *dst, const struct fmt_spec *fp, const union value *v)
{
  char buf[40];
  
  if (try_F (buf, fp, v))
    insert_commas (dst, buf, fp);
  else
    convert_E (dst, fp, v);

  return 1;
}

static int
convert_Z (char *dst, const struct fmt_spec *fp, const union value *v)
{
  static int warned = 0;

  if (!warned)
    {
      msg (MW, _("Quality of zoned decimal (Z) output format code is "
		 "suspect.  Check your results, report bugs to author."));
      warned = 1;
    }

  if (v->f == SYSMIS)
    {
      msg (ME, _("The system-missing value cannot be output as a zoned "
		 "decimal number."));
      return 0;
    }
  
  {
    char buf[41];
    double d;
    int i;
    
    d = fabs (floor (v->f));
    if (d >= power10[fp->w])
      {
	msg (ME, _("Number %g too big to fit in field with format Z%d.%d."),
	     v->f, fp->w, fp->d);
	return 0;
      }

    sprintf (buf, "%*.0f", fp->w, v->f);
    for (i = 0; i < fp->w; i++)
      dst[i] = (buf[i] - '0') | 0xf0;
    if (v->f < 0)
      dst[fp->w - 1] &= 0xdf;
  }

  return 1;
}

static int
convert_A (char *dst, const struct fmt_spec *fp, const union value *v)
{
  memcpy (dst, v->c, fp->w);
  return 1;
}

static int
convert_AHEX (char *dst, const struct fmt_spec *fp, const union value *v)
{
  int i;

  for (i = 0; i < fp->w / 2; i++)
    {
      ((unsigned char *) dst)[i * 2] = MAKE_HEXIT ((v->c[i]) >> 4);
      ((unsigned char *) dst)[i * 2 + 1] = MAKE_HEXIT ((v->c[i]) & 0xf);
    }

  return 1;
}

static int
convert_IB (char *dst, const struct fmt_spec *fp, const union value *v)
{
  /* Strategy: Basically the same as convert_PIBHEX() but with base
     256. Then it's necessary to negate the two's-complement result if
     v->f is negative. */

  /* Used for constructing the two's-complement result. */
  unsigned temp[8];

  /* Fraction (mantissa). */
  double frac;

  /* Exponent. */
  int exp;

  /* Difference between exponent and (-8*fp->w-1). */
  int diff;

  /* Counter. */
  int i;

  /* Make the exponent (-8*fp->w-1). */
  frac = frexp (fabs (v->f), &exp);
  diff = exp - (-8 * fp->w - 1);
  exp -= diff;
  frac *= ldexp (1.0, diff);

  /* Extract each base-256 digit. */
  for (i = 0; i < fp->w; i++)
    {
      modf (frac, &frac);
      frac *= 256.0;
      temp[i] = floor (frac);
    }

  /* Perform two's-complement negation if v->f is negative. */
  if (v->f < 0)
    {
      /* Perform NOT operation. */
      for (i = 0; i < fp->w; i++)
	temp[i] = ~temp[i];
      /* Add 1 to the whole number. */
      for (i = fp->w - 1; i >= 0; i--)
	{
	  temp[i]++;
	  if (temp[i])
	    break;
	}
    }
  memcpy (dst, temp, fp->w);
#ifndef WORDS_BIGENDIAN
  mm_reverse (dst, fp->w);
#endif

  return 1;
}

static int
convert_P (char *dst, const struct fmt_spec *fp, const union value *v)
{
  /* Buffer for v->f*2-1 characters + a decimal point if library is
     not quite compliant + a null. */
  char buf[17];

  /* Counter. */
  int i;

  /* Main extraction. */
  sprintf (buf, "%0*.0f", fp->w * 2 - 1, floor (fabs (v->f)));

  for (i = 0; i < fp->w; i++)
    ((unsigned char *) dst)[i]
      = ((buf[i * 2] - '0') << 4) + buf[i * 2 + 1] - '0';

  /* Set sign. */
  dst[fp->w - 1] &= 0xf0;
  if (v->f >= 0.0)
    dst[fp->w - 1] |= 0xf;
  else
    dst[fp->w - 1] |= 0xd;

  return 1;
}

static int
convert_PIB (char *dst, const struct fmt_spec *fp, const union value *v)
{
  /* Strategy: Basically the same as convert_IB(). */

  /* Fraction (mantissa). */
  double frac;

  /* Exponent. */
  int exp;

  /* Difference between exponent and (-8*fp->w). */
  int diff;

  /* Counter. */
  int i;

  /* Make the exponent (-8*fp->w). */
  frac = frexp (fabs (v->f), &exp);
  diff = exp - (-8 * fp->w);
  exp -= diff;
  frac *= ldexp (1.0, diff);

  /* Extract each base-256 digit. */
  for (i = 0; i < fp->w; i++)
    {
      modf (frac, &frac);
      frac *= 256.0;
      ((unsigned char *) dst)[i] = floor (frac);
    }
#ifndef WORDS_BIGENDIAN
  mm_reverse (dst, fp->w);
#endif

  return 1;
}

static int
convert_PIBHEX (char *dst, const struct fmt_spec *fp, const union value *v)
{
  /* Strategy: Use frexp() to create a normalized result (but mostly
     to find the base-2 exponent), then change the base-2 exponent to
     (-4*fp->w) using multiplication and division by powers of two.
     Extract each hexit by multiplying by 16. */

  /* Fraction (mantissa). */
  double frac;

  /* Exponent. */
  int exp;

  /* Difference between exponent and (-4*fp->w). */
  int diff;

  /* Counter. */
  int i;

  /* Make the exponent (-4*fp->w). */
  frac = frexp (fabs (v->f), &exp);
  diff = exp - (-4 * fp->w);
  exp -= diff;
  frac *= ldexp (1.0, diff);

  /* Extract each hexit. */
  for (i = 0; i < fp->w; i++)
    {
      modf (frac, &frac);
      frac *= 16.0;
      *dst++ = MAKE_HEXIT ((int) floor (frac));
    }

  return 1;
}

static int
convert_PK (char *dst, const struct fmt_spec *fp, const union value *v)
{
  /* Buffer for v->f*2 characters + a decimal point if library is not
     quite compliant + a null. */
  char buf[18];

  /* Counter. */
  int i;

  /* Main extraction. */
  sprintf (buf, "%0*.0f", fp->w * 2, floor (fabs (v->f)));

  for (i = 0; i < fp->w; i++)
    ((unsigned char *) dst)[i]
      = ((buf[i * 2] - '0') << 4) + buf[i * 2 + 1] - '0';

  return 1;
}

static int
convert_RB (char *dst, const struct fmt_spec *fp, const union value *v)
{
  union
    {
      double d;
      char c[8];
    }
  u;

  u.d = v->f;
  memcpy (dst, u.c, fp->w);

  return 1;
}

static int
convert_RBHEX (char *dst, const struct fmt_spec *fp, const union value *v)
{
  union
  {
    double d;
    char c[8];
  }
  u;

  int i;

  u.d = v->f;
  for (i = 0; i < fp->w / 2; i++)
    {
      *dst++ = MAKE_HEXIT (u.c[i] >> 4);
      *dst++ = MAKE_HEXIT (u.c[i] & 15);
    }

  return 1;
}

static int
convert_CCx (char *dst, const struct fmt_spec *fp, const union value *v)
{
  if (try_CCx (dst, fp, v->f))
    return 1;
  else
    {
      struct fmt_spec f;
      
      f.type = FMT_COMMA;
      f.w = fp->w;
      f.d = fp->d;
  
      return convert_F (dst, &f, v);
    }
}

static int
convert_date (char *dst, const struct fmt_spec *fp, const union value *v)
{
  static const char *months[12] =
    {
      "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
      "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
    };

  char buf[64] = {0};
  int month, day, year;

  julian_to_calendar (v->f / 86400., &year, &month, &day);
  switch (fp->type)
    {
    case FMT_DATE:
      if (fp->w >= 11)
	sprintf (buf, "%02d-%s-%04d", day, months[month - 1], year);
      else
	sprintf (buf, "%02d-%s-%02d", day, months[month - 1], year % 100);
      break;
    case FMT_EDATE:
      if (fp->w >= 10)
	sprintf (buf, "%02d.%02d.%04d", day, month, year);
      else
	sprintf (buf, "%02d.%02d.%02d", day, month, year % 100);
      break;
    case FMT_SDATE:
      if (fp->w >= 10)
	sprintf (buf, "%04d/%02d/%02d", year, month, day);
      else
	sprintf (buf, "%02d/%02d/%02d", year % 100, month, day);
      break;
    case FMT_ADATE:
      if (fp->w >= 10)
	sprintf (buf, "%02d/%02d/%04d", month, day, year);
      else
	sprintf (buf, "%02d/%02d/%02d", month, day, year % 100);
      break;
    case FMT_JDATE:
      {
	int yday = (v->f / 86400.) - calendar_to_julian (year, 1, 1) + 1;
	
	if (fp->w >= 7)
	  {
	    if (year4 (year))
	      sprintf (buf, "%04d%03d", year, yday);
	  }
	else
	  sprintf (buf, "%02d%03d", year % 100, yday);
	break;
      }
    case FMT_QYR:
      if (fp->w >= 8)
	sprintf (buf, "%d Q% 04d", (month - 1) / 3 + 1, year);
      else
	sprintf (buf, "%d Q% 02d", (month - 1) / 3 + 1, year % 100);
      break;
    case FMT_MOYR:
      if (fp->w >= 8)
	sprintf (buf, "%s% 04d", months[month - 1], year);
      else
	sprintf (buf, "%s% 02d", months[month - 1], year % 100);
      break;
    case FMT_WKYR:
      {
	int yday = (v->f / 86400.) - calendar_to_julian (year, 1, 1) + 1;
	
	if (fp->w >= 10)
	  sprintf (buf, "%02d WK% 04d", (yday - 1) / 7 + 1, year);
	else
	  sprintf (buf, "%02d WK% 02d", (yday - 1) / 7 + 1, year % 100);
      }
      break;
    case FMT_DATETIME:
      {
	char *cp;

	cp = spprintf (buf, "%02d-%s-%04d %02d:%02d",
		       day, months[month - 1], year,
		       (int) fmod (floor (v->f / 60. / 60.), 24.),
		       (int) fmod (floor (v->f / 60.), 60.));
	if (fp->w >= 20)
	  {
	    int w, d;

	    if (fp->w >= 22 && fp->d > 0)
	      {
		d = min (fp->d, fp->w - 21);
		w = 3 + d;
	      }
	    else
	      {
		w = 2;
		d = 0;
	      }

	    cp = spprintf (cp, ":%0*.*f", w, d, fmod (v->f, 60.));
	  }
      }
      break;
#if __CHECKER__
    case 42000:
      assert (0);
#endif
    default:
      assert (0);
    }

  if (buf[0] == 0)
    return 0;
  st_bare_pad_copy (dst, buf, fp->w);
  return 1;
}

static int
convert_time (char *dst, const struct fmt_spec *fp, const union value *v)
{
  char temp_buf[40];
  char *cp;

  double time;
  int width;

  if (fabs (v->f) > 1e20)
    {
      msg (ME, _("Time value %g too large in magnitude to convert to "
	   "alphanumeric time."), v->f);
      return 0;
    }

  time = v->f;
  width = fp->w;
  cp = temp_buf;
  if (time < 0)
    *cp++ = '-', time = -time;
  if (fp->type == FMT_DTIME)
    {
      double days = floor (time / 60. / 60. / 24.);
      cp = spprintf (temp_buf, "%02.0f ", days);
      time = time - days * 60. * 60. * 24.;
      width -= 3;
    }
  else
    cp = temp_buf;

  cp = spprintf (cp, "%02.0f:%02.0f",
		 fmod (floor (time / 60. / 60.), 24.),
		 fmod (floor (time / 60.), 60.));

  if (width >= 8)
    {
      int w, d;

      if (width >= 10 && fp->d >= 0 && fp->d != 0)
	d = min (fp->d, width - 9), w = 3 + d;
      else
	w = 2, d = 0;

      cp = spprintf (cp, ":%0*.*f", w, d, fmod (time, 60.));
    }
  st_bare_pad_copy (dst, temp_buf, fp->w);

  return 1;
}

static int
convert_WKDAY (char *dst, const struct fmt_spec *fp, const union value *v)
{
  static const char *weekdays[7] =
    {
      "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY",
      "THURSDAY", "FRIDAY", "SATURDAY",
    };

  int x = v->f;

  if (x < 1 || x > 7)
    {
      msg (ME, _("Weekday index %d does not lie between 1 and 7."), x);
      return 0;
    }
  st_bare_pad_copy (dst, weekdays[x - 1], fp->w);

  return 1;
}

static int
convert_MONTH (char *dst, const struct fmt_spec *fp, const union value *v)
{
  static const char *months[12] =
    {
      "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
      "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER",
    };

  int x = v->f;

  if (x < 1 || x > 12)
    {
      msg (ME, _("Month index %d does not lie between 1 and 12."), x);
      return 0;
    }
  
  st_bare_pad_copy (dst, months[x - 1], fp->w);

  return 1;
}

/* Helper functions. */

/* Copies SRC to DST, inserting commas and dollar signs as appropriate
   for format spec *FP.  */
static void
insert_commas (char *dst, const char *src, const struct fmt_spec *fp)
{
  /* Number of leading spaces in the number.  This is the amount of
     room we have for inserting commas and dollar signs. */
  int n_spaces;

  /* Number of digits before the decimal point.  This is used to
     determine the Number of commas to insert. */
  int n_digits;

  /* Number of commas to insert. */
  int n_commas;

  /* Number of items ,%$ to insert. */
  int n_items;

  /* Number of n_items items not to use for commas. */
  int n_reserved;

  /* Digit iterator. */
  int i;

  /* Source pointer. */
  const char *sp;

  /* Count spaces and digits. */
  sp = src;
  while (sp < src + fp->w && *sp == ' ')
    sp++;
  n_spaces = sp - src;
  sp = src + n_spaces;
  if (*sp == '-')
    sp++;
  n_digits = 0;
  while (sp + n_digits < src + fp->w && isdigit ((unsigned char) sp[n_digits]))
    n_digits++;
  n_commas = (n_digits - 1) / 3;
  n_items = n_commas + (fp->type == FMT_DOLLAR || fp->type == FMT_PCT);

  /* Check whether we have enough space to do insertions. */
  if (!n_spaces || !n_items)
    {
      memcpy (dst, src, fp->w);
      return;
    }
  if (n_items > n_spaces)
    {
      n_items -= n_commas;
      if (!n_items)
	{
	  memcpy (dst, src, fp->w);
	  return;
	}
    }

  /* Put spaces at the beginning if there's extra room. */
  if (n_spaces > n_items)
    {
      memset (dst, ' ', n_spaces - n_items);
      dst += n_spaces - n_items;
    }

  /* Insert $ and reserve space for %. */
  n_reserved = 0;
  if (fp->type == FMT_DOLLAR)
    {
      *dst++ = '$';
      n_items--;
    }
  else if (fp->type == FMT_PCT)
    n_reserved = 1;

  /* Copy negative sign and digits, inserting commas. */
  if (sp - src > n_spaces)
    *dst++ = '-';
  for (i = n_digits; i; i--)
    {
      if (i % 3 == 0 && n_digits > i && n_items > n_reserved)
	{
	  n_items--;
	  *dst++ = fp->type == FMT_COMMA ? set_grouping : set_decimal;
	}
      *dst++ = *sp++;
    }

  /* Copy decimal places and insert % if necessary. */
  memcpy (dst, sp, fp->w - (sp - src));
  if (fp->type == FMT_PCT && n_items > 0)
    dst[fp->w - (sp - src)] = '%';
}

/* Returns 1 if YEAR (i.e., 1987) can be represented in four digits, 0
   otherwise. */
static int
year4 (int year)
{
  if (year >= 1 && year <= 9999)
    return 1;
  msg (ME, _("Year %d cannot be represented in four digits for "
	     "output formatting purposes."), year);
  return 0;
}

static int
try_CCx (char *dst, const struct fmt_spec *fp, double v)
{
  struct set_cust_currency *cc = &set_cc[fp->type - FMT_CCA];

  struct fmt_spec f;

  char buf[64];
  char buf2[64];
  char *cp;

  /* Determine length available, decimal character for number
     proper. */
  f.type = cc->decimal == set_decimal ? FMT_COMMA : FMT_DOT;
  f.w = fp->w - strlen (cc->prefix) - strlen (cc->suffix);
  if (v < 0)
    f.w -= strlen (cc->neg_prefix) + strlen (cc->neg_suffix) - 1;
  else
    /* Convert -0 to +0. */
    v = fabs (v);
  f.d = fp->d;

  if (f.w <= 0)
    return 0;

  /* There's room for all that currency crap.  Let's do the F
     conversion first. */
  if (!convert_F (buf, &f, (union value *) &v) || *buf == '*')
    return 0;
  insert_commas (buf2, buf, &f);

  /* Postprocess back into buf. */
  cp = buf;
  if (v < 0)
    cp = stpcpy (cp, cc->neg_prefix);
  cp = stpcpy (cp, cc->prefix);
  {
    char *bp = buf2;
    while (*bp == ' ')
      bp++;

    assert ((v >= 0) ^ (*bp == '-'));
    if (v < 0)
      bp++;

    memcpy (cp, bp, f.w - (bp - buf2));
    cp += f.w - (bp - buf2);
  }
  cp = stpcpy (cp, cc->suffix);
  if (v < 0)
    cp = stpcpy (cp, cc->neg_suffix);

  /* Copy into dst. */
  assert (cp - buf <= fp->w);
  if (cp - buf < fp->w)
    {
      memcpy (&dst[fp->w - (cp - buf)], buf, cp - buf);
      memset (dst, ' ', fp->w - (cp - buf));
    }
  else
    memcpy (dst, buf, fp->w);

  return 1;
}

/* This routine relies on the underlying implementation of sprintf:

   If the number has a magnitude 1e40 or greater, then we needn't
   bother with it, since it's guaranteed to need processing in
   scientific notation.

   Otherwise, do a binary search for the base-10 magnitude of the
   thing.  log10() is not accurate enough, and the alternatives are
   frightful.  Besides, we never need as many as 6 (pairs of)
   comparisons.  The algorithm used for searching is Knuth's Algorithm
   6.2.1C (Uniform binary search).

   DON'T CHANGE ANYTHING HERE UNLESS YOU'VE THOUGHT ABOUT IT FOR A
   LONG TIME!  The rest of the program is heavily dependent on
   specific properties of this routine's output.  LOG ALL CHANGES! */
static int
try_F (char *dst, const struct fmt_spec *fp, const union value *value)
{
  /* This is the DELTA array from Knuth.
     DELTA[j] = floor((40+2**(j-1))/(2**j)). */
  static const int delta[8] =
  {
    0, (40 + 1) / 2, (40 + 2) / 4, (40 + 4) / 8, (40 + 8) / 16,
    (40 + 16) / 32, (40 + 32) / 64, (40 + 64) / 128,
  };

  /* The number of digits in floor(v), including sign.  This is `i'
     from Knuth. */
  int n_int = (40 + 1) / 2;

  /* Used to step through delta[].  This is `j' from Knuth. */
  int j = 2;

  /* Value. */
  double v = value->f;

  /* Magnitude of v.  This is `K' from Knuth. */
  double mag;

  /* Number of characters for the fractional part, including the
     decimal point. */
  int n_dec;

  /* Pointer into buf used for formatting. */
  char *cp;

  /* Used to count characters formatted by nsprintf(). */
  int n;

  /* Temporary buffer. */
  char buf[128];

  /* First check for infinities and NaNs.  12/13/96. */
  if (!finite (v))
    {
      n = nsprintf (buf, "%f", v);
      if (n > fp->w)
	memset (buf, '*', fp->w);
      else if (n < fp->w)
	{
	  memmove (&buf[fp->w - n], buf, n);
	  memset (buf, ' ', fp->w - n);
	}
      memcpy (dst, buf, fp->w);
      return 1;
    }

  /* Then check for radically out-of-range values. */
  mag = fabs (v);
  if (mag >= power10[fp->w])
    return 0;

  if (mag < 1.0)
    {
      n_int = 0;

      /* Avoid printing `-.000'. 7/6/96. */
      if (approx_eq (v, 0.0))
	v = 0.0;
    }
  else
    /* Now perform a `uniform binary search' based on the tables
       power10[] and delta[].  After this step, nint is the number of
       digits in floor(v), including any sign.  */
    for (;;)
      {
	if (mag >= power10[n_int])	/* Should this be approx_ge()? */
	  {
	    assert (delta[j]);
	    n_int += delta[j++];
	  }
	else if (mag < power10[n_int - 1])
	  {
	    assert (delta[j]);
	    n_int -= delta[j++];
	  }
	else
	  break;
      }

  /* If we have any decimal places, then there is a decimal point,
     too. */
  n_dec = fp->d;
  if (n_dec)
    n_dec++;

  /* 1/10/96: If there aren't any digits at all, add one.  This occurs
     only when fabs(v) < 1.0. */
  if (n_int + n_dec == 0)
    n_int++;

  /* Give space for a minus sign.  Moved 1/10/96. */
  if (v < 0)
    n_int++;

  /* Normally we only go through the loop once; occasionally twice.
     Three times or more indicates a very serious bug somewhere. */
  for (;;)
    {
      /* Check out the total length of the string. */
      cp = buf;
      if (n_int + n_dec > fp->w)
	{
	  /* The string is too long.  Let's see what can be done. */
	  if (n_int <= fp->w)
	    /* If we can, just reduce the number of decimal places. */
	    n_dec = fp->w - n_int;
	  else
	    return 0;
	}
      else if (n_int + n_dec < fp->w)
	{
	  /* The string is too short.  Left-pad with spaces. */
	  int n_spaces = fp->w - n_int - n_dec;
	  memset (cp, ' ', n_spaces);
	  cp += n_spaces;
	}

      /* Finally, format the number. */
      if (n_dec)
	n = nsprintf (cp, "%.*f", n_dec - 1, v);
      else
	n = nsprintf (cp, "%.0f", v);

      /* If v is positive and its magnitude is less than 1...  */
      if (n_int == 0)
	{
	  if (*cp == '0')
	    {
	      /* The value rounds to `.###'. */
	      memmove (cp, &cp[1], n - 1);
	      n--;
	    }
	  else
	    {
	      /* The value rounds to `1.###'. */
	      n_int = 1;
	      continue;
	    }
	}
      /* Else if v is negative and its magnitude is less than 1...  */
      else if (v < 0 && n_int == 1)
	{
	  if (cp[1] == '0')
	    {
	      /* The value rounds to `-.###'. */
	      memmove (&cp[1], &cp[2], n - 2);
	      n--;
	    }
	  else
	    {
	      /* The value rounds to `-1.###'. */
	      n_int = 2;
	      continue;
	    }
	}

      /* Check for a correct number of digits & decimal places & stuff.
         This is just a desperation check.  Hopefully it won't fail too
         often, because then we have to run through the whole loop again:
         sprintf() is not a fast operation with floating-points! */
      if (n == n_int + n_dec)
	{
	  /* Convert periods `.' to commas `,' for our foreign friends. */
	  if ((set_decimal == ',' && fp->type != FMT_DOT)
	      || (set_decimal == '.' && fp->type == FMT_DOT))
	    {
	      cp = strchr (cp, '.');
	      if (cp)
		*cp = ',';
	    }

	  memcpy (dst, buf, fp->w);
	  return 1;
	}

      n_int = n - n_dec; /* FIXME?  Need an idiot check on resulting n_int? */
    }
}
