/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

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

#include "data-in.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "calendar.h"
#include "identifier.h"
#include "settings.h"
#include "value.h"

#include <libpspp/assertion.h>
#include <libpspp/compiler.h>
#include <libpspp/integer-format.h>
#include <libpspp/magic.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>

#include "c-ctype.h"
#include "minmax.h"
#include "size_max.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Information about parsing one data field. */
struct data_in
  {
    struct substring input;     /* Source. */
    enum fmt_type format;       /* Input format. */
    int implied_decimals;       /* Number of implied decimal places. */

    union value *output;        /* Destination. */
    int width;                  /* Output width. */

    int first_column; 		/* First column of field; 0 if inapplicable. */
    int last_column; 		/* Last column. */
  };

/* Integer format used for IB and PIB input. */
static enum integer_format input_integer_format = INTEGER_NATIVE;

/* Floating-point format used for RB and RBHEX input. */
static enum float_format input_float_format = FLOAT_NATIVE_DOUBLE;

typedef bool data_in_parser_func (struct data_in *);
#define FMT(NAME, METHOD, IMIN, OMIN, IO, CATEGORY) \
        static data_in_parser_func parse_##METHOD;
#include "format.def"

static void vdata_warning (const struct data_in *, const char *, va_list)
     PRINTF_FORMAT (2, 0);
static void data_warning (const struct data_in *, const char *, ...)
     PRINTF_FORMAT (2, 3);

static void apply_implied_decimals (struct data_in *);
static void default_result (struct data_in *);
static bool trim_spaces_and_check_missing (struct data_in *);

static int hexit_value (int c);

/* Parses the characters in INPUT according to FORMAT.  Stores
   the parsed representation in OUTPUT, which has the given WIDTH
   (0 for a numeric field, otherwise the string width).

   If no decimal point is included in a numeric format, then
   IMPLIED_DECIMALS decimal places are implied.  Specify 0 if no
   decimal places should be implied.

   If FIRST_COLUMN is nonzero, then it should be the 1-based
   column number of the first character in INPUT, used in error
   messages. */
bool
data_in (struct substring input,
         enum fmt_type format, int implied_decimals,
         int first_column, union value *output, int width)
{
  static data_in_parser_func *const handlers[FMT_NUMBER_OF_FORMATS] =
    {
#define FMT(NAME, METHOD, IMIN, OMIN, IO, CATEGORY) parse_##METHOD,
#include "format.def"
    };

  struct data_in i;
  bool ok;

  assert ((width != 0) == fmt_is_string (format));

  i.input = input;
  i.format = format;
  i.implied_decimals = implied_decimals;

  i.output = output;
  i.width = width;

  i.first_column = first_column;
  i.last_column = first_column + ss_length (input) - 1;

  if (!ss_is_empty (i.input))
    {
      ok = handlers[i.format] (&i);
      if (!ok)
        default_result (&i);
    }
  else
    {
      default_result (&i);
      ok = true;
    }

  return ok;
}

/* Returns the integer format used for IB and PIB input. */
enum integer_format
data_in_get_integer_format (void)
{
  return input_integer_format;
}

/* Sets the integer format used for IB and PIB input to
   FORMAT. */
void
data_in_set_integer_format (enum integer_format format)
{
  input_integer_format = format;
}

/* Returns the floating-point format used for RB and RBHEX
   input. */
enum float_format
data_in_get_float_format (void)
{
  return input_float_format;
}

/* Sets the floating-point format used for RB and RBHEX input to
   FORMAT. */
void
data_in_set_float_format (enum float_format format)
{
  input_float_format = format;
}

/* Format parsers. */

/* Parses F, COMMA, DOT, DOLLAR, PCT, and E input formats. */
static bool
parse_number (struct data_in *i)
{
  const struct fmt_number_style *style = fmt_get_style (i->format);

  struct string tmp;

  bool explicit_decimals = false;
  int save_errno;
  char *tail;

  assert (fmt_get_category (i->format) != FMT_CAT_CUSTOM);

  /* Trim spaces and check for missing value representation. */
  if (trim_spaces_and_check_missing (i))
    return true;

  ds_init_empty (&tmp);
  ds_extend (&tmp, 64);

  /* Prefix character may precede sign. */
  if (!ss_is_empty (style->prefix))
    {
      ss_match_char (&i->input, ss_first (style->prefix));
      ss_ltrim (&i->input, ss_cstr (CC_SPACES));
    }

  /* Sign. */
  if (ss_match_char (&i->input, '-'))
    {
      ds_put_char (&tmp, '-');
      ss_ltrim (&i->input, ss_cstr (CC_SPACES));
    }
  else
    {
      ss_match_char (&i->input, '+');
      ss_ltrim (&i->input, ss_cstr (CC_SPACES));
    }

  /* Prefix character may follow sign. */
  if (!ss_is_empty (style->prefix))
    {
      ss_match_char (&i->input, ss_first (style->prefix));
      ss_ltrim (&i->input, ss_cstr (CC_SPACES));
    }

  /* Digits before decimal point. */
  while (c_isdigit (ss_first (i->input)))
    {
      ds_put_char (&tmp, ss_get_char (&i->input));
      if (style->grouping != 0)
        ss_match_char (&i->input, style->grouping);
    }

  /* Decimal point and following digits. */
  if (ss_match_char (&i->input, style->decimal))
    {
      explicit_decimals = true;
      ds_put_char (&tmp, '.');
      while (c_isdigit (ss_first (i->input)))
        ds_put_char (&tmp, ss_get_char (&i->input));
    }

  /* Exponent. */
  if (!ds_is_empty (&tmp)
      && !ss_is_empty (i->input)
      && strchr ("eEdD-+", ss_first (i->input)))
    {
      explicit_decimals = true;
      ds_put_char (&tmp, 'e');

      if (strchr ("eEdD", ss_first (i->input)))
        {
          ss_advance (&i->input, 1);
          ss_match_char (&i->input, ' ');
        }

      if (ss_first (i->input) == '-' || ss_first (i->input) == '+')
        {
          if (ss_get_char (&i->input) == '-')
            ds_put_char (&tmp, '-');
          ss_match_char (&i->input, ' ');
        }

      while (c_isdigit (ss_first (i->input)))
        ds_put_char (&tmp, ss_get_char (&i->input));
    }

  /* Suffix character. */
  if (!ss_is_empty (style->suffix))
    ss_match_char (&i->input, ss_first (style->suffix));

  if (!ss_is_empty (i->input))
    {
      if (ds_is_empty (&tmp))
        data_warning (i, _("Field contents are not numeric."));
      else
        data_warning (i, _("Number followed by garbage."));
      ds_destroy (&tmp);
      return false;
    }

  /* Let strtod() do the conversion. */
  save_errno = errno;
  errno = 0;
  i->output->f = strtod (ds_cstr (&tmp), &tail);
  if (*tail != '\0')
    {
      data_warning (i, _("Invalid numeric syntax."));
      errno = save_errno;
      ds_destroy (&tmp);
      return false;
    }
  else if (errno == ERANGE)
    {
      if (fabs (i->output->f) > 1)
        {
          data_warning (i, _("Too-large number set to system-missing."));
          i->output->f = SYSMIS;
        }
      else
        {
          data_warning (i, _("Too-small number set to zero."));
          i->output->f = 0.0;
        }
    }
  else
    {
      errno = save_errno;
      if (!explicit_decimals)
        apply_implied_decimals (i);
    }

  ds_destroy (&tmp);
  return true;
}

/* Parses N format. */
static bool
parse_N (struct data_in *i)
{
  int c;

  i->output->f = 0;
  while ((c = ss_get_char (&i->input)) != EOF)
    {
      if (!c_isdigit (c))
        {
          data_warning (i, _("All characters in field must be digits."));
          return false;
        }
      i->output->f = i->output->f * 10.0 + (c - '0');
    }

  apply_implied_decimals (i);
  return true;
}

/* Parses PIBHEX format. */
static bool
parse_PIBHEX (struct data_in *i)
{
  double n;
  int c;

  n = 0.0;

  while ((c = ss_get_char (&i->input)) != EOF)
    {
      if (!c_isxdigit (c))
        {
          data_warning (i, _("Unrecognized character in field."));
          return false;
        }
      n = n * 16.0 + hexit_value (c);
    }

  i->output->f = n;
  return true;
}

/* Parses RBHEX format. */
static bool
parse_RBHEX (struct data_in *i)
{
  double d;
  size_t j;

  memset (&d, 0, sizeof d);
  for (j = 0; !ss_is_empty (i->input) && j < sizeof d; j++)
    {
      int hi = ss_get_char (&i->input);
      int lo = ss_get_char (&i->input);
      if (lo == EOF)
        {
          data_warning (i, _("Field must have even length."));
          return false;
        }
      else if (!c_isxdigit (hi) || !c_isxdigit (lo))
	{
	  data_warning (i, _("Field must contain only hex digits."));
	  return false;
	}
      ((unsigned char *) &d)[j] = 16 * hexit_value (hi) + hexit_value (lo);
    }

  i->output->f = d;

  return true;
}

/* Digits for Z format. */
static const char z_digits[] = "0123456789{ABCDEFGHI}JKLMNOPQR";

/* Returns true if C is a Z format digit, false otherwise. */
static bool
is_z_digit (int c)
{
  return c > 0 && strchr (z_digits, c) != NULL;
}

/* Returns the (absolute value of the) value of C as a Z format
   digit. */
static int
z_digit_value (int c)
{
  assert (is_z_digit (c));
  return (strchr (z_digits, c) - z_digits) % 10;
}

/* Returns true if Z format digit C represents a negative value,
   false otherwise. */
static bool
is_negative_z_digit (int c)
{
  assert (is_z_digit (c));
  return (strchr (z_digits, c) - z_digits) >= 20;
}

/* Parses Z format. */
static bool
parse_Z (struct data_in *i)
{
  struct string tmp;

  int save_errno;

  bool got_dot = false;
  bool got_final_digit = false;

  /* Trim spaces and check for missing value representation. */
  if (trim_spaces_and_check_missing (i))
    return true;

  ds_init_empty (&tmp);
  ds_extend (&tmp, 64);

  ds_put_char (&tmp, '+');
  while (!ss_is_empty (i->input))
    {
      int c = ss_get_char (&i->input);
      if (c_isdigit (c) && !got_final_digit)
        ds_put_char (&tmp, c);
      else if (is_z_digit (c) && !got_final_digit)
        {
          ds_put_char (&tmp, z_digit_value (c) + '0');
          if (is_negative_z_digit (c))
            ds_data (&tmp)[0] = '-';
          got_final_digit = true;
        }
      else if (c == '.' && !got_dot)
        {
          ds_put_char (&tmp, '.');
          got_dot = true;
        }
      else
        {
          ds_destroy (&tmp);
          return false;
        }
    }

  if (!ss_is_empty (i->input))
    {
      if (ds_length (&tmp) == 1)
        data_warning (i, _("Field contents are not numeric."));
      else
        data_warning (i, _("Number followed by garbage."));
      ds_destroy (&tmp);
      return false;
    }

  /* Let strtod() do the conversion. */
  save_errno = errno;
  errno = 0;
  i->output->f = strtod (ds_cstr (&tmp), NULL);
  if (errno == ERANGE)
    {
      if (fabs (i->output->f) > 1)
        {
          data_warning (i, _("Too-large number set to system-missing."));
          i->output->f = SYSMIS;
        }
      else
        {
          data_warning (i, _("Too-small number set to zero."));
          i->output->f = 0.0;
        }
    }
  else
    {
      errno = save_errno;
      if (!got_dot)
        apply_implied_decimals (i);
    }

  ds_destroy (&tmp);
  return true;
}

/* Parses IB format. */
static bool
parse_IB (struct data_in *i)
{
  size_t bytes;
  uint64_t value;
  uint64_t sign_bit;

  bytes = MIN (8, ss_length (i->input));
  value = integer_get (input_integer_format, ss_data (i->input), bytes);

  sign_bit = UINT64_C(1) << (8 * bytes - 1);
  if (!(value & sign_bit))
    i->output->f = value;
  else
    {
      /* Sign-extend to full 64 bits. */
      value -= sign_bit << 1;
      i->output->f = -(double) -value;
    }

  apply_implied_decimals (i);

  return true;
}

/* Parses PIB format. */
static bool
parse_PIB (struct data_in *i)
{
  i->output->f = integer_get (input_integer_format, ss_data (i->input),
                              MIN (8, ss_length (i->input)));

  apply_implied_decimals (i);

  return true;
}

/* Consumes the first character of S.  Stores its high 4 bits in
   HIGH_NIBBLE and its low 4 bits in LOW_NIBBLE. */
static void
get_nibbles (struct substring *s, int *high_nibble, int *low_nibble)
{
  int c = ss_get_char (s);
  assert (c != EOF);
  *high_nibble = (c >> 4) & 15;
  *low_nibble = c & 15;
}

/* Parses P format. */
static bool
parse_P (struct data_in *i)
{
  int high_nibble, low_nibble;

  i->output->f = 0.0;

  while (ss_length (i->input) > 1)
    {
      get_nibbles (&i->input, &high_nibble, &low_nibble);
      if (high_nibble > 9 || low_nibble > 9)
        return false;
      i->output->f = (100 * i->output->f) + (10 * high_nibble) + low_nibble;
    }

  get_nibbles (&i->input, &high_nibble, &low_nibble);
  if (high_nibble > 9)
    return false;
  i->output->f = (10 * i->output->f) + high_nibble;
  if (low_nibble < 10)
    i->output->f = (10 * i->output->f) + low_nibble;
  else if (low_nibble == 0xb || low_nibble == 0xd)
    i->output->f = -i->output->f;

  apply_implied_decimals (i);

  return true;
}

/* Parses PK format. */
static bool
parse_PK (struct data_in *i)
{
  i->output->f = 0.0;
  while (!ss_is_empty (i->input))
    {
      int high_nibble, low_nibble;

      get_nibbles (&i->input, &high_nibble, &low_nibble);
      if (high_nibble > 9 || low_nibble > 9)
        {
          i->output->f = SYSMIS;
          return true;
        }
      i->output->f = (100 * i->output->f) + (10 * high_nibble) + low_nibble;
    }

  apply_implied_decimals (i);

  return true;
}

/* Parses RB format. */
static bool
parse_RB (struct data_in *i)
{
  size_t size = float_get_size (input_float_format);
  if (ss_length (i->input) >= size)
    float_convert (input_float_format, ss_data (i->input),
                   FLOAT_NATIVE_DOUBLE, &i->output->f);
  else
    i->output->f = SYSMIS;

  return true;
}

/* Parses A format. */
static bool
parse_A (struct data_in *i)
{
  buf_copy_rpad (i->output->s, i->width,
                 ss_data (i->input), ss_length (i->input));
  return true;
}

/* Parses AHEX format. */
static bool
parse_AHEX (struct data_in *i)
{
  size_t j;

  for (j = 0; ; j++)
    {
      int hi = ss_get_char (&i->input);
      int lo = ss_get_char (&i->input);
      if (hi == EOF)
        break;
      else if (lo == EOF)
        {
          data_warning (i, _("Field must have even length."));
          return false;
        }

      if (!c_isxdigit (hi) || !c_isxdigit (lo))
	{
	  data_warning (i, _("Field must contain only hex digits."));
	  return false;
	}

      if (j < i->width)
        i->output->s[j] = hexit_value (hi) * 16 + hexit_value (lo);
    }

  memset (i->output->s + j, ' ', i->width - j);

  return true;
}

/* Date & time format components. */

/* Sign of a time value. */
enum time_sign
  {
    SIGN_NO_TIME,       /* No time yet encountered. */
    SIGN_POSITIVE,      /* Positive time. */
    SIGN_NEGATIVE       /* Negative time. */
  };

/* Parses a signed decimal integer from at most the first
   MAX_DIGITS characters in I, storing the result into *RESULT.
   Returns true if successful, false if no integer was
   present. */
static bool
parse_int (struct data_in *i, long *result, size_t max_digits)
{
  struct substring head = ss_head (i->input, max_digits);
  size_t n = ss_get_long (&head, result);
  if (n)
    {
      ss_advance (&i->input, n);
      return true;
    }
  else
    {
      data_warning (i, _("Syntax error in date field."));
      return false;
    }
}

/* Parses a date integer between 1 and 31 from I, storing it into
   *DAY.
   Returns true if successful, false if no date was present. */
static bool
parse_day (struct data_in *i, long *day)
{
  if (!parse_int (i, day, SIZE_MAX))
    return false;
  if (*day >= 1 && *day <= 31)
    return true;

  data_warning (i, _("Day (%ld) must be between 1 and 31."), *day);
  return false;
}

/* Parses an integer from the beginning of I.
   Adds SECONDS_PER_UNIT times the absolute value of the integer
   to *TIME.
   If *TIME_SIGN is SIGN_NO_TIME, allows a sign to precede the
   time and sets *TIME_SIGN.  Otherwise, does not allow a sign.
   Returns true if successful, false if no integer was present. */
static bool
parse_time_units (struct data_in *i, double seconds_per_unit,
                  enum time_sign *time_sign, double *time)

{
  long units;

  if (*time_sign == SIGN_NO_TIME)
    {
      if (ss_match_char (&i->input, '-'))
        *time_sign = SIGN_NEGATIVE;
      else
        {
          ss_match_char (&i->input, '+');
          *time_sign = SIGN_POSITIVE;
        }
    }
  if (!parse_int (i, &units, SIZE_MAX))
    return false;
  if (units < 0)
    {
      data_warning (i, _("Syntax error in date field."));
      return false;
    }
  *time += units * seconds_per_unit;
  return true;
}

/* Parses a data delimiter from the beginning of I.
   Returns true if successful, false if no delimiter was
   present. */
static bool
parse_date_delimiter (struct data_in *i)
{
  if (ss_ltrim (&i->input, ss_cstr ("-/.," CC_SPACES)))
    return true;

  data_warning (i, _("Delimiter expected between fields in date."));
  return false;
}

/* Parses spaces at the beginning of I. */
static void
parse_spaces (struct data_in *i)
{
  ss_ltrim (&i->input, ss_cstr (CC_SPACES));
}

static struct substring
parse_name_token (struct data_in *i)
{
  struct substring token;
  ss_get_chars (&i->input, ss_span (i->input, ss_cstr (CC_LETTERS)), &token);
  return token;
}

/* Reads a name from I and sets *OUTPUT to the value associated
   with that name.  If ALLOW_SUFFIXES is true, then names that
   begin with one of the names are accepted; otherwise, only
   exact matches (except for case) are allowed.
   Returns true if successful, false otherwise. */
static bool
match_name (struct substring token, const char **names, long *output)
{
  int i;

  for (i = 1; *names != NULL; i++)
    if (ss_equals_case (ss_cstr (*names++), token))
      {
        *output = i;
        return true;
      }

  return false;
}

/* Parses a month name or number from the beginning of I,
   storing the month (in range 1...12) into *MONTH.
   Returns true if successful, false if no month was present. */
static bool
parse_month (struct data_in *i, long *month)
{
  if (c_isdigit (ss_first (i->input)))
    {
      if (!parse_int (i, month, SIZE_MAX))
	return false;
      if (*month >= 1 && *month <= 12)
        return true;
    }
  else
    {
      static const char *english_names[] =
        {
          "jan", "feb", "mar", "apr", "may", "jun",
          "jul", "aug", "sep", "oct", "nov", "dec",
          NULL,
        };

      static const char *roman_names[] =
        {
          "i", "ii", "iii", "iv", "v", "vi",
          "vii", "viii", "ix", "x", "xi", "xii",
          NULL,
        };

      struct substring token = parse_name_token (i);
      if (match_name (ss_head (token, 3), english_names, month)
          || match_name (ss_head (token, 4), roman_names, month))
        return true;
    }

  data_warning (i, _("Unrecognized month format.  Months may be specified "
                     "as Arabic or Roman numerals or as at least 3 letters "
                     "of their English names."));
  return false;
}

/* Parses a year of at most MAX_DIGITS from the beginning of I,
   storing a "4-digit" year into *YEAR. */
static bool
parse_year (struct data_in *i, long *year, size_t max_digits)
{
  if (!parse_int (i, year, max_digits))
    return false;

  if (*year >= 0 && *year <= 99)
    {
      int epoch = get_epoch ();
      int epoch_century = ROUND_DOWN (epoch, 100);
      int epoch_offset = epoch - epoch_century;
      if (*year >= epoch_offset)
        *year += epoch_century;
      else
        *year += epoch_century + 100;
    }
  if (*year >= 1582 || *year <= 19999)
    return true;

  data_warning (i, _("Year (%ld) must be between 1582 and 19999."), *year);
  return false;
}

/* Returns true if input in I has been exhausted,
   false otherwise. */
static bool
parse_trailer (struct data_in *i)
{
  if (ss_is_empty (i->input))
    return true;

  data_warning (i, _("Trailing garbage \"%.*s\" following date."),
              (int) ss_length (i->input), ss_data (i->input));
  return false;
}

/* Parses a 3-digit Julian day-of-year value from I into *YDAY.
   Returns true if successful, false on failure. */
static bool
parse_yday (struct data_in *i, long *yday)
{
  struct substring num_s;
  long num;

  ss_get_chars (&i->input, 3, &num_s);
  if (ss_span (num_s, ss_cstr (CC_DIGITS)) != 3)
    {
      data_warning (i, _("Julian day must have exactly three digits."));
      return false;
    }
  else if (!ss_get_long (&num_s, &num) || num < 1 || num > 366)
    {
      data_warning (i, _("Julian day (%ld) must be between 1 and 366."), num);
      return false;
    }

  *yday = num;
  return true;
}

/* Parses a quarter-of-year integer between 1 and 4 from I.
   Stores the corresponding month into *MONTH.
   Returns true if successful, false if no quarter was present. */
static bool
parse_quarter (struct data_in *i, long int *month)
{
  long quarter;

  if (!parse_int (i, &quarter, SIZE_MAX))
    return false;
  if (quarter >= 1 && quarter <= 4)
    {
      *month = (quarter - 1) * 3 + 1;
      return true;
    }

  data_warning (i, _("Quarter (%ld) must be between 1 and 4."), quarter);
  return false;
}

/* Parses a week-of-year integer between 1 and 53 from I,
   Stores the corresponding year-of-day into *YDAY.
   Returns true if successful, false if no week was present. */
static bool
parse_week (struct data_in *i, long int *yday)
{
  long week;

  if (!parse_int (i, &week, SIZE_MAX))
    return false;
  if (week >= 1 && week <= 53)
    {
      *yday = (week - 1) * 7 + 1;
      return true;
    }

  data_warning (i, _("Week (%ld) must be between 1 and 53."), week);
  return false;
}

/* Parses a time delimiter from the beginning of I.
   Returns true if successful, false if no delimiter was
   present. */
static bool
parse_time_delimiter (struct data_in *i)
{
  if (ss_ltrim (&i->input, ss_cstr (":" CC_SPACES)) > 0)
    return true;

  data_warning (i, _("Delimiter expected between fields in time."));
  return false;
}

/* Parses minutes and optional seconds from the beginning of I.
   The time is converted into seconds, which are added to
   *TIME.
   Returns true if successful, false if an error was found. */
static bool
parse_minute_second (struct data_in *i, double *time)
{
  long minute;
  char buf[64];
  char *cp;

  /* Parse minutes. */
  if (!parse_int (i, &minute, SIZE_MAX))
    return false;
  if (minute < 0 || minute > 59)
    {
      data_warning (i, _("Minute (%ld) must be between 0 and 59."), minute);
      return false;
    }
  *time += 60. * minute;

  /* Check for seconds. */
  if (ss_ltrim (&i->input, ss_cstr (":" CC_SPACES)) == 0
      || !c_isdigit (ss_first (i->input)))
   return true;

  /* Parse seconds. */
  cp = buf;
  while (c_isdigit (ss_first (i->input)))
    *cp++ = ss_get_char (&i->input);
  if (ss_match_char (&i->input, fmt_decimal_char (FMT_F)))
    *cp++ = '.';
  while (c_isdigit (ss_first (i->input)))
    *cp++ = ss_get_char (&i->input);
  *cp = '\0';

  *time += strtod (buf, NULL);

  return true;
}

/* Parses a weekday name from the beginning of I,
   storing a value of 1=Sunday...7=Saturday into *WEEKDAY.
   Returns true if successful, false if an error was found. */
static bool
parse_weekday (struct data_in *i, long *weekday)
{
  static const char *weekday_names[] =
    {
      "su", "mo", "tu", "we", "th", "fr", "sa",
      NULL,
    };

  struct substring token = parse_name_token (i);
  bool ok = match_name (ss_head (token, 2), weekday_names, weekday);
  if (!ok)
    data_warning (i, _("Unrecognized weekday name.  At least the first two "
                       "letters of an English weekday name must be "
                       "specified."));
  return ok;
}

/* Date & time formats. */

/* Helper function for passing to
   calendar_gregorian_to_offset. */
static void
calendar_error (void *i_, const char *format, ...)
{
  struct data_in *i = i_;
  va_list args;

  va_start (args, format);
  vdata_warning (i, format, args);
  va_end (args);
}

/* Parses WKDAY format. */
static bool
parse_WKDAY (struct data_in *i)
{
  long weekday;

  if (trim_spaces_and_check_missing (i))
    return true;

  if (!parse_weekday (i, &weekday)
      || !parse_trailer (i))
    return false;

  i->output->f = weekday;
  return true;
}

/* Parses MONTH format. */
static bool
parse_MONTH (struct data_in *i)
{
  long month;

  if (trim_spaces_and_check_missing (i))
    return true;

  if (!parse_month (i, &month)
      || !parse_trailer (i))
    return false;

  i->output->f = month;
  return true;
}

/* Parses DATE, ADATE, EDATE, JDATE, SDATE, QYR, MOYR, KWYR,
   DATETIME, TIME and DTIME formats. */
static bool
parse_date (struct data_in *i)
{
  long int year = INT_MIN;
  long int month = 1;
  long int day = 1;
  long int yday = 1;
  double time = 0, date = 0;
  enum time_sign time_sign = SIGN_NO_TIME;

  const char *template = fmt_date_template (i->format);
  size_t template_width = strlen (template);

  if (trim_spaces_and_check_missing (i))
    return true;

  while (*template != '\0')
    {
      unsigned char ch = *template;
      int count = 1;
      bool ok;

      while (template[count] == ch)
        count++;
      template += count;

      ok = true;
      switch (ch)
        {
        case 'd':
          ok = count < 3 ? parse_day (i, &day) : parse_yday (i, &yday);
          break;
        case 'm':
          ok = parse_month (i, &month);
          break;
        case 'y':
          {
            size_t max_digits;
            if (!c_isalpha (*template))
              max_digits = SIZE_MAX;
            else
              {
                if (ss_length (i->input) >= template_width + 2)
                  max_digits = 4;
                else
                  max_digits = 2;
              }
            ok = parse_year (i, &year, max_digits);
          }
          break;
        case 'q':
          ok = parse_quarter (i, &month);
          break;
        case 'w':
          ok = parse_week (i, &yday);
          break;
        case 'D':
          ok = parse_time_units (i, 60. * 60. * 24., &time_sign, &time);
          break;
        case 'H':
          ok = parse_time_units (i, 60. * 60., &time_sign, &time);
          break;
        case 'M':
          ok = parse_minute_second (i, &time);
          break;
        case '-':
        case '/':
        case '.':
        case 'X':
          ok = parse_date_delimiter (i);
          break;
        case ':':
          ok = parse_time_delimiter (i);
        case ' ':
          parse_spaces (i);
          break;
        default:
          assert (count == 1);
          if (!ss_match_char (&i->input, c_toupper (ch))
              && !ss_match_char (&i->input, c_tolower (ch)))
            {
              data_warning (i, _("`%c' expected in date field."), ch);
              return false;
            }
          break;
        }
      if (!ok)
        return false;
    }
  if (!parse_trailer (i))
    return false;

  if (year != INT_MIN)
    {
      double ofs = calendar_gregorian_to_offset (year, month, day,
                                                 calendar_error, i);
      if (ofs == SYSMIS)
        return false;
      date = (yday - 1 + ofs) * 60. * 60. * 24.;
    }
  else
    date = 0.;
  i->output->f = date + (time_sign == SIGN_NEGATIVE ? -time : time);

  return true;
}

/* Utility functions. */

/* Outputs FORMAT with the given ARGS as a warning for input
   I. */
static void
vdata_warning (const struct data_in *i, const char *format, va_list args)
{
  struct msg m;
  struct string text;

  ds_init_empty (&text);
  ds_put_char (&text, '(');
  if (i->first_column != 0)
    {
      if (i->first_column == i->last_column)
        ds_put_format (&text, _("column %d"), i->first_column);
      else
        ds_put_format (&text, _("columns %d-%d"),
                       i->first_column, i->last_column);
      ds_put_cstr (&text, ", ");
    }
  ds_put_format (&text, _("%s field) "), fmt_name (i->format));
  ds_put_vformat (&text, format, args);

  m.category = MSG_DATA;
  m.severity = MSG_WARNING;
  m.text = ds_cstr (&text);

  msg_emit (&m);
}

/* Outputs FORMAT with the given ARGS as a warning for input
   I. */
static void
data_warning (const struct data_in *i, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  vdata_warning (i, format, args);
  va_end (args);
}

/* Apply implied decimal places to output. */
static void
apply_implied_decimals (struct data_in *i)
{
  if (i->implied_decimals > 0)
    i->output->f /= pow (10., i->implied_decimals);
}

/* Sets the default result for I.
   For a numeric format, this is the value set on SET BLANKS
   (typically system-missing); for a string format, it is all
   spaces. */
static void
default_result (struct data_in *i)
{
  if (fmt_is_string (i->format))
    memset (i->output->s, ' ', i->width);
  else
    i->output->f = get_blanks ();
}

/* Trims leading and trailing spaces from I.
   If the result is empty, or a single period character, then
   sets the default result and returns true; otherwise, returns
   false. */
static bool
trim_spaces_and_check_missing (struct data_in *i)
{
  ss_trim (&i->input, ss_cstr (" "));
  if (ss_is_empty (i->input) || ss_equals (i->input, ss_cstr (".")))
    {
      default_result (i);
      return true;
    }
  return false;
}

/* Returns the integer value of hex digit C. */
static int
hexit_value (int c)
{
  const char s[] = "0123456789abcdef";
  const char *cp = strchr (s, c_tolower ((unsigned char) c));

  assert (cp != NULL);
  return cp - s;
}
