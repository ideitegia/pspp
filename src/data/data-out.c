/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2011, 2012, 2013, 2014 Free Software Foundation, Inc.

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

#include "data/data-out.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistr.h>

#include "data/calendar.h"
#include "data/format.h"
#include "data/settings.h"
#include "data/value.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/float-format.h"
#include "libpspp/i18n.h"
#include "libpspp/integer-format.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"

#include "gl/minmax.h"
#include "gl/c-snprintf.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* A representation of a number that can be quickly rounded to
   any desired number of decimal places (up to a specified
   maximum). */
struct rounder
  {
    char string[64];    /* Magnitude of number with excess precision. */
    int integer_digits; /* Number of digits before decimal point. */
    int leading_nines;  /* Number of `9's or `.'s at start of string. */
    int leading_zeros;  /* Number of `0's or `.'s at start of string. */
    bool negative;      /* Is the number negative? */
  };

static void rounder_init (struct rounder *, double number, int max_decimals);
static int rounder_width (const struct rounder *, int decimals,
                          int *integer_digits, bool *negative);
static void rounder_format (const struct rounder *, int decimals,
                            char *output);

typedef void data_out_converter_func (const union value *,
                                      const struct fmt_spec *,
                                      char *);
#define FMT(NAME, METHOD, IMIN, OMIN, IO, CATEGORY) \
        static data_out_converter_func output_##METHOD;
#include "format.def"

static bool output_decimal (const struct rounder *, const struct fmt_spec *,
                            bool require_affixes, char *);
static bool output_scientific (double, const struct fmt_spec *,
                               bool require_affixes, char *);

static double power10 (int) PURE_FUNCTION;
static double power256 (int) PURE_FUNCTION;

static void output_infinite (double, const struct fmt_spec *, char *);
static void output_missing (const struct fmt_spec *, char *);
static void output_overflow (const struct fmt_spec *, char *);
static bool output_bcd_integer (double, int digits, char *);
static void output_binary_integer (uint64_t, int bytes, enum integer_format,
                                   char *);
static void output_hex (const void *, size_t bytes, char *);


static data_out_converter_func *const converters[FMT_NUMBER_OF_FORMATS] =
    {
#define FMT(NAME, METHOD, IMIN, OMIN, IO, CATEGORY) output_##METHOD,
#include "format.def"
    };

/* Converts the INPUT value, encoded in INPUT_ENCODING, according to format
   specification FORMAT, appending the output to OUTPUT in OUTPUT_ENCODING.
   However, binary formats (FMT_P, FMT_PK, FMT_IB, FMT_PIB, FMT_RB) yield the
   binary results, which may not be properly encoded for OUTPUT_ENCODING.

   VALUE must be the correct width for FORMAT, that is, its width must be
   fmt_var_width(FORMAT).

   INPUT_ENCODING can normally be obtained by calling dict_get_encoding() on
   the dictionary with which INPUT is associated.  ENCODING is only important
   when FORMAT's type is FMT_A. */
void
data_out_recode (const union value *input, const char *input_encoding,
                 const struct fmt_spec *format,
                 struct string *output, const char *output_encoding)
{
  assert (fmt_check_output (format));
  if (format->type == FMT_A)
    {
      char *in = CHAR_CAST (char *, value_str (input, format->w));
      char *out = recode_string (output_encoding, input_encoding,
                                 in, format->w);
      ds_put_cstr (output, out);
      free (out);
    }
  else if (fmt_get_category (format->type) == FMT_CAT_BINARY)
    converters[format->type] (input, format,
                              ds_put_uninit (output, format->w));
  else
    {
      char *utf8_encoded = data_out (input, input_encoding, format);
      char *output_encoded = recode_string (output_encoding, UTF8,
                                            utf8_encoded, -1);
      ds_put_cstr (output, output_encoded);
      free (output_encoded);
      free (utf8_encoded);
    }
}

static char *
binary_to_utf8 (const char *in, struct pool *pool)
{
  uint8_t *out = pool_alloc_unaligned (pool, strlen (in) * 2 + 1);
  uint8_t *p = out;

  while (*in != '\0')
    {
      uint8_t byte = *in++;
      int mblen = u8_uctomb (p, byte, 2);
      assert (mblen > 0);
      p += mblen;
    }
  *p = '\0';

  return CHAR_CAST (char *, out);
}

/* Converts the INPUT value into a UTF-8 encoded string, according to format
   specification FORMAT.

   VALUE must be the correct width for FORMAT, that is, its width must be
   fmt_var_width(FORMAT).

   ENCODING must be the encoding of INPUT.  Normally this can be obtained by
   calling dict_get_encoding() on the dictionary with which INPUT is
   associated.  ENCODING is only important when FORMAT's type is FMT_A.

   The return value is dynamically allocated, and must be freed by the caller.
   If POOL is non-null, then the return value is allocated on that pool.  */
char *
data_out_pool (const union value *input, const char *encoding,
	       const struct fmt_spec *format, struct pool *pool)
{
  assert (fmt_check_output (format));
  if (format->type == FMT_A)
    {
      char *in = CHAR_CAST (char *, value_str (input, format->w));
      return recode_string_pool (UTF8, encoding, in, format->w, pool);
    }
  else if (fmt_get_category (format->type) == FMT_CAT_BINARY)
    {
      char tmp[16];

      assert (format->w + 1 <= sizeof tmp);
      converters[format->type] (input, format, tmp);
      return binary_to_utf8 (tmp, pool);
    }
  else
    {
      const struct fmt_number_style *style = settings_get_style (format->type);
      size_t size = format->w + style->extra_bytes + 1;
      char *output;

      output = pool_alloc_unaligned (pool, size);
      converters[format->type] (input, format, output);
      return output;
    }
}

/* Like data_out_pool(), except that for basic numeric formats (F, COMMA, DOT,
   COLLAR, PCT, E) and custom currency formats are formatted as wide as
   necessary to fully display the selected number of decimal places. */
char *
data_out_stretchy (const union value *input, const char *encoding,
                   const struct fmt_spec *format, struct pool *pool)
{

  if (fmt_get_category (format->type) & (FMT_CAT_BASIC | FMT_CAT_CUSTOM))
    {
      const struct fmt_number_style *style = settings_get_style (format->type);
      struct fmt_spec wide_format;
      char tmp[128];
      size_t size;

      wide_format.type = format->type;
      wide_format.w = 40;
      wide_format.d = format->d;

      size = format->w + style->extra_bytes + 1;
      if (size <= sizeof tmp)
        {
          output_number (input, &wide_format, tmp);
          return pool_strdup (pool, tmp + strspn (tmp, " "));
        }
    }

  return data_out_pool (input, encoding, format, pool);
}

char *
data_out (const union value *input, const char *encoding, const struct fmt_spec *format)
{
  return data_out_pool (input, encoding, format, NULL);
}


/* Main conversion functions. */

/* Outputs F, COMMA, DOT, DOLLAR, PCT, E, CCA, CCB, CCC, CCD, and
   CCE formats. */
static void
output_number (const union value *input, const struct fmt_spec *format,
               char *output)
{
  double number = input->f;

  if (number == SYSMIS)
    output_missing (format, output);
  else if (!isfinite (number))
    output_infinite (number, format, output);
  else
    {
      if (format->type != FMT_E && fabs (number) < 1.5 * power10 (format->w))
        {
          struct rounder r;
          rounder_init (&r, number, format->d);

          if (output_decimal (&r, format, true, output)
              || output_scientific (number, format, true, output)
              || output_decimal (&r, format, false, output))
            return;
        }

      if (!output_scientific (number, format, false, output))
        output_overflow (format, output);
    }
}

/* Outputs N format. */
static void
output_N (const union value *input, const struct fmt_spec *format,
          char *output)
{
  double number = input->f * power10 (format->d);
  if (input->f == SYSMIS || number < 0)
    output_missing (format, output);
  else
    {
      char buf[128];
      number = fabs (round (number));
      if (number < power10 (format->w)
          && c_snprintf (buf, 128, "%0*.0f", format->w, number) == format->w)
        memcpy (output, buf, format->w);
      else
        output_overflow (format, output);
    }

  output[format->w] = '\0';
}

/* Outputs Z format. */
static void
output_Z (const union value *input, const struct fmt_spec *format,
          char *output)
{
  double number = input->f * power10 (format->d);
  char buf[128];
  if (input->f == SYSMIS)
    output_missing (format, output);
  else if (fabs (number) < power10 (format->w)
           && c_snprintf (buf, 128, "%0*.0f", format->w,
                       fabs (round (number))) == format->w)
    {
      if (number < 0 && strspn (buf, "0") < format->w)
        {
          char *p = &buf[format->w - 1];
          *p = "}JKLMNOPQR"[*p - '0'];
        }
      memcpy (output, buf, format->w);
      output[format->w] = '\0';
    }
  else
    output_overflow (format, output);
}

/* Outputs P format. */
static void
output_P (const union value *input, const struct fmt_spec *format,
          char *output)
{
  if (output_bcd_integer (fabs (input->f * power10 (format->d)),
                          format->w * 2 - 1, output)
      && input->f < 0.0)
    output[format->w - 1] |= 0xd;
  else
    output[format->w - 1] |= 0xf;
}

/* Outputs PK format. */
static void
output_PK (const union value *input, const struct fmt_spec *format,
           char *output)
{
  output_bcd_integer (input->f * power10 (format->d), format->w * 2, output);
}

/* Outputs IB format. */
static void
output_IB (const union value *input, const struct fmt_spec *format,
           char *output)
{
  double number = round (input->f * power10 (format->d));
  if (input->f == SYSMIS
      || number >= power256 (format->w) / 2 - 1
      || number < -power256 (format->w) / 2)
    memset (output, 0, format->w);
  else
    {
      uint64_t integer = fabs (number);
      if (number < 0)
        integer = -integer;
      output_binary_integer (integer, format->w,
			     settings_get_output_integer_format (),
                             output);
    }

  output[format->w] = '\0';
}

/* Outputs PIB format. */
static void
output_PIB (const union value *input, const struct fmt_spec *format,
            char *output)
{
  double number = round (input->f * power10 (format->d));
  if (input->f == SYSMIS
      || number < 0 || number >= power256 (format->w))
    memset (output, 0, format->w);
  else
    output_binary_integer (number, format->w,
			   settings_get_output_integer_format (), output);

  output[format->w] = '\0';
}

/* Outputs PIBHEX format. */
static void
output_PIBHEX (const union value *input, const struct fmt_spec *format,
               char *output)
{
  double number = round (input->f);
  if (input->f == SYSMIS)
    output_missing (format, output);
  else if (input->f < 0 || number >= power256 (format->w / 2))
    output_overflow (format, output);
  else
    {
      char tmp[8];
      output_binary_integer (number, format->w / 2, INTEGER_MSB_FIRST, tmp);
      output_hex (tmp, format->w / 2, output);
    }

}

/* Outputs RB format. */
static void
output_RB (const union value *input, const struct fmt_spec *format,
           char *output)
{
  double d = input->f;
  memcpy (output, &d, format->w);

  output[format->w] = '\0';
}

/* Outputs RBHEX format. */
static void
output_RBHEX (const union value *input, const struct fmt_spec *format,
              char *output)
{
  double d = input->f;

  output_hex (&d, format->w / 2, output);
}

/* Outputs DATE, ADATE, EDATE, JDATE, SDATE, QYR, MOYR, WKYR,
   DATETIME, TIME, and DTIME formats. */
static void
output_date (const union value *input, const struct fmt_spec *format,
             char *output)
{
  double number = input->f;
  int year, month, day, yday;

  const char *template = fmt_date_template (format->type, format->w);

  char tmp[64];
  char *p = tmp;

  if (number == SYSMIS)
    goto missing;

  if (fmt_get_category (format->type) == FMT_CAT_DATE)
    {
      if (number <= 0)
        goto missing;
      calendar_offset_to_gregorian (number / 60. / 60. / 24.,
                                    &year, &month, &day, &yday);
      number = fmod (number, 60. * 60. * 24.);
    }
  else
    year = month = day = yday = 0;

  while (*template != '\0')
    {
      int excess_width;

      int ch = *template;
      int count = 1;
      while (template[count] == ch)
        count++;
      template += count;

      switch (ch)
        {
        case 'd':
          if (count < 3)
            p += sprintf (p, "%02d", day);
          else
            p += sprintf (p, "%03d", yday);
          break;
        case 'm':
          if (count < 3)
            p += sprintf (p, "%02d", month);
          else
            {
              static const char *const months[12] =
                {
                  "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                  "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
                };
              p = stpcpy (p, months[month - 1]);
            }
          break;
        case 'y':
          if (count >= 4)
            {
              if (year <= 9999)
                p += sprintf (p, "%04d", year);
              else if (format->type == FMT_DATETIME)
                p = stpcpy (p, "****");
              else
                goto overflow;
            }
          else
            {
              int epoch =  settings_get_epoch ();
              int offset = year - epoch;
              if (offset < 0 || offset > 99)
                goto overflow;
              p += sprintf (p, "%02d", abs (year) % 100);
            }
          break;
        case 'q':
          p += sprintf (p, "%d", (month - 1) / 3 + 1);
          break;
        case 'w':
          p += sprintf (p, "%2d", (yday - 1) / 7 + 1);
          break;
        case 'D':
          if (number < 0)
            *p++ = '-';
          number = fabs (number);
          p += c_snprintf (p, 64, "%*.0f", count, floor (number / 60. / 60. / 24.));
          number = fmod (number, 60. * 60. * 24.);
          break;
        case 'H':
          if (number < 0)
            *p++ = '-';
          number = fabs (number);
          p += c_snprintf (p, 64, "%0*.0f", count, floor (number / 60. / 60.));
          number = fmod (number, 60. * 60.);
          break;
        case 'M':
          p += sprintf (p, "%02d", (int) floor (number / 60.));
          number = fmod (number, 60.);
          excess_width = format->w - (p - tmp);
          if (excess_width < 0)
            goto overflow;
          if (excess_width == 3 || excess_width == 4
              || (excess_width >= 5 && format->d == 0))
            p += sprintf (p, ":%02d", (int) number);
          else if (excess_width >= 5)
            {
              int d = MIN (format->d, excess_width - 4);
              int w = d + 3;
              c_snprintf (p, 64, ":%0*.*f", w, d, number);
	      if (settings_get_decimal_char (FMT_F) != '.')
                {
                  char *cp = strchr (p, '.');
                  if (cp != NULL)
		    *cp = settings_get_decimal_char (FMT_F);
                }
              p += strlen (p);
            }
          goto done;
        default:
          assert (count == 1);
          *p++ = ch;
          break;
        }
    }

 done:
  buf_copy_lpad (output, format->w, tmp, p - tmp, ' ');
  output[format->w] = '\0';
  return;

 overflow:
  output_overflow (format, output);
  return;

 missing:
  output_missing (format, output);
  return;
}

/* Outputs WKDAY format. */
static void
output_WKDAY (const union value *input, const struct fmt_spec *format,
              char *output)
{
  static const char *const weekdays[7] =
    {
      "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY",
      "THURSDAY", "FRIDAY", "SATURDAY",
    };

  if (input->f >= 1 && input->f < 8)
    {
      buf_copy_str_rpad (output, format->w,
                         weekdays[(int) input->f - 1], ' ');
      output[format->w] = '\0';
    }
  else
    {
      if (input->f != SYSMIS)
        msg (ME, _("Weekday number %f is not between 1 and 7."), input->f);
      output_missing (format, output);
    }

}

/* Outputs MONTH format. */
static void
output_MONTH (const union value *input, const struct fmt_spec *format,
              char *output)
{
  static const char *const months[12] =
    {
      "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
      "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER",
    };

  if (input->f >= 1 && input->f < 13)
    {
      buf_copy_str_rpad (output, format->w, months[(int) input->f - 1], ' ');
      output[format->w] = '\0';
    }
  else
    {
      if (input->f != SYSMIS)
        msg (ME, _("Month number %f is not between 1 and 12."), input->f);
      output_missing (format, output);
    }

}

/* Outputs A format. */
static void
output_A (const union value *input UNUSED,
          const struct fmt_spec *format UNUSED, char *output UNUSED)
{
  NOT_REACHED ();
}

/* Outputs AHEX format. */
static void
output_AHEX (const union value *input, const struct fmt_spec *format,
             char *output)
{
  output_hex (value_str (input, format->w), format->w / 2, output);
}

/* Decimal and scientific formatting. */

/* If REQUEST plus the current *WIDTH fits within MAX_WIDTH,
   increments *WIDTH by REQUEST and return true.
   Otherwise returns false without changing *WIDTH. */
static bool
allocate_space (int request, int max_width, int *width)
{
  assert (*width <= max_width);
  if (request + *width <= max_width)
    {
      *width += request;
      return true;
    }
  else
    return false;
}

/* Tries to compose the number represented by R, in the style of
   FORMAT, into OUTPUT.  Returns true if successful, false on
   failure, which occurs if FORMAT's width is too narrow.  If
   REQUIRE_AFFIXES is true, then the prefix and suffix specified
   by FORMAT's style must be included; otherwise, they may be
   omitted to make the number fit. */
static bool
output_decimal (const struct rounder *r, const struct fmt_spec *format,
                bool require_affixes, char *output)
{
  const struct fmt_number_style *style =
    settings_get_style (format->type);

  int decimals;

  for (decimals = format->d; decimals >= 0; decimals--)
    {
      /* Formatted version of magnitude of NUMBER. */
      char magnitude[64];

      /* Number of digits in MAGNITUDE's integer and fractional parts. */
      int integer_digits;

      /* Amount of space within the field width already claimed.
         Initially this is the width of MAGNITUDE, then it is reduced
         in stages as space is allocated to prefixes and suffixes and
         grouping characters. */
      int width;

      /* Include various decorations? */
      bool add_neg_prefix;
      bool add_affixes;
      bool add_grouping;

      /* Position in output. */
      char *p;

      /* Make sure there's room for the number's magnitude, plus
         the negative suffix, plus (if negative) the negative
         prefix. */
      width = rounder_width (r, decimals, &integer_digits, &add_neg_prefix);
      width += style->neg_suffix.width;
      if (add_neg_prefix)
        width += style->neg_prefix.width;
      if (width > format->w)
        continue;

      /* If there's room for the prefix and suffix, allocate
         space.  If the affixes are required, but there's no
         space, give up. */
      add_affixes = allocate_space (fmt_affix_width (style),
                                    format->w, &width);
      if (!add_affixes && require_affixes)
        continue;

      /* Check whether we should include grouping characters.
         We need room for a complete set or we don't insert any at all.
         We don't include grouping characters if decimal places were
         requested but they were all dropped. */
      add_grouping = (style->grouping != 0
                      && integer_digits > 3
                      && (format->d == 0 || decimals > 0)
                      && allocate_space ((integer_digits - 1) / 3,
                                         format->w, &width));

      /* Format the number's magnitude. */
      rounder_format (r, decimals, magnitude);

      /* Assemble number. */
      p = output;
      if (format->w > width)
        p = mempset (p, ' ', format->w - width);
      if (add_neg_prefix)
        p = stpcpy (p, style->neg_prefix.s);
      if (add_affixes)
        p = stpcpy (p, style->prefix.s);
      if (!add_grouping)
        p = mempcpy (p, magnitude, integer_digits);
      else
        {
          int i;
          for (i = 0; i < integer_digits; i++)
            {
              if (i > 0 && (integer_digits - i) % 3 == 0)
                *p++ = style->grouping;
              *p++ = magnitude[i];
            }
        }
      if (decimals > 0)
        {
          *p++ = style->decimal;
          p = mempcpy (p, &magnitude[integer_digits + 1], decimals);
        }
      if (add_affixes)
        p = stpcpy (p, style->suffix.s);
      if (add_neg_prefix)
        p = stpcpy (p, style->neg_suffix.s);
      else
        p = mempset (p, ' ', style->neg_suffix.width);

      assert (p >= output + format->w);
      assert (p <= output + format->w + style->extra_bytes);
      *p = '\0';

      return true;
    }
  return false;
}

/* Formats NUMBER into OUTPUT in scientific notation according to
   the style of the format specified in FORMAT. */
static bool
output_scientific (double number, const struct fmt_spec *format,
                   bool require_affixes, char *output)
{
  const struct fmt_number_style *style =
    settings_get_style (format->type);
  int width;
  int fraction_width;
  bool add_affixes;
  char *p;

  /* Allocate minimum required space. */
  width = 6 + style->neg_suffix.width;
  if (number < 0)
    width += style->neg_prefix.width;
  if (width > format->w)
    return false;

  /* Check for room for prefix and suffix. */
  add_affixes = allocate_space (fmt_affix_width (style), format->w, &width);
  if (require_affixes && !add_affixes)
    return false;

  /* Figure out number of characters we can use for the fraction,
     if any.  (If that turns out to be 1, then we'll output a
     decimal point without any digits following; that's what the
     # flag does in the call to c_snprintf, below.) */
  fraction_width = MIN (MIN (format->d + 1, format->w - width), 16);
  if (format->type != FMT_E && fraction_width == 1)
    fraction_width = 0;
  width += fraction_width;

  /* Format (except suffix). */
  p = output;
  if (width < format->w)
    p = mempset (p, ' ', format->w - width);
  if (number < 0)
    p = stpcpy (p, style->neg_prefix.s);
  if (add_affixes)
    p = stpcpy (p, style->prefix.s);
  if (fraction_width > 0)
    c_snprintf (p, 64, "%#.*E", fraction_width - 1, fabs (number));
  else
    c_snprintf (p, 64, "%.0E", fabs (number));

  /* The C locale always uses a period `.' as a decimal point.
     Translate to comma if necessary. */
  if (style->decimal != '.')
    {
      char *cp = strchr (p, '.');
      if (cp != NULL)
        *cp = style->decimal;
    }

  /* Make exponent have exactly three digits, plus sign. */
  {
    char *cp = strchr (p, 'E') + 1;
    long int exponent = strtol (cp, NULL, 10);
    if (abs (exponent) > 999)
      return false;
    sprintf (cp, "%+04ld", exponent);
  }

  /* Add suffixes. */
  p = strchr (p, '\0');
  if (add_affixes)
    p = stpcpy (p, style->suffix.s);
  if (number < 0)
    p = stpcpy (p, style->neg_suffix.s);
  else
    p = mempset (p, ' ', style->neg_suffix.width);

  assert (p >= output + format->w);
  assert (p <= output + format->w + style->extra_bytes);
  *p = '\0';

  return true;
}

/* Returns true if the magnitude represented by R should be
   rounded up when chopped off at DECIMALS decimal places, false
   if it should be rounded down. */
static bool
should_round_up (const struct rounder *r, int decimals)
{
  int digit = r->string[r->integer_digits + decimals + 1];
  assert (digit >= '0' && digit <= '9');
  return digit >= '5';
}

/* Initializes R for formatting the magnitude of NUMBER to no
   more than MAX_DECIMAL decimal places. */
static void
rounder_init (struct rounder *r, double number, int max_decimals)
{
  assert (fabs (number) < 1e41);
  assert (max_decimals >= 0 && max_decimals <= 16);
  if (max_decimals == 0)
    {
      /* Fast path.  No rounding needed.

         We append ".00" to the integer representation because
         round_up assumes that fractional digits are present.  */
      c_snprintf (r->string, 64, "%.0f.00", fabs (round (number)));
    }
  else
    {
      /* Slow path.

         This is more difficult than it really should be because
         we have to make sure that numbers that are exactly
         halfway between two representations are always rounded
         away from zero.  This is not what sprintf normally does
         (usually it rounds to even), so we have to fake it as
         best we can, by formatting with extra precision and then
         doing the rounding ourselves.

         We take up to two rounds to format numbers.  In the
         first round, we obtain 2 digits of precision beyond
         those requested by the user.  If those digits are
         exactly "50", then in a second round we format with as
         many digits as are significant in a "double".

         It might be better to directly implement our own
         floating-point formatting routine instead of relying on
         the system's sprintf implementation.  But the classic
         Steele and White paper on printing floating-point
         numbers does not hint how to do what we want, and it's
         not obvious how to change their algorithms to do so.  It
         would also be a lot of work. */
      c_snprintf (r->string, 64, "%.*f", max_decimals + 2, fabs (number));
      if (!strcmp (r->string + strlen (r->string) - 2, "50"))
        {
          int binary_exponent, decimal_exponent, format_decimals;
          frexp (number, &binary_exponent);
          decimal_exponent = binary_exponent * 3 / 10;
          format_decimals = (DBL_DIG + 1) - decimal_exponent;
          if (format_decimals > max_decimals + 2)
            c_snprintf (r->string, 64, "%.*f", format_decimals, fabs (number));
        }
    }

  if (r->string[0] == '0')
    memmove (r->string, &r->string[1], strlen (r->string));

  r->leading_zeros = strspn (r->string, "0.");
  r->leading_nines = strspn (r->string, "9.");
  r->integer_digits = strchr (r->string, '.') - r->string;
  assert (r->integer_digits < 64);
  assert (r->integer_digits >= 0);
  r->negative = number < 0;
}

/* Returns the number of characters required to format the
   magnitude represented by R to DECIMALS decimal places.
   The return value includes integer digits and a decimal point
   and fractional digits, if any, but it does not include any
   negative prefix or suffix or other affixes.

   *INTEGER_DIGITS is set to the number of digits before the
   decimal point in the output, between 0 and 40.

   If R represents a negative number and its rounded
   representation would include at least one nonzero digit,
   *NEGATIVE is set to true; otherwise, it is set to false. */
static int
rounder_width (const struct rounder *r, int decimals,
               int *integer_digits, bool *negative)
{
  /* Calculate base measures. */
  int width = r->integer_digits;
  if (decimals > 0)
    width += decimals + 1;
  *integer_digits = r->integer_digits;
  *negative = r->negative;

  /* Rounding can cause adjustments. */
  if (should_round_up (r, decimals))
    {
      /* Rounding up leading 9s adds a new digit (a 1). */
      if (r->leading_nines >= width)
        {
          width++;
          ++*integer_digits;
        }
    }
  else
    {
      /* Rounding down. */
      if (r->leading_zeros >= width)
        {
          /* All digits that remain after rounding are zeros.
             Therefore we drop the negative sign. */
          *negative = false;
          if (r->integer_digits == 0 && decimals == 0)
            {
              /* No digits at all are left.  We need to display
                 at least a single digit (a zero). */
              assert (width == 0);
              width++;
              *integer_digits = 1;
            }
        }
    }
  return width;
}

/* Formats the magnitude represented by R into OUTPUT, rounding
   to DECIMALS decimal places.  Exactly as many characters as
   indicated by rounder_width are written.  No terminating null
   is appended. */
static void
rounder_format (const struct rounder *r, int decimals, char *output)
{
  int base_width = r->integer_digits + (decimals > 0 ? decimals + 1 : 0);
  if (should_round_up (r, decimals))
    {
      if (r->leading_nines < base_width)
        {
          /* Rounding up.  This is the common case where rounding
             up doesn't add an extra digit. */
          char *p;
          memcpy (output, r->string, base_width);
          for (p = output + base_width - 1; ; p--)
            {
              assert (p >= output);
              if (*p == '9')
                *p = '0';
              else if (*p >= '0' && *p <= '8')
                {
                  (*p)++;
                  break;
                }
              else
                assert (*p == '.');
            }
        }
      else
        {
          /* Rounding up leading 9s causes the result to be a 1
             followed by a number of 0s, plus a decimal point. */
          char *p = output;
          *p++ = '1';
          p = mempset (p, '0', r->integer_digits);
          if (decimals > 0)
            {
              *p++ = '.';
              p = mempset (p, '0', decimals);
            }
          assert (p == output + base_width + 1);
        }
    }
  else
    {
      /* Rounding down. */
      if (r->integer_digits != 0 || decimals != 0)
        {
          /* Common case: just copy the digits. */
          memcpy (output, r->string, base_width);
        }
      else
        {
          /* No digits remain.  The output is just a zero. */
          output[0] = '0';
        }
    }
}

/* Helper functions. */

/* Returns 10**X. */
static double PURE_FUNCTION
power10 (int x)
{
  static const double p[] =
    {
      1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9,
      1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19,
      1e20, 1e21, 1e22, 1e23, 1e24, 1e25, 1e26, 1e27, 1e28, 1e29,
      1e30, 1e31, 1e32, 1e33, 1e34, 1e35, 1e36, 1e37, 1e38, 1e39,
      1e40,
    };
  return x >= 0 && x < sizeof p / sizeof *p ? p[x] : pow (10.0, x);
}

/* Returns 256**X. */
static double PURE_FUNCTION
power256 (int x)
{
  static const double p[] =
    {
      1.0,
      256.0,
      65536.0,
      16777216.0,
      4294967296.0,
      1099511627776.0,
      281474976710656.0,
      72057594037927936.0,
      18446744073709551616.0
    };
  return x >= 0 && x < sizeof p / sizeof *p ? p[x] : pow (256.0, x);
}

/* Formats non-finite NUMBER into OUTPUT according to the width
   given in FORMAT. */
static void
output_infinite (double number, const struct fmt_spec *format, char *output)
{
  assert (!isfinite (number));

  if (format->w >= 3)
    {
      const char *s;

      if (isnan (number))
        s = "NaN";
      else if (isinf (number))
        s = number > 0 ? "+Infinity" : "-Infinity";
      else
        s = "Unknown";

      buf_copy_str_lpad (output, format->w, s, ' ');
    }
  else
    output_overflow (format, output);

  output[format->w] = '\0';
}

/* Formats OUTPUT as a missing value for the given FORMAT. */
static void
output_missing (const struct fmt_spec *format, char *output)
{
  memset (output, ' ', format->w);

  if (format->type != FMT_N)
    {
      int dot_ofs = (format->type == FMT_PCT ? 2
                     : format->type == FMT_E ? 5
                     : 1);
      output[MAX (0, format->w - format->d - dot_ofs)] = '.';
    }
  else
    output[format->w - 1] = '.';

  output[format->w] = '\0';
}

/* Formats OUTPUT for overflow given FORMAT. */
static void
output_overflow (const struct fmt_spec *format, char *output)
{
  memset (output, '*', format->w);
  output[format->w] = '\0';
}

/* Converts the integer part of NUMBER to a packed BCD number
   with the given number of DIGITS in OUTPUT.  If DIGITS is odd,
   the least significant nibble of the final byte in OUTPUT is
   set to 0.  Returns true if successful, false if NUMBER is not
   representable.  On failure, OUTPUT is cleared to all zero
   bytes. */
static bool
output_bcd_integer (double number, int digits, char *output)
{
  char decimal[64];

  assert (digits < sizeof decimal);

  output[DIV_RND_UP (digits, 2)] = '\0';
  if (number != SYSMIS
      && number >= 0.
      && number < power10 (digits)
      && c_snprintf (decimal, 64, "%0*.0f", digits, round (number)) == digits)
    {
      const char *src = decimal;
      int i;

      for (i = 0; i < digits / 2; i++)
        {
          int d0 = *src++ - '0';
          int d1 = *src++ - '0';
          *output++ = (d0 << 4) + d1;
        }
      if (digits % 2)
        *output = (*src - '0') << 4;

      return true;
    }
  else
    {
      memset (output, 0, DIV_RND_UP (digits, 2));
      return false;
    }
}

/* Writes VALUE to OUTPUT as a BYTES-byte binary integer of the
   given INTEGER_FORMAT. */
static void
output_binary_integer (uint64_t value, int bytes,
                       enum integer_format integer_format, char *output)
{
  integer_put (value, integer_format, output, bytes);
}

/* Converts the BYTES bytes in DATA to twice as many hexadecimal
   digits in OUTPUT. */
static void
output_hex (const void *data_, size_t bytes, char *output)
{
  const uint8_t *data = data_;
  size_t i;

  for (i = 0; i < bytes; i++)
    {
      static const char hex_digits[] = "0123456789ABCDEF";
      *output++ = hex_digits[data[i] >> 4];
      *output++ = hex_digits[data[i] & 15];
    }
  *output = '\0';
}
