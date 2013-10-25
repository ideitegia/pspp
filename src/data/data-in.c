/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include "data-in.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "calendar.h"
#include "dictionary.h"
#include "format.h"
#include "identifier.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/integer-format.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"
#include "settings.h"
#include "value.h"

#include "gl/c-ctype.h"
#include "gl/c-strtod.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Information about parsing one data field. */
struct data_in
  {
    struct substring input;     /* Source. */
    enum fmt_type format;       /* Input format. */

    union value *output;        /* Destination. */
    int width;                  /* Output width. */
  };

typedef char *data_in_parser_func (struct data_in *);
#define FMT(NAME, METHOD, IMIN, OMIN, IO, CATEGORY) \
        static data_in_parser_func parse_##METHOD;
#include "format.def"

static void default_result (struct data_in *);
static bool trim_spaces_and_check_missing (struct data_in *);

static int hexit_value (int c);

/* Parses the characters in INPUT, which are encoded in the given
   INPUT_ENCODING, according to FORMAT.

   Stores the parsed representation in OUTPUT, which the caller must have
   initialized with the given WIDTH (0 for a numeric field, otherwise the
   string width).  If FORMAT is FMT_A, then OUTPUT_ENCODING must specify the
   correct encoding for OUTPUT (normally obtained via dict_get_encoding()). */
char *
data_in (struct substring input, const char *input_encoding,
         enum fmt_type format,
         union value *output, int width, const char *output_encoding)
{
  static data_in_parser_func *const handlers[FMT_NUMBER_OF_FORMATS] =
    {
#define FMT(NAME, METHOD, IMIN, OMIN, IO, CATEGORY) parse_##METHOD,
#include "format.def"
    };

  struct data_in i;

  enum fmt_category cat;
  const char *dest_encoding;
  char *s;
  char *error;

  assert ((width != 0) == fmt_is_string (format));

  i.format = format;

  i.output = output;
  i.width = width;

  if (ss_is_empty (input))
    {
      default_result (&i);
      return NULL;
    }

  cat = fmt_get_category (format);
  if (cat & (FMT_CAT_BASIC | FMT_CAT_HEXADECIMAL
             | FMT_CAT_DATE | FMT_CAT_TIME | FMT_CAT_DATE_COMPONENT))
    {
      /* We're going to parse these into numbers.  For this purpose we want to
         deal with them in the local "C" encoding.  Any character not in that
         encoding wouldn't be valid anyhow. */
      dest_encoding = C_ENCODING;
    }
  else if (cat & (FMT_CAT_BINARY | FMT_CAT_LEGACY))
    {
      /* Don't recode these binary formats at all, since they are not text. */
      dest_encoding = NULL;
    }
  else
    {
      assert (cat == FMT_CAT_STRING);
      if (format == FMT_AHEX)
        {
          /* We want the hex digits in the local "C" encoding, even though the
             result may not be in that encoding. */
          dest_encoding = C_ENCODING;
        }
      else
        {
          /* Use the final output encoding. */
          dest_encoding = output_encoding;
        }
    }

  if (dest_encoding != NULL)
    {
      i.input = recode_substring_pool (dest_encoding, input_encoding, input,
                                       NULL);
      s = i.input.string;
    }
  else
    {
      i.input = input;
      s = NULL;
    }

  error = handlers[i.format] (&i);
  if (error != NULL)
    default_result (&i);

  free (s);

  return error;
}

bool
data_in_msg (struct substring input, const char *input_encoding,
             enum fmt_type format,
             union value *output, int width, const char *output_encoding)
{
  char *error = data_in (input, input_encoding, format,
                         output, width, output_encoding);
  if (error != NULL)
    {
      msg (SW,_("Data is not valid as format %s: %s"),
           fmt_name (format), error);
      free (error);
      return false;
    }
  else
    return true;
}

static bool
number_has_implied_decimals (const char *s, enum fmt_type type)
{
  int decimal = settings_get_style (type)->decimal;
  bool got_digit = false;
  for (;;)
    {
      switch (*s)
        {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
          got_digit = true;
          break;

        case '+': case '-':
          if (got_digit)
            return false;
          break;

        case 'e': case 'E': case 'd': case 'D':
          return false;

        case '.': case ',':
          if (*s == decimal)
            return false;
          break;

        case '\0':
          return true;

        default:
          break;
        }

      s++;
    }
}

static bool
has_implied_decimals (struct substring input, const char *input_encoding,
                      enum fmt_type format)
{
  bool retval;
  char *s;

  switch (format)
    {
    case FMT_F:
    case FMT_COMMA:
    case FMT_DOT:
    case FMT_DOLLAR:
    case FMT_PCT:
    case FMT_E:
    case FMT_Z:
      break;

    case FMT_N:
    case FMT_IB:
    case FMT_PIB:
    case FMT_P:
    case FMT_PK:
      return true;

    default:
      return false;
    }

  s = recode_string (C_ENCODING, input_encoding,
                     ss_data (input), ss_length (input));
  retval = (format == FMT_Z
            ? strchr (s, '.') == NULL
            : number_has_implied_decimals (s, format));
  free (s);

  return retval;
}

/* In some cases, when no decimal point is explicitly included in numeric
   input, its position is implied by the number of decimal places in the input
   format.  In such a case, this function may be called just after data_in().
   Its arguments are a subset of that function's arguments plus D, the number
   of decimal places associated with FORMAT.

   If it is appropriate, this function modifies the numeric value in OUTPUT. */
void
data_in_imply_decimals (struct substring input, const char *input_encoding,
                        enum fmt_type format, int d, union value *output)
{
  if (d > 0 && output->f != SYSMIS
      && has_implied_decimals (input, input_encoding, format))
    output->f /= pow (10., d);
}

/* Format parsers. */

/* Parses F, COMMA, DOT, DOLLAR, PCT, and E input formats. */
static char *
parse_number (struct data_in *i)
{
  const struct fmt_number_style *style =
    settings_get_style (i->format);

  struct string tmp;

  int save_errno;
  char *tail;

  if  (fmt_get_category (i->format) == FMT_CAT_CUSTOM)
    {
      style = settings_get_style (FMT_F);
    }

  /* Trim spaces and check for missing value representation. */
  if (trim_spaces_and_check_missing (i))
    return NULL;

  ds_init_empty (&tmp);
  ds_extend (&tmp, 64);

  /* Prefix character may precede sign. */
  if (style->prefix.s[0] != '\0')
    {
      ss_match_byte (&i->input, style->prefix.s[0]);
      ss_ltrim (&i->input, ss_cstr (CC_SPACES));
    }

  /* Sign. */
  if (ss_match_byte (&i->input, '-'))
    {
      ds_put_byte (&tmp, '-');
      ss_ltrim (&i->input, ss_cstr (CC_SPACES));
    }
  else
    {
      ss_match_byte (&i->input, '+');
      ss_ltrim (&i->input, ss_cstr (CC_SPACES));
    }

  /* Prefix character may follow sign. */
  if (style->prefix.s[0] != '\0')
    {
      ss_match_byte (&i->input, style->prefix.s[0]);
      ss_ltrim (&i->input, ss_cstr (CC_SPACES));
    }

  /* Digits before decimal point. */
  while (c_isdigit (ss_first (i->input)))
    {
      ds_put_byte (&tmp, ss_get_byte (&i->input));
      if (style->grouping != 0)
        ss_match_byte (&i->input, style->grouping);
    }

  /* Decimal point and following digits. */
  if (ss_match_byte (&i->input, style->decimal))
    {
      ds_put_byte (&tmp, '.');
      while (c_isdigit (ss_first (i->input)))
        ds_put_byte (&tmp, ss_get_byte (&i->input));
    }

  /* Exponent. */
  if (!ds_is_empty (&tmp)
      && !ss_is_empty (i->input)
      && strchr ("eEdD-+", ss_first (i->input)))
    {
      ds_put_byte (&tmp, 'e');

      if (strchr ("eEdD", ss_first (i->input)))
        {
          ss_advance (&i->input, 1);
          ss_match_byte (&i->input, ' ');
        }

      if (ss_first (i->input) == '-' || ss_first (i->input) == '+')
        {
          if (ss_get_byte (&i->input) == '-')
            ds_put_byte (&tmp, '-');
          ss_match_byte (&i->input, ' ');
        }

      while (c_isdigit (ss_first (i->input)))
        ds_put_byte (&tmp, ss_get_byte (&i->input));
    }

  /* Suffix character. */
  if (style->suffix.s[0] != '\0')
    ss_match_byte (&i->input, style->suffix.s[0]);

  if (!ss_is_empty (i->input))
    {
      char *error;
      if (ds_is_empty (&tmp))
        error = xstrdup (_("Field contents are not numeric."));
      else
        error = xstrdup (_("Number followed by garbage."));
      ds_destroy (&tmp);
      return error;
    }

  /* Let c_strtod() do the conversion. */
  save_errno = errno;
  errno = 0;
  i->output->f = c_strtod (ds_cstr (&tmp), &tail);
  if (*tail != '\0')
    {
      errno = save_errno;
      ds_destroy (&tmp);
      return xstrdup (_("Invalid numeric syntax."));
    }
  else if (errno == ERANGE)
    {
      if (fabs (i->output->f) > 1)
        {
          i->output->f = SYSMIS;
          ds_destroy (&tmp);
          return xstrdup (_("Too-large number set to system-missing."));
        }
      else
        {
          i->output->f = 0.0;
          ds_destroy (&tmp);
          return xstrdup (_("Too-small number set to zero."));
        }
    }
  else
    errno = save_errno;

  ds_destroy (&tmp);
  return NULL;
}

/* Parses N format. */
static char *
parse_N (struct data_in *i)
{
  int c;

  i->output->f = 0;
  while ((c = ss_get_byte (&i->input)) != EOF)
    {
      if (!c_isdigit (c))
        return xstrdup (_("All characters in field must be digits."));
      i->output->f = i->output->f * 10.0 + (c - '0');
    }

  return NULL;
}

/* Parses PIBHEX format. */
static char *
parse_PIBHEX (struct data_in *i)
{
  double n;
  int c;

  n = 0.0;

  while ((c = ss_get_byte (&i->input)) != EOF)
    {
      if (!c_isxdigit (c))
        return xstrdup (_("Unrecognized character in field."));
      n = n * 16.0 + hexit_value (c);
    }

  i->output->f = n;
  return NULL;
}

/* Parses RBHEX format. */
static char *
parse_RBHEX (struct data_in *i)
{
  double d;
  size_t j;

  memset (&d, 0, sizeof d);
  for (j = 0; !ss_is_empty (i->input) && j < sizeof d; j++)
    {
      int hi = ss_get_byte (&i->input);
      int lo = ss_get_byte (&i->input);
      if (lo == EOF)
        return xstrdup (_("Field must have even length."));
      else if (!c_isxdigit (hi) || !c_isxdigit (lo))
        return xstrdup (_("Field must contain only hex digits."));
      ((unsigned char *) &d)[j] = 16 * hexit_value (hi) + hexit_value (lo);
    }

  i->output->f = d;

  return NULL;
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
static char *
parse_Z (struct data_in *i)
{
  struct string tmp;

  int save_errno;

  bool got_dot = false;
  bool got_final_digit = false;

  /* Trim spaces and check for missing value representation. */
  if (trim_spaces_and_check_missing (i))
    return NULL;

  ds_init_empty (&tmp);
  ds_extend (&tmp, 64);

  ds_put_byte (&tmp, '+');
  while (!ss_is_empty (i->input))
    {
      int c = ss_get_byte (&i->input);
      if (c_isdigit (c) && !got_final_digit)
        ds_put_byte (&tmp, c);
      else if (is_z_digit (c) && !got_final_digit)
        {
          ds_put_byte (&tmp, z_digit_value (c) + '0');
          if (is_negative_z_digit (c))
            ds_data (&tmp)[0] = '-';
          got_final_digit = true;
        }
      else if (c == '.' && !got_dot)
        {
          ds_put_byte (&tmp, '.');
          got_dot = true;
        }
      else
        {
          ds_destroy (&tmp);
          return xstrdup (_("Invalid zoned decimal syntax."));
        }
    }

  if (!ss_is_empty (i->input))
    {
      char *error;

      if (ds_length (&tmp) == 1)
        error = xstrdup (_("Field contents are not numeric."));
      else
        error = xstrdup (_("Number followed by garbage."));

      ds_destroy (&tmp);
      return error;
    }

  /* Let c_strtod() do the conversion. */
  save_errno = errno;
  errno = 0;
  i->output->f = c_strtod (ds_cstr (&tmp), NULL);
  if (errno == ERANGE)
    {
      if (fabs (i->output->f) > 1)
        {
          i->output->f = SYSMIS;
          ds_destroy (&tmp);
          return xstrdup (_("Too-large number set to system-missing."));
        }
      else
        {
          i->output->f = 0.0;
          ds_destroy (&tmp);
          return xstrdup (_("Too-small number set to zero."));
        }
    }
  else
    errno = save_errno;

  ds_destroy (&tmp);
  return NULL;
}

/* Parses IB format. */
static char *
parse_IB (struct data_in *i)
{
  size_t bytes;
  uint64_t value;
  uint64_t sign_bit;

  bytes = MIN (8, ss_length (i->input));
  value = integer_get (settings_get_input_integer_format (), ss_data (i->input), bytes);

  sign_bit = UINT64_C(1) << (8 * bytes - 1);
  if (!(value & sign_bit))
    i->output->f = value;
  else
    {
      /* Sign-extend to full 64 bits. */
      value -= sign_bit << 1;
      i->output->f = -(double) -value;
    }

  return NULL;
}

/* Parses PIB format. */
static char *
parse_PIB (struct data_in *i)
{
  i->output->f = integer_get (settings_get_input_integer_format (), ss_data (i->input),
                              MIN (8, ss_length (i->input)));

  return NULL;
}

/* Consumes the first character of S.  Stores its high 4 bits in
   HIGH_NIBBLE and its low 4 bits in LOW_NIBBLE. */
static void
get_nibbles (struct substring *s, int *high_nibble, int *low_nibble)
{
  int c = ss_get_byte (s);
  assert (c != EOF);
  *high_nibble = (c >> 4) & 15;
  *low_nibble = c & 15;
}

/* Parses P format. */
static char *
parse_P (struct data_in *i)
{
  int high_nibble, low_nibble;

  i->output->f = 0.0;

  while (ss_length (i->input) > 1)
    {
      get_nibbles (&i->input, &high_nibble, &low_nibble);
      if (high_nibble > 9 || low_nibble > 9)
        return xstrdup (_("Invalid syntax for P field."));
      i->output->f = (100 * i->output->f) + (10 * high_nibble) + low_nibble;
    }

  get_nibbles (&i->input, &high_nibble, &low_nibble);
  if (high_nibble > 9)
    return xstrdup (_("Invalid syntax for P field."));
  i->output->f = (10 * i->output->f) + high_nibble;
  if (low_nibble < 10)
    i->output->f = (10 * i->output->f) + low_nibble;
  else if (low_nibble == 0xb || low_nibble == 0xd)
    i->output->f = -i->output->f;

  return NULL;
}

/* Parses PK format. */
static char *
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
          return NULL;
        }
      i->output->f = (100 * i->output->f) + (10 * high_nibble) + low_nibble;
    }

  return NULL;
}

/* Parses RB format. */
static char *
parse_RB (struct data_in *i)
{
  enum float_format ff = settings_get_input_float_format ();
  size_t size = float_get_size (ff);
  if (ss_length (i->input) >= size)
    float_convert (ff, ss_data (i->input),
                   FLOAT_NATIVE_DOUBLE, &i->output->f);
  else
    i->output->f = SYSMIS;

  return NULL;
}

/* Parses A format. */
static char *
parse_A (struct data_in *i)
{
  /* This is equivalent to buf_copy_rpad, except that we posibly
     do a character set recoding in the middle. */
  uint8_t *dst = value_str_rw (i->output, i->width);
  size_t dst_size = i->width;
  const char *src = ss_data (i->input);
  size_t src_size = ss_length (i->input);

  memcpy (dst, src, MIN (src_size, dst_size));

  if (dst_size > src_size)
    memset (&dst[src_size], ' ', dst_size - src_size);

  return NULL;
}

/* Parses AHEX format. */
static char *
parse_AHEX (struct data_in *i)
{
  uint8_t *s = value_str_rw (i->output, i->width);
  size_t j;

  for (j = 0; ; j++)
    {
      int hi = ss_get_byte (&i->input);
      int lo = ss_get_byte (&i->input);
      if (hi == EOF)
        break;
      else if (lo == EOF)
        return xstrdup (_("Field must have even length."));

      if (!c_isxdigit (hi) || !c_isxdigit (lo))
        return xstrdup (_("Field must contain only hex digits."));

      if (j < i->width)
        s[j] = hexit_value (hi) * 16 + hexit_value (lo);
    }

  memset (&s[j], ' ', i->width - j);

  return NULL;
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
static char * WARN_UNUSED_RESULT
parse_int (struct data_in *i, long *result, size_t max_digits)
{
  struct substring head = ss_head (i->input, max_digits);
  size_t n = ss_get_long (&head, result);
  if (n)
    {
      ss_advance (&i->input, n);
      return NULL;
    }
  else
    return xstrdup (_("Syntax error in date field."));
}

/* Parses a date integer between 1 and 31 from I, storing it into
   *DAY.
   Returns true if successful, false if no date was present. */
static char *
parse_day (struct data_in *i, long *day)
{
  char *error = parse_int (i, day, SIZE_MAX);
  if (error != NULL)
    return error;
  if (*day >= 1 && *day <= 31)
    return NULL;

  return xasprintf (_("Day (%ld) must be between 1 and 31."), *day);
}

/* Parses an integer from the beginning of I.
   Adds SECONDS_PER_UNIT times the absolute value of the integer
   to *TIME.
   If *TIME_SIGN is SIGN_NO_TIME, allows a sign to precede the
   time and sets *TIME_SIGN.  Otherwise, does not allow a sign.
   Returns true if successful, false if no integer was present. */
static char *
parse_time_units (struct data_in *i, double seconds_per_unit,
                  enum time_sign *time_sign, double *time)

{
  char *error;
  long units;

  if (*time_sign == SIGN_NO_TIME)
    {
      if (ss_match_byte (&i->input, '-'))
        *time_sign = SIGN_NEGATIVE;
      else
        {
          ss_match_byte (&i->input, '+');
          *time_sign = SIGN_POSITIVE;
        }
    }
  error = parse_int (i, &units, SIZE_MAX);
  if (error != NULL)
    return error;
  if (units < 0)
    return xstrdup (_("Syntax error in date field."));
  *time += units * seconds_per_unit;
  return NULL;
}

/* Parses a data delimiter from the beginning of I.
   Returns true if successful, false if no delimiter was
   present. */
static char *
parse_date_delimiter (struct data_in *i)
{
  if (ss_ltrim (&i->input, ss_cstr ("-/.," CC_SPACES)))
    return NULL;

  return xstrdup (_("Delimiter expected between fields in date."));
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
  ss_get_bytes (&i->input, ss_span (i->input, ss_cstr (CC_LETTERS)), &token);
  return token;
}

/* Reads a name from I and sets *OUTPUT to the value associated
   with that name.  If ALLOW_SUFFIXES is true, then names that
   begin with one of the names are accepted; otherwise, only
   exact matches (except for case) are allowed.
   Returns true if successful, false otherwise. */
static bool
match_name (struct substring token, const char *const *names, long *output)
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
static char *
parse_month (struct data_in *i, long *month)
{
  if (c_isdigit (ss_first (i->input)))
    {
      char *error = parse_int (i, month, SIZE_MAX);
      if (error != NULL)
	return error;
      if (*month >= 1 && *month <= 12)
        return NULL;
    }
  else
    {
      static const char *const english_names[] =
        {
          "jan", "feb", "mar", "apr", "may", "jun",
          "jul", "aug", "sep", "oct", "nov", "dec",
          NULL,
        };

      static const char *const roman_names[] =
        {
          "i", "ii", "iii", "iv", "v", "vi",
          "vii", "viii", "ix", "x", "xi", "xii",
          NULL,
        };

      struct substring token = parse_name_token (i);
      if (match_name (ss_head (token, 3), english_names, month)
          || match_name (ss_head (token, 4), roman_names, month))
        return NULL;
    }

  return xstrdup (_("Unrecognized month format.  Months may be specified "
                    "as Arabic or Roman numerals or as at least 3 letters "
                    "of their English names."));
}

/* Parses a year of at most MAX_DIGITS from the beginning of I,
   storing a "4-digit" year into *YEAR. */
static char *
parse_year (struct data_in *i, long *year, size_t max_digits)
{
  char *error = parse_int (i, year, max_digits);
  if (error != NULL)
    return error;

  if (*year >= 0 && *year <= 99)
    {
      int epoch = settings_get_epoch ();
      int epoch_century = ROUND_DOWN (epoch, 100);
      int epoch_offset = epoch - epoch_century;
      if (*year >= epoch_offset)
        *year += epoch_century;
      else
        *year += epoch_century + 100;
    }
  if (*year >= 1582 && *year <= 19999)
    return NULL;

  return xasprintf (_("Year (%ld) must be between 1582 and 19999."), *year);
}

/* Returns true if input in I has been exhausted,
   false otherwise. */
static char *
parse_trailer (struct data_in *i)
{
  if (ss_is_empty (i->input))
    return NULL;

  return xasprintf (_("Trailing garbage `%.*s' following date."),
                    (int) ss_length (i->input), ss_data (i->input));
}

/* Parses a 3-digit Julian day-of-year value from I into *YDAY.
   Returns true if successful, false on failure. */
static char *
parse_yday (struct data_in *i, long *yday)
{
  struct substring num_s;
  long num;

  ss_get_bytes (&i->input, 3, &num_s);
  if (ss_span (num_s, ss_cstr (CC_DIGITS)) != 3)
    return xstrdup (_("Julian day must have exactly three digits."));
  else if (!ss_get_long (&num_s, &num) || num < 1 || num > 366)
    return xasprintf (_("Julian day (%ld) must be between 1 and 366."), num);

  *yday = num;
  return NULL;
}

/* Parses a quarter-of-year integer between 1 and 4 from I.
   Stores the corresponding month into *MONTH.
   Returns true if successful, false if no quarter was present. */
static char *
parse_quarter (struct data_in *i, long int *month)
{
  long quarter;
  char *error;

  error = parse_int (i, &quarter, SIZE_MAX);
  if (error != NULL)
    return error;
  if (quarter >= 1 && quarter <= 4)
    {
      *month = (quarter - 1) * 3 + 1;
      return NULL;
    }

  return xasprintf (_("Quarter (%ld) must be between 1 and 4."), quarter);
}

/* Parses a week-of-year integer between 1 and 53 from I,
   Stores the corresponding year-of-day into *YDAY.
   Returns true if successful, false if no week was present. */
static char *
parse_week (struct data_in *i, long int *yday)
{
  char *error;
  long week;

  error = parse_int (i, &week, SIZE_MAX);
  if (error != NULL)
    return error;
  if (week >= 1 && week <= 53)
    {
      *yday = (week - 1) * 7 + 1;
      return NULL;
    }

  return xasprintf (_("Week (%ld) must be between 1 and 53."), week);
}

/* Parses a time delimiter from the beginning of I.
   Returns true if successful, false if no delimiter was
   present. */
static char *
parse_time_delimiter (struct data_in *i)
{
  if (ss_ltrim (&i->input, ss_cstr (":" CC_SPACES)) > 0)
    return NULL;

  return xstrdup (_("Delimiter expected between fields in time."));
}

/* Parses minutes and optional seconds from the beginning of I.
   The time is converted into seconds, which are added to
   *TIME.
   Returns true if successful, false if an error was found. */
static char *
parse_minute_second (struct data_in *i, double *time)
{
  long minute;
  char buf[64];
  char *error;
  char *cp;

  /* Parse minutes. */
  error = parse_int (i, &minute, SIZE_MAX);
  if (error != NULL)
    return error;
  if (minute < 0 || minute > 59)
    return xasprintf (_("Minute (%ld) must be between 0 and 59."), minute);
  *time += 60. * minute;

  /* Check for seconds. */
  if (ss_ltrim (&i->input, ss_cstr (":" CC_SPACES)) == 0
      || !c_isdigit (ss_first (i->input)))
   return NULL;

  /* Parse seconds. */
  cp = buf;
  while (c_isdigit (ss_first (i->input)))
    *cp++ = ss_get_byte (&i->input);
  if (ss_match_byte (&i->input, settings_get_decimal_char (FMT_F)))
    *cp++ = '.';
  while (c_isdigit (ss_first (i->input)))
    *cp++ = ss_get_byte (&i->input);
  *cp = '\0';

  *time += c_strtod (buf, NULL);

  return NULL;
}

/* Parses a weekday name from the beginning of I,
   storing a value of 1=Sunday...7=Saturday into *WEEKDAY.
   Returns true if successful, false if an error was found. */
static char *
parse_weekday (struct data_in *i, long *weekday)
{
  static const char *const weekday_names[] =
    {
      "su", "mo", "tu", "we", "th", "fr", "sa",
      NULL,
    };

  struct substring token = parse_name_token (i);
  bool ok = match_name (ss_head (token, 2), weekday_names, weekday);
  if (!ok)
    return xstrdup (_("Unrecognized weekday name.  At least the first two "
                      "letters of an English weekday name must be "
                      "specified."));
  return NULL;
}

/* Date & time formats. */

/* Parses WKDAY format. */
static char *
parse_WKDAY (struct data_in *i)
{
  long weekday;
  char *error;

  if (trim_spaces_and_check_missing (i))
    return NULL;

  error = parse_weekday (i, &weekday);
  if (error == NULL)
    error = parse_trailer (i);

  i->output->f = weekday;
  return error;
}

/* Parses MONTH format. */
static char *
parse_MONTH (struct data_in *i)
{
  long month;
  char *error;

  if (trim_spaces_and_check_missing (i))
    return NULL;

  error = parse_month (i, &month);
  if (error == NULL)
    error = parse_trailer (i);

  i->output->f = month;
  return error;
}

/* Parses DATE, ADATE, EDATE, JDATE, SDATE, QYR, MOYR, KWYR,
   DATETIME, TIME and DTIME formats. */
static char *
parse_date (struct data_in *i)
{
  long int year = INT_MIN;
  long int month = 1;
  long int day = 1;
  long int yday = 1;
  double time = 0, date = 0;
  enum time_sign time_sign = SIGN_NO_TIME;

  const char *template = fmt_date_template (i->format, 0);
  size_t template_width = strlen (template);
  char *error;

  if (trim_spaces_and_check_missing (i))
    return NULL;

  while (*template != '\0')
    {
      unsigned char ch = *template;
      int count = 1;

      while (template[count] == ch)
        count++;
      template += count;

      switch (ch)
        {
        case 'd':
          error = count < 3 ? parse_day (i, &day) : parse_yday (i, &yday);
          break;
        case 'm':
          error = parse_month (i, &month);
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
            error = parse_year (i, &year, max_digits);
          }
          break;
        case 'q':
          error = parse_quarter (i, &month);
          break;
        case 'w':
          error = parse_week (i, &yday);
          break;
        case 'D':
          error = parse_time_units (i, 60. * 60. * 24., &time_sign, &time);
          break;
        case 'H':
          error = parse_time_units (i, 60. * 60., &time_sign, &time);
          break;
        case 'M':
          error = parse_minute_second (i, &time);
          break;
        case '-':
        case '/':
        case '.':
          error = parse_date_delimiter (i);
          break;
        case ':':
          error = parse_time_delimiter (i);
        case ' ':
          if (i->format != FMT_MOYR)
            {
              parse_spaces (i);
              error = NULL;
            }
          else
            error = parse_date_delimiter (i);
          break;
        default:
          assert (count == 1);
          if (!ss_match_byte (&i->input, c_toupper (ch))
              && !ss_match_byte (&i->input, c_tolower (ch)))
            error = xasprintf (_("`%c' expected in date field."), ch);
          else
            error = NULL;
          break;
        }
      if (error != NULL)
        return error;
    }
  error = parse_trailer (i);
  if (error != NULL)
    return error;

  if (year != INT_MIN)
    {
      char *error;
      double ofs;

      ofs = calendar_gregorian_to_offset (year, month, day, &error);
      if (ofs == SYSMIS)
        return error;
      date = (yday - 1 + ofs) * 60. * 60. * 24.;
    }
  else
    date = 0.;
  i->output->f = date + (time_sign == SIGN_NEGATIVE ? -time : time);

  return NULL;
}

/* Utility functions. */

/* Sets the default result for I.
   For a numeric format, this is the value set on SET BLANKS
   (typically system-missing); for a string format, it is all
   spaces. */
static void
default_result (struct data_in *i)
{
  if (fmt_is_string (i->format))
    memset (value_str_rw (i->output, i->width), ' ', i->width);
  else
    i->output->f = settings_get_blanks ();
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
