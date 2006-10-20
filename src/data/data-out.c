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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>
#include <libpspp/message.h>
#include <ctype.h>
#include <math.h>
#include <float.h>
#include <stdlib.h>
#include <time.h>
#include "calendar.h"
#include <libpspp/assertion.h>
#include <libpspp/message.h>
#include "format.h"
#include <libpspp/magic.h>
#include <libpspp/misc.h>
#include <libpspp/misc.h>
#include "settings.h"
#include <libpspp/str.h>
#include "variable.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Public functions. */

typedef int numeric_converter (char *, const struct fmt_spec *, double);
static numeric_converter convert_F, convert_N, convert_E, convert_F_plus;
static numeric_converter convert_Z, convert_IB, convert_P, convert_PIB;
static numeric_converter convert_PIBHEX, convert_PK, convert_RB;
static numeric_converter convert_RBHEX, convert_CCx, convert_date;
static numeric_converter convert_time, convert_WKDAY, convert_MONTH;

static numeric_converter try_F, convert_infinite;

typedef int string_converter (char *, const struct fmt_spec *, const char *);
static string_converter convert_A, convert_AHEX;

/* Converts binary value V into printable form in the exactly
   FP->W character in buffer S according to format specification
   FP.  No null terminator is appended to the buffer.  */
bool
data_out (char *s, const struct fmt_spec *fp, const union value *v)
{
  int cat = formats[fp->type].cat;
  int ok;

  assert (check_output_specifier (fp, 0));
  if (!(cat & FCAT_STRING)) 
    {
      /* Numeric formatting. */
      double number = v->f;

      /* Handle SYSMIS turning into blanks. */
      if ((cat & FCAT_BLANKS_SYSMIS) && number == SYSMIS)
        {
          memset (s, ' ', fp->w);
          s[fp->w - fp->d - 1] = '.';
          return true;
        }

      /* Handle decimal shift. */
      if ((cat & FCAT_SHIFT_DECIMAL) && number != SYSMIS && fp->d)
        number *= pow (10.0, fp->d);

      switch (fp->type) 
        {
        case FMT_F:
          ok = convert_F (s, fp, number);
          break;

        case FMT_N:
          ok = convert_N (s, fp, number);
          break;

        case FMT_E:
          ok = convert_E (s, fp, number);
          break;

        case FMT_COMMA: case FMT_DOT: case FMT_DOLLAR: case FMT_PCT:
          ok = convert_F_plus (s, fp, number);
          break;

        case FMT_Z:
          ok = convert_Z (s, fp, number);
          break;

        case FMT_A:
          NOT_REACHED ();

        case FMT_AHEX:
          NOT_REACHED ();

        case FMT_IB:
          ok = convert_IB (s, fp, number);
          break;

        case FMT_P:
          ok = convert_P (s, fp, number);
          break;

        case FMT_PIB:
          ok = convert_PIB (s, fp, number);
          break;

        case FMT_PIBHEX:
          ok = convert_PIBHEX (s, fp, number);
          break;

        case FMT_PK:
          ok = convert_PK (s, fp, number);
          break;

        case FMT_RB:
          ok = convert_RB (s, fp, number);
          break;

        case FMT_RBHEX:
          ok = convert_RBHEX (s, fp, number);
          break;

        case FMT_CCA: case FMT_CCB: case FMT_CCC: case FMT_CCD: case FMT_CCE:
          ok = convert_CCx (s, fp, number);
          break;

        case FMT_DATE: case FMT_EDATE: case FMT_SDATE: case FMT_ADATE:
        case FMT_JDATE: case FMT_QYR: case FMT_MOYR: case FMT_WKYR:
        case FMT_DATETIME: 
          ok = convert_date (s, fp, number);
          break;

        case FMT_TIME: case FMT_DTIME:
          ok = convert_time (s, fp, number);
          break;

        case FMT_WKDAY:
          ok = convert_WKDAY (s, fp, number);
          break;

        case FMT_MONTH:
          ok = convert_MONTH (s, fp, number);
          break;

        default:
          NOT_REACHED ();
        }
    }
  else 
    {
      /* String formatting. */
      const char *string = v->s;

      switch (fp->type) 
        {
        case FMT_A:
          ok = convert_A (s, fp, string);
          break;

        case FMT_AHEX:
          ok = convert_AHEX (s, fp, string);
          break;

        default:
          NOT_REACHED ();
        }
    }

  /* Error handling. */
  if (!ok)
    strncpy (s, "ERROR", fp->w);
  
  return ok;
}

/* Converts V into S in F format with width W and D decimal places,
   then deletes trailing zeros.  S is not null-terminated. */
void
num_to_string (double v, char *s, int w, int d)
{
  struct fmt_spec f = make_output_format (FMT_F, w, d);
  convert_F (s, &f, v);
}

/* Main conversion functions. */

static void insert_commas (char *dst, const char *src,
			   const struct fmt_spec *fp);
static int year4 (int year);
static int try_CCx (char *s, const struct fmt_spec *fp, double v);

#if FLT_RADIX!=2
#error Write your own floating-point output routines.
#endif

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
convert_F (char *dst, const struct fmt_spec *fp, double number)
{
  if (!try_F (dst, fp, number))
    convert_E (dst, fp, number);
  return 1;
}

/* Handles N format. */
static int
convert_N (char *dst, const struct fmt_spec *fp, double number)
{
  double d = floor (number);

  if (d < 0 || d == SYSMIS)
    {
      msg (ME, _("The N output format cannot be used to output a "
		 "negative number or the system-missing value."));
      return 0;
    }
  
  if (d < power10[fp->w])
    {
      char buf[128];
      sprintf (buf, "%0*.0f", fp->w, number);
      memcpy (dst, buf, fp->w);
    }
  else
    memset (dst, '*', fp->w);

  return 1;
}

/* Handles E format.  Also operates as fallback for some other
   formats. */
static int
convert_E (char *dst, const struct fmt_spec *fp, double number)
{
  /* Temporary buffer. */
  char buf[128];
  
  /* Ranged number of decimal places. */
  int d;

  if (!finite (number))
    return convert_infinite (dst, fp, number);

  /* Check that the format is wide enough.
     Although PSPP generally checks this, convert_E() can be called as
     a fallback from other formats which do not check. */
  if (fp->w < 6)
    {
      memset (dst, '*', fp->w);
      return 1;
    }

  /* Put decimal places in usable range. */
  d = min (fp->d, fp->w - 6);
  if (number < 0)
    d--;
  if (d < 0)
    d = 0;
  sprintf (buf, "%*.*E", fp->w, d, number);

  /* What we do here is force the exponent part to have four
     characters whenever possible.  That is, 1.00E+99 is okay (`E+99')
     but 1.00E+100 (`E+100') must be coerced to 1.00+100 (`+100').  On
     the other hand, 1.00E1000 (`E+100') cannot be canonicalized.
     Note that ANSI C guarantees at least two digits in the
     exponent. */
  if (fabs (number) > 1e99)
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
  if ((get_decimal() == ',' && fp->type != FMT_DOT)
      || (get_decimal() == '.' && fp->type == FMT_DOT))
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
convert_F_plus (char *dst, const struct fmt_spec *fp, double number)
{
  char buf[40];
  
  if (try_F (buf, fp, number))
    insert_commas (dst, buf, fp);
  else
    convert_E (dst, fp, number);

  return 1;
}

static int
convert_Z (char *dst, const struct fmt_spec *fp, double number)
{
  static bool warned = false;

  if (!warned)
    {
      msg (MW, 
	_("Quality of zoned decimal (Z) output format code is "
          "suspect.  Check your results. Report bugs to %s."),
	PACKAGE_BUGREPORT);
      warned = 1;
    }

  if (number == SYSMIS)
    {
      msg (ME, _("The system-missing value cannot be output as a zoned "
		 "decimal number."));
      return 0;
    }
  
  {
    char buf[41];
    double d;
    int i;
    
    d = fabs (floor (number));
    if (d >= power10[fp->w])
      {
	msg (ME, _("Number %g too big to fit in field with format Z%d.%d."),
	     number, fp->w, fp->d);
	return 0;
      }

    sprintf (buf, "%*.0f", fp->w, number);
    for (i = 0; i < fp->w; i++)
      dst[i] = (buf[i] - '0') | 0xf0;
    if (number < 0)
      dst[fp->w - 1] &= 0xdf;
  }

  return 1;
}

static int
convert_A (char *dst, const struct fmt_spec *fp, const char *string)
{
  memcpy(dst, string, fp->w);
  return 1;
}

static int
convert_AHEX (char *dst, const struct fmt_spec *fp, const char *string)
{
  int i;

  for (i = 0; i < fp->w / 2; i++)
    {
      *dst++ = MAKE_HEXIT ((string[i]) >> 4);
      *dst++ = MAKE_HEXIT ((string[i]) & 0xf);
    }

  return 1;
}

static int
convert_IB (char *dst, const struct fmt_spec *fp, double number)
{
  /* Strategy: Basically the same as convert_PIBHEX() but with
     base 256. Then negate the two's-complement result if number
     is negative. */

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
  frac = frexp (fabs (number), &exp);
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

  /* Perform two's-complement negation if number is negative. */
  if (number < 0)
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
  buf_reverse (dst, fp->w);
#endif

  return 1;
}

static int
convert_P (char *dst, const struct fmt_spec *fp, double number)
{
  /* Buffer for fp->w*2-1 characters + a decimal point if library is
     not quite compliant + a null. */
  char buf[17];

  /* Counter. */
  int i;

  /* Main extraction. */
  sprintf (buf, "%0*.0f", fp->w * 2 - 1, floor (fabs (number)));

  for (i = 0; i < fp->w; i++)
    ((unsigned char *) dst)[i]
      = ((buf[i * 2] - '0') << 4) + buf[i * 2 + 1] - '0';

  /* Set sign. */
  dst[fp->w - 1] &= 0xf0;
  if (number >= 0.0)
    dst[fp->w - 1] |= 0xf;
  else
    dst[fp->w - 1] |= 0xd;

  return 1;
}

static int
convert_PIB (char *dst, const struct fmt_spec *fp, double number)
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
  frac = frexp (fabs (number), &exp);
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
  buf_reverse (dst, fp->w);
#endif

  return 1;
}

static int
convert_PIBHEX (char *dst, const struct fmt_spec *fp, double number)
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
  frac = frexp (fabs (number), &exp);
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
convert_PK (char *dst, const struct fmt_spec *fp, double number)
{
  /* Buffer for fp->w*2 characters + a decimal point if library is not
     quite compliant + a null. */
  char buf[18];

  /* Counter. */
  int i;

  /* Main extraction. */
  sprintf (buf, "%0*.0f", fp->w * 2, floor (fabs (number)));

  for (i = 0; i < fp->w; i++)
    ((unsigned char *) dst)[i]
      = ((buf[i * 2] - '0') << 4) + buf[i * 2 + 1] - '0';

  return 1;
}

static int
convert_RB (char *dst, const struct fmt_spec *fp, double number)
{
  union
    {
      double d;
      char c[8];
    }
  u;

  u.d = number;
  memcpy (dst, u.c, fp->w);

  return 1;
}

static int
convert_RBHEX (char *dst, const struct fmt_spec *fp, double number)
{
  union
  {
    double d;
    char c[8];
  }
  u;

  int i;

  u.d = number;
  for (i = 0; i < fp->w / 2; i++)
    {
      *dst++ = MAKE_HEXIT (u.c[i] >> 4);
      *dst++ = MAKE_HEXIT (u.c[i] & 15);
    }

  return 1;
}

static int
convert_CCx (char *dst, const struct fmt_spec *fp, double number)
{
  if (try_CCx (dst, fp, number))
    return 1;
  else
    {
      struct fmt_spec f;
      
      f.type = FMT_COMMA;
      f.w = fp->w;
      f.d = fp->d;
  
      return convert_F_plus (dst, &f, number);
    }
}

static int
convert_date (char *dst, const struct fmt_spec *fp, double number)
{
  static const char *months[12] =
    {
      "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
      "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
    };

  char buf[64] = {0};
  int ofs = number / 86400.;
  int month, day, year;

  if (ofs < 1)
    return 0;

  calendar_offset_to_gregorian (ofs, &year, &month, &day);
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
        int yday = calendar_offset_to_yday (ofs);
	
        if (fp->w < 7)
          sprintf (buf, "%02d%03d", year % 100, yday); 
        else if (year4 (year))
          sprintf (buf, "%04d%03d", year, yday);
        else
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
	int yday = calendar_offset_to_yday (ofs);
	
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
		       (int) fmod (floor (number / 60. / 60.), 24.),
		       (int) fmod (floor (number / 60.), 60.));
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

	    cp = spprintf (cp, ":%0*.*f", w, d, fmod (number, 60.));
	  }
      }
      break;
    default:
      NOT_REACHED ();
    }

  if (buf[0] == 0)
    return 0;
  buf_copy_str_rpad (dst, fp->w, buf);
  return 1;
}

static int
convert_time (char *dst, const struct fmt_spec *fp, double number)
{
  char temp_buf[40];
  char *cp;

  double time;
  int width;

  if (fabs (number) > 1e20)
    {
      msg (ME, _("Time value %g too large in magnitude to convert to "
	   "alphanumeric time."), number);
      return 0;
    }

  time = number;
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
  buf_copy_str_rpad (dst, fp->w, temp_buf);

  return 1;
}

static int
convert_WKDAY (char *dst, const struct fmt_spec *fp, double wkday)
{
  static const char *weekdays[7] =
    {
      "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY",
      "THURSDAY", "FRIDAY", "SATURDAY",
    };

  if (wkday < 1 || wkday > 7)
    {
      msg (ME, _("Weekday index %f does not lie between 1 and 7."),
           (double) wkday);
      return 0;
    }
  buf_copy_str_rpad (dst, fp->w, weekdays[(int) wkday - 1]);

  return 1;
}

static int
convert_MONTH (char *dst, const struct fmt_spec *fp, double month)
{
  static const char *months[12] =
    {
      "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
      "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER",
    };

  if (month < 1 || month > 12)
    {
      msg (ME, _("Month index %f does not lie between 1 and 12."),
           month);
      return 0;
    }
  
  buf_copy_str_rpad (dst, fp->w, months[(int) month - 1]);

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
	  *dst++ = fp->type == FMT_COMMA ? get_grouping() : get_decimal();
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
try_CCx (char *dst, const struct fmt_spec *fp, double number)
{
  const struct custom_currency *cc = get_cc(fp->type - FMT_CCA);

  struct fmt_spec f;

  char buf[64];
  char buf2[64];
  char *cp;

  /* Determine length available, decimal character for number
     proper. */
  f.type = cc->decimal == get_decimal () ? FMT_COMMA : FMT_DOT;
  f.w = fp->w - strlen (cc->prefix) - strlen (cc->suffix);
  if (number < 0)
    f.w -= strlen (cc->neg_prefix) + strlen (cc->neg_suffix) - 1;
  else
    /* Convert -0 to +0. */
    number = fabs (number);
  f.d = fp->d;

  if (f.w <= 0)
    return 0;

  /* There's room for all that currency crap.  Let's do the F
     conversion first. */
  if (!convert_F (buf, &f, number) || *buf == '*')
    return 0;
  insert_commas (buf2, buf, &f);

  /* Postprocess back into buf. */
  cp = buf;
  if (number < 0)
    cp = stpcpy (cp, cc->neg_prefix);
  cp = stpcpy (cp, cc->prefix);
  {
    char *bp = buf2;
    while (*bp == ' ')
      bp++;

    assert ((number >= 0) ^ (*bp == '-'));
    if (number < 0)
      bp++;

    memcpy (cp, bp, f.w - (bp - buf2));
    cp += f.w - (bp - buf2);
  }
  cp = stpcpy (cp, cc->suffix);
  if (number < 0)
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

static int
format_and_round (char *dst, double number, const struct fmt_spec *fp,
                  int decimals);

/* Tries to format NUMBER into DST as the F format specified in
   *FP.  Return true if successful, false on failure. */
static int
try_F (char *dst, const struct fmt_spec *fp, double number)
{
  assert (fp->w <= 40);
  if (finite (number)) 
    {
      if (fabs (number) < power10[fp->w])
        {
          /* The value may fit in the field. */
          if (fp->d == 0) 
            {
              /* There are no decimal places, so there's no way
                 that the value can be shortened.  Either it fits
                 or it doesn't. */
              char buf[41];
              sprintf (buf, "%*.0f", fp->w, number);
              if (strlen (buf) <= fp->w) 
                {
                  buf_copy_str_lpad (dst, fp->w, buf);
                  return true; 
                }
              else 
                return false;
            }
          else 
            {
              /* First try to format it with 2 extra decimal
                 places.  This gives us a good chance of not
                 needing even more decimal places, but it also
                 avoids wasting too much time formatting more
                 decimal places on the first try. */
              int result = format_and_round (dst, number, fp, fp->d + 2);

              if (result >= 0)
                return result;

              /* 2 extra decimal places weren't enough to
                 correctly round.  Try again with the maximum
                 number of places. */
              return format_and_round (dst, number, fp, LDBL_DIG + 1);
            }
        }
      else 
        {
          /* The value is too big to fit in the field. */
          return false;
        }
    }
  else
    return convert_infinite (dst, fp, number);
}

/* Tries to compose NUMBER into DST in format FP by first
   formatting it with DECIMALS decimal places, then rounding off
   to as many decimal places will fit or the number specified in
   FP, whichever is fewer.

   Returns 1 if conversion succeeds, 0 if this try at conversion
   failed and so will any other tries (because the integer part
   of the number is too long), or -1 if this try failed but
   another with higher DECIMALS might succeed (because we'd be
   able to properly round). */
static int
format_and_round (char *dst, double number, const struct fmt_spec *fp,
                  int decimals)
{
  /* Number of characters before the decimal point,
     which includes digits and possibly a minus sign. */
  int predot_chars;

  /* Number of digits in the output fraction,
     which may be smaller than fp->d if there's not enough room. */
  int fraction_digits;

  /* Points to last digit that will remain in the fraction after
     rounding. */
  char *final_frac_dig;

  /* Round up? */
  bool round_up;
  
  char buf[128];
  
  assert (decimals > fp->d);
  if (decimals > LDBL_DIG)
    decimals = LDBL_DIG + 1;

  sprintf (buf, "%.*f", decimals, number);

  /* Omit integer part if it's 0. */
  if (!memcmp (buf, "0.", 2))
    memmove (buf, buf + 1, strlen (buf));
  else if (!memcmp (buf, "-0.", 3))
    memmove (buf + 1, buf + 2, strlen (buf + 1));

  predot_chars = strcspn (buf, ".");
  if (predot_chars > fp->w) 
    {
      /* Can't possibly fit. */
      return 0; 
    }
  else if (predot_chars == fp->w)
    {
      /* Exact fit for integer part and sign. */
      memcpy (dst, buf, fp->w);
      return 1;
    }
  else if (predot_chars + 1 == fp->w) 
    {
      /* There's room for the decimal point, but not for any
         digits of the fraction.
         Right-justify the integer part and sign. */
      dst[0] = ' ';
      memcpy (dst + 1, buf, fp->w - 1);
      return 1;
    }

  /* It looks like we have room for at least one digit of the
     fraction.  Figure out how many. */
  fraction_digits = fp->w - predot_chars - 1;
  if (fraction_digits > fp->d)
    fraction_digits = fp->d;
  final_frac_dig = buf + predot_chars + fraction_digits;

  /* Decide rounding direction and truncate string. */
  if (final_frac_dig[1] == '5'
      && strspn (final_frac_dig + 2, "0") == strlen (final_frac_dig + 2)) 
    {
      /* Exactly 1/2. */
      if (decimals <= LDBL_DIG)
        {
          /* Don't have enough fractional digits to know which way to
             round.  We can format with more decimal places, so go
             around again. */
          return -1;
        }
      else 
        {
          /* We used up all our fractional digits and still don't
             know.  Round to even. */
          round_up = (final_frac_dig[0] - '0') % 2 != 0;
        }
    }
  else
    round_up = final_frac_dig[1] >= '5';
  final_frac_dig[1] = '\0';

  /* Do rounding. */
  if (round_up) 
    {
      char *cp = final_frac_dig;
      for (;;) 
        {
          if (*cp >= '0' && *cp <= '8')
            {
              (*cp)++;
              break; 
            }
          else if (*cp == '9') 
            *cp = '0';
          else
            assert (*cp == '.');

          if (cp == buf || *--cp == '-')
            {
              size_t length;
              
              /* Tried to go past the leftmost digit.  Insert a 1. */
              memmove (cp + 1, cp, strlen (cp) + 1);
              *cp = '1';

              length = strlen (buf);
              if (length > fp->w) 
                {
                  /* Inserting the `1' overflowed our space.
                     Drop a decimal place. */
                  buf[--length] = '\0';

                  /* If that was the last decimal place, drop the
                     decimal point too. */
                  if (buf[length - 1] == '.')
                    buf[length - 1] = '\0';
                }
              
              break;
            }
        }
    }

  /* Omit `-' if value output is zero. */
  if (buf[0] == '-' && buf[strspn (buf, "-.0")] == '\0')
    memmove (buf, buf + 1, strlen (buf));

  buf_copy_str_lpad (dst, fp->w, buf);
  return 1;
}

/* Formats non-finite NUMBER into DST according to the width
   given in FP. */
static int
convert_infinite (char *dst, const struct fmt_spec *fp, double number)
{
  assert (!finite (number));
  
  if (fp->w >= 3)
    {
      const char *s;

      if (isnan (number))
        s = "NaN";
      else if (isinf (number))
        s = number > 0 ? "+Infinity" : "-Infinity";
      else
        s = "Unknown";

      buf_copy_str_lpad (dst, fp->w, s);
    }
  else 
    memset (dst, '*', fp->w);

  return true;
}
