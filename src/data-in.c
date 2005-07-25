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
#include "data-in.h"
#include "error.h"
#include <math.h>
#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "bool.h"
#include "error.h"
#include "getline.h"
#include "calendar.h"
#include "lexer.h"
#include "magic.h"
#include "misc.h"
#include "settings.h"
#include "str.h"
#include "var.h"

#include "debug-print.h"


/* Specialized error routine. */

static void dls_error (const struct data_in *, const char *format, ...)
     PRINTF_FORMAT (2, 3);

static void
vdls_error (const struct data_in *i, const char *format, va_list args)
{
  struct error e;
  struct string title;

  if (i->flags & DI_IGNORE_ERROR)
    return;

  ds_init (&title, 64);
  if (!getl_reading_script)
    ds_puts (&title, _("data-file error: "));
  if (i->f1 == i->f2)
    ds_printf (&title, _("(column %d"), i->f1);
  else
    ds_printf (&title, _("(columns %d-%d"), i->f1, i->f2);
  ds_printf (&title, _(", field type %s) "), fmt_to_string (&i->format));
    
  e.class = DE;
  err_location (&e.where);
  e.title = ds_c_str (&title);

  err_vmsg (&e, format, args);

  ds_destroy (&title);
}

static void
dls_error (const struct data_in *i, const char *format, ...) 
{
  va_list args;

  va_start (args, format);
  vdls_error (i, format, args);
  va_end (args);
}

/* Parsing utility functions. */

/* Excludes leading and trailing whitespace from I by adjusting
   pointers. */
static void
trim_whitespace (struct data_in *i)
{
  while (i->s < i->e && isspace (i->s[0])) 
    i->s++;

  while (i->s < i->e && isspace (i->e[-1]))
    i->e--;
}

/* Returns nonzero if we're not at the end of the string being
   parsed. */
static inline bool
have_char (struct data_in *i)
{
  return i->s < i->e;
}

/* If implied decimal places are enabled, apply them to
   I->v->f. */
static void
apply_implied_decimals (struct data_in *i) 
{
  if ((i->flags & DI_IMPLIED_DECIMALS) && i->format.d > 0)
    i->v->f /= pow (10., i->format.d);
}

/* Format parsers. */ 

static bool parse_int (struct data_in *i, long *result);

/* This function is based on strtod() from the GNU C library. */
static bool
parse_numeric (struct data_in *i)
{
  int sign;                     /* +1 or -1. */
  double num;			/* The number so far.  */

  bool got_dot;			/* Found a decimal point.  */
  size_t digit_cnt;		/* Count of digits.  */

  int decimal;			/* Decimal point character. */
  int grouping;			/* Grouping character. */

  long int exponent;		/* Number's exponent. */
  int type;			/* Usually same as i->format.type. */

  trim_whitespace (i);

  type = i->format.type;
  if (type == FMT_DOLLAR && have_char (i) && *i->s == '$')
    {
      i->s++;
      type = FMT_COMMA;
    }

  /* Get the sign.  */
  if (have_char (i))
    {
      sign = *i->s == '-' ? -1 : 1;
      if (*i->s == '-' || *i->s == '+')
	i->s++;
    }
  else
    sign = 1;
  
  if (type != FMT_DOT)
    {
      decimal = get_decimal();
      grouping = get_grouping();
    }
  else
    {
      decimal = get_grouping();
      grouping = get_decimal();
    }

  i->v->f = SYSMIS;
  num = 0.0;
  got_dot = false;
  digit_cnt = 0;
  exponent = 0;
  for (; have_char (i); i->s++)
    {
      if (isdigit (*i->s))
	{
	  digit_cnt++;

	  /* Make sure that multiplication by 10 will not overflow.  */
	  if (num > DBL_MAX * 0.1)
	    /* The value of the digit doesn't matter, since we have already
	       gotten as many digits as can be represented in a `double'.
	       This doesn't necessarily mean the result will overflow.
	       The exponent may reduce it to within range.

	       We just need to record that there was another
	       digit so that we can multiply by 10 later.  */
	    ++exponent;
	  else
	    num = (num * 10.0) + (*i->s - '0');

	  /* Keep track of the number of digits after the decimal point.
	     If we just divided by 10 here, we would lose precision.  */
	  if (got_dot)
	    --exponent;
	}
      else if (!got_dot && *i->s == decimal)
	/* Record that we have found the decimal point.  */
	got_dot = true;
      else if ((type != FMT_COMMA && type != FMT_DOT) || *i->s != grouping)
	/* Any other character terminates the number.  */
	break;
    }

  if (!digit_cnt)
    {
      if (got_dot)
	{
	  i->v->f = SYSMIS;
	  return true;
	}
      dls_error (i, _("Field does not form a valid floating-point constant."));
      i->v->f = SYSMIS;
      return false;
    }
  
  if (have_char (i) && strchr ("eEdD-+", *i->s))
    {
      /* Get the exponent specified after the `e' or `E'.  */
      long exp;

      if (isalpha (*i->s))
	i->s++;
      if (!parse_int (i, &exp))
        {
          i->v->f = SYSMIS;
          return false;
        }

      exponent += exp;
    }
  else if (!got_dot && (i->flags & DI_IMPLIED_DECIMALS))
    exponent -= i->format.d;

  if (type == FMT_PCT && have_char (i) && *i->s == '%')
    i->s++;
  if (i->s < i->e)
    {
      dls_error (i, _("Field contents followed by garbage."));
      i->v->f = SYSMIS;
      return false;
    }

  if (num == 0.0)
    {
      i->v->f = 0.0;
      return true;
    }

  /* Multiply NUM by 10 to the EXPONENT power, checking for overflow
     and underflow.  */
  if (exponent < 0)
    {
      if (-exponent + digit_cnt > -(DBL_MIN_10_EXP) + 5
	  || num < DBL_MIN * pow (10.0, (double) -exponent)) 
        {
          dls_error (i, _("Underflow in floating-point constant."));
          i->v->f = 0.0;
          return false;
        }

      num *= pow (10.0, (double) exponent);
    }
  else if (exponent > 0)
    {
      if (num > DBL_MAX * pow (10.0, (double) -exponent))
        {
          dls_error (i, _("Overflow in floating-point constant."));
          i->v->f = SYSMIS;
          return false;
        }
      
      num *= pow (10.0, (double) exponent);
    }

  i->v->f = sign > 0 ? num : -num;
  return true;
}

/* Returns the integer value of hex digit C. */
static inline int
hexit_value (int c)
{
  const char s[] = "0123456789abcdef";
  const char *cp = strchr (s, tolower ((unsigned char) c));

  assert (cp != NULL);
  return cp - s;
}

static inline bool
parse_N (struct data_in *i)
{
  const unsigned char *cp;

  i->v->f = 0;
  for (cp = i->s; cp < i->e; cp++)
    {
      if (!isdigit (*cp))
	{
	  dls_error (i, _("All characters in field must be digits."));
	  return false;
	}

      i->v->f = i->v->f * 10.0 + *cp - '0';
    }

  apply_implied_decimals (i);
  return true;
}

static inline bool
parse_PIBHEX (struct data_in *i)
{
  double n;
  const unsigned char *cp;

  trim_whitespace (i);

  n = 0.0;
  for (cp = i->s; cp < i->e; cp++)
    {
      if (!isxdigit (*cp))
	{
	  dls_error (i, _("Unrecognized character in field."));
	  return false;
	}

      n = n * 16.0 + hexit_value (*cp);
    }
  
  i->v->f = n;
  return true;
}

static inline bool
parse_RBHEX (struct data_in *i)
{
  /* Validate input. */
  trim_whitespace (i);
  if ((i->e - i->s) % 2)
    {
      dls_error (i, _("Field must have even length."));
      return false;
    }
  
  {
    const unsigned char *cp;
    
    for (cp = i->s; cp < i->e; cp++)
      if (!isxdigit (*cp))
	{
	  dls_error (i, _("Field must contain only hex digits."));
	  return false;
	}
  }
  
  /* Parse input. */
  {
    union
      {
	double d;
	unsigned char c[sizeof (double)];
      }
    u;

    int j;

    memset (u.c, 0, sizeof u.c);
    for (j = 0; j < min ((i->e - i->s) / 2, sizeof u.d); j++)
      u.c[j] = 16 * hexit_value (i->s[j * 2]) + hexit_value (i->s[j * 2 + 1]);

    i->v->f = u.d;
  }
  
  return true;
}

static inline bool
parse_Z (struct data_in *i)
{
  char buf[64];
  bool got_dot = false;

  /* Warn user that we suck. */
  {
    static bool warned;

    if (!warned)
      {
	msg (MW, 
	     _("Quality of zoned decimal (Z) input format code is "
	       "suspect.  Check your results three times. Report bugs "
		"to %s."),PACKAGE_BUGREPORT);
	warned = true;
      }
  }

  /* Validate input. */
  trim_whitespace (i);

  if (i->e - i->s < 2)
    {
      dls_error (i, _("Zoned decimal field contains fewer than 2 "
		      "characters."));
      return false;
    }

  /* Copy sign into buf[0]. */
  if ((i->e[-1] & 0xc0) != 0xc0)
    {
      dls_error (i, _("Bad sign byte in zoned decimal number."));
      return false;
    }
  buf[0] = (i->e[-1] ^ (i->e[-1] >> 1)) & 0x10 ? '-' : '+';

  /* Copy digits into buf[1 ... len - 1] and terminate string. */
  {
    const unsigned char *sp;
    char *dp;

    for (sp = i->s, dp = buf + 1; sp < i->e - 1; sp++, dp++)
      if (*sp == '.') 
        {
          *dp = '.';
          got_dot = true;
        }
      else if ((*sp & 0xf0) == 0xf0 && (*sp & 0xf) < 10)
	*dp = (*sp & 0xf) + '0';
      else
	{
	  dls_error (i, _("Format error in zoned decimal number."));
	  return false;
	}

    *dp = '\0';
  }

  /* Parse as number. */
  {
    char *tail;
    
    i->v->f = strtod ((char *) buf, (char **) &tail);
    if ((unsigned char *) tail != i->e)
      {
	dls_error (i, _("Error in syntax of zoned decimal number."));
	return false;
      }
  }

  if (!got_dot)
    apply_implied_decimals (i);

  return true;
}

static inline bool
parse_IB (struct data_in *i)
{
  char buf[64];
  const char *p;

  unsigned char xor;

  /* We want the data to be in big-endian format.  If this is a
     little-endian machine, reverse the byte order. */
#ifdef WORDS_BIGENDIAN
  p = i->s;
#else
  memcpy (buf, i->s, i->e - i->s);
  buf_reverse (buf, i->e - i->s);
  p = buf;
#endif

  /* If the value is negative, we need to logical-NOT each value
     before adding it. */
  if (p[0] & 0x80)
    xor = 0xff;
  else
    xor = 0x00;
  
  {
    int j;

    i->v->f = 0.0;
    for (j = 0; j < i->e - i->s; j++)
      i->v->f = i->v->f * 256.0 + (p[j] ^ xor);
  }

  /* If the value is negative, add 1 and set the sign, to complete a
     two's-complement negation. */
  if (p[0] & 0x80)
    i->v->f = -(i->v->f + 1.0);

  apply_implied_decimals (i);

  return true;
}

static inline bool
parse_PIB (struct data_in *i)
{
  int j;

  i->v->f = 0.0;
#if WORDS_BIGENDIAN
  for (j = 0; j < i->e - i->s; j++)
    i->v->f = i->v->f * 256.0 + i->s[j];
#else
  for (j = i->e - i->s - 1; j >= 0; j--)
    i->v->f = i->v->f * 256.0 + i->s[j];
#endif

  apply_implied_decimals (i);

  return true;
}

static inline bool
parse_P (struct data_in *i)
{
  const unsigned char *cp;

  i->v->f = 0.0;
  for (cp = i->s; cp < i->e - 1; cp++)
    {
      i->v->f = i->v->f * 10 + (*cp >> 4);
      i->v->f = i->v->f * 10 + (*cp & 15);
    }
  i->v->f = i->v->f * 10 + (*cp >> 4);
  if ((*cp ^ (*cp >> 1)) & 0x10)
      i->v->f = -i->v->f;

  apply_implied_decimals (i);

  return true;
}

static inline bool
parse_PK (struct data_in *i)
{
  const unsigned char *cp;

  i->v->f = 0.0;
  for (cp = i->s; cp < i->e; cp++)
    {
      i->v->f = i->v->f * 10 + (*cp >> 4);
      i->v->f = i->v->f * 10 + (*cp & 15);
    }

  apply_implied_decimals (i);

  return true;
}

static inline bool
parse_RB (struct data_in *i)
{
  union
    {
      double d;
      unsigned char c[sizeof (double)];
    }
  u;

  memset (u.c, 0, sizeof u.c);
  memcpy (u.c, i->s, min ((int) sizeof (u.c), i->e - i->s));
  i->v->f = u.d;

  return true;
}

static inline bool
parse_A (struct data_in *i)
{
  ptrdiff_t len = i->e - i->s;
  
  if (len >= i->format.w)
    memcpy (i->v->s, i->s, i->format.w);
  else
    {
      memcpy (i->v->s, i->s, len);
      memset (i->v->s + len, ' ', i->format.w - len);
    }

  return true;
}

static inline bool
parse_AHEX (struct data_in *i)
{
  /* Validate input. */
  trim_whitespace (i);
  if ((i->e - i->s) % 2)
    {
      dls_error (i, _("Field must have even length."));
      return false;
    }

  {
    const unsigned char *cp;
    
    for (cp = i->s; cp < i->e; cp++)
      if (!isxdigit (*cp))
	{
	  dls_error (i, _("Field must contain only hex digits."));
	  return false;
	}
  }
  
  {
    int j;
    
    /* Parse input. */
    for (j = 0; j < min (i->e - i->s, i->format.w); j += 2)
      i->v->s[j / 2] = hexit_value (i->s[j]) * 16 + hexit_value (i->s[j + 1]);
    memset (i->v->s + (i->e - i->s) / 2, ' ', (i->format.w - (i->e - i->s)) / 2);
  }
  
  return true;
}

/* Date & time format components. */

/* Advances *CP past any whitespace characters. */
static inline void
skip_whitespace (struct data_in *i)
{
  while (isspace ((unsigned char) *i->s))
    i->s++;
}

static inline bool
parse_leader (struct data_in *i)
{
  skip_whitespace (i);
  return true;
}

static inline bool
force_have_char (struct data_in *i)
{
  if (have_char (i))
    return true;

  dls_error (i, _("Unexpected end of field."));
  return false;
}

static bool
parse_int (struct data_in *i, long *result)
{
  bool negative = false;
  
  if (!force_have_char (i))
    return false;

  if (*i->s == '+')
    {
      i->s++;
      force_have_char (i);
    }
  else if (*i->s == '-')
    {
      negative = true;
      i->s++;
      force_have_char (i);
    }
  
  if (!isdigit (*i->s))
    {
      dls_error (i, _("Digit expected in field."));
      return false;
    }

  *result = 0;
  for (;;)
    {
      *result = *result * 10 + *i->s++ - '0';
      if (!have_char (i) || !isdigit (*i->s))
	break;
    }

  if (negative)
    *result = -*result;
  return true;
}

static bool
parse_day (struct data_in *i, long *day)
{
  if (!parse_int (i, day))
    return false;
  if (*day >= 1 && *day <= 31)
    return true;

  dls_error (i, _("Day (%ld) must be between 1 and 31."), *day);
  return false;
}

static bool
parse_day_count (struct data_in *i, long *day_count)
{
  return parse_int (i, day_count);
}

static bool
parse_date_delimiter (struct data_in *i)
{
  bool delim = false;

  while (have_char (i)
	 && (*i->s == '-' || *i->s == '/' || isspace (*i->s)
	     || *i->s == '.' || *i->s == ','))
    {
      delim = true;
      i->s++;
    }
  if (delim)
    return true;

  dls_error (i, _("Delimiter expected between fields in date."));
  return false;
}

/* Association between a name and a value. */
struct enum_name
  {
    const char *name;           /* Name. */
    bool can_abbreviate;        /* True if name may be abbreviated. */
    int value;                  /* Value associated with name. */
  };

/* Reads a name from I and sets *OUTPUT to the value associated
   with that name.  Returns true if successful, false otherwise. */
static bool
parse_enum (struct data_in *i, const char *what,
            const struct enum_name *enum_names,
            long *output) 
{
  const char *name;
  size_t length;
  const struct enum_name *ep;

  /* Consume alphabetic characters. */
  name = i->s;
  length = 0;
  while (have_char (i) && isalpha (*i->s)) 
    {
      length++;
      i->s++; 
    }
  if (length == 0) 
    {
      dls_error (i, _("Parse error at `%c' expecting %s."), *i->s, what);
      return false;
    }

  for (ep = enum_names; ep->name != NULL; ep++)
    if ((ep->can_abbreviate
         && lex_id_match_len (ep->name, strlen (ep->name), name, length))
        || (!ep->can_abbreviate && length == strlen (ep->name)
            && !buf_compare_case (name, ep->name, length)))
      {
        *output = ep->value;
        return true;
      }

  dls_error (i, _("Unknown %s `%.*s'."), what, (int) length, name);
  return false;
}

static bool
parse_month (struct data_in *i, long *month)
{
  static const struct enum_name month_names[] = 
    {
      {"january", true, 1},
      {"february", true, 2},
      {"march", true, 3},
      {"april", true, 4},
      {"may", true, 5},
      {"june", true, 6},
      {"july", true, 7},
      {"august", true, 8},
      {"september", true, 9},
      {"october", true, 10},
      {"november", true, 11},
      {"december", true, 12},

      {"i", false, 1},
      {"ii", false, 2},
      {"iii", false, 3},
      {"iv", false, 4},
      {"iiii", false, 4},
      {"v", false, 5},
      {"vi", false, 6},
      {"vii", false, 7},
      {"viii", false, 8},
      {"ix", false, 9},
      {"viiii", false, 9},
      {"x", false, 10},
      {"xi", false, 11},
      {"xii", false, 12},

      {NULL, false, 0},
    };

  if (!force_have_char (i))
    return false;
  
  if (isdigit (*i->s))
    {
      if (!parse_int (i, month))
	return false;
      if (*month >= 1 && *month <= 12)
	return true;
      
      dls_error (i, _("Month (%ld) must be between 1 and 12."), *month);
      return false;
    }
  else 
    return parse_enum (i, _("month"), month_names, month);
}

static bool
parse_year (struct data_in *i, long *year)
{
  if (!parse_int (i, year))
    return false;
  
  if (*year >= 0 && *year <= 199)
    *year += 1900;
  if (*year >= 1582 || *year <= 19999)
    return true;

  dls_error (i, _("Year (%ld) must be between 1582 and 19999."), *year);
  return false;
}

static bool
parse_trailer (struct data_in *i)
{
  skip_whitespace (i);
  if (!have_char (i))
    return true;
  
  dls_error (i, _("Trailing garbage \"%s\" following date."), i->s);
  return false;
}

static bool
parse_julian (struct data_in *i, long *julian)
{
  if (!parse_int (i, julian))
    return false;
   
  {
    int day = *julian % 1000;

    if (day < 1 || day > 366)
      {
	dls_error (i, _("Julian day (%d) must be between 1 and 366."), day);
	return false;
      }
  }
  
  {
    int year = *julian / 1000;

    if (year >= 0 && year <= 199)
      *julian += 1900000L;
    else if (year < 1582 || year > 19999)
      {
	dls_error (i, _("Year (%d) must be between 1582 and 19999."), year);
	return false;
      }
  }

  return true;
}

static bool
parse_quarter (struct data_in *i, long *quarter)
{
  if (!parse_int (i, quarter))
    return false;
  if (*quarter >= 1 && *quarter <= 4)
    return true;

  dls_error (i, _("Quarter (%ld) must be between 1 and 4."), *quarter);
  return false;
}

static bool
parse_q_delimiter (struct data_in *i)
{
  skip_whitespace (i);
  if (!have_char (i) || tolower (*i->s) != 'q')
    {
      dls_error (i, _("`Q' expected between quarter and year."));
      return false;
    }
  i->s++;
  skip_whitespace (i);
  return true;
}

static bool
parse_week (struct data_in *i, long *week)
{
  if (!parse_int (i, week))
    return false;
  if (*week >= 1 && *week <= 53)
    return true;

  dls_error (i, _("Week (%ld) must be between 1 and 53."), *week);
  return false;
}

static bool
parse_wk_delimiter (struct data_in *i)
{
  skip_whitespace (i);
  if (i->s + 1 >= i->e
      || tolower (i->s[0]) != 'w' || tolower (i->s[1]) != 'k')
    {
      dls_error (i, _("`WK' expected between week and year."));
      return false;
    }
  i->s += 2;
  skip_whitespace (i);
  return true;
}

static bool
parse_time_delimiter (struct data_in *i)
{
  bool delim = false;

  while (have_char (i) && (*i->s == ':' || *i->s == '.' || isspace (*i->s)))
    {
      delim = true;
      i->s++;
    }

  if (delim)
    return true;
  
  dls_error (i, _("Delimiter expected between fields in time."));
  return false;
}

static bool
parse_hour (struct data_in *i, long *hour)
{
  if (!parse_int (i, hour))
    return false;
  if (*hour >= 0)
    return true;
  
  dls_error (i, _("Hour (%ld) must be positive."), *hour);
  return false;
}

static bool
parse_minute (struct data_in *i, long *minute)
{
  if (!parse_int (i, minute))
    return false;
  if (*minute >= 0 && *minute <= 59)
    return true;
  
  dls_error (i, _("Minute (%ld) must be between 0 and 59."), *minute);
  return false;
}

static bool
parse_opt_second (struct data_in *i, double *second)
{
  bool delim = false;

  char buf[64];
  char *cp;

  while (have_char (i)
	 && (*i->s == ':' || *i->s == '.' || isspace (*i->s)))
    {
      delim = true;
      i->s++;
    }
  
  if (!delim || !isdigit (*i->s))
    {
      *second = 0.0;
      return true;
    }

  cp = buf;
  while (have_char (i) && isdigit (*i->s))
    *cp++ = *i->s++;
  if (have_char (i) && *i->s == '.')
    *cp++ = *i->s++;
  while (have_char (i) && isdigit (*i->s))
    *cp++ = *i->s++;
  *cp = '\0';
  
  *second = strtod (buf, NULL);

  return true;
}

static bool
parse_hour24 (struct data_in *i, long *hour24)
{
  if (!parse_int (i, hour24))
    return false;
  if (*hour24 >= 0 && *hour24 <= 23)
    return true;
  
  dls_error (i, _("Hour (%ld) must be between 0 and 23."), *hour24);
  return false;
}

     
static bool
parse_weekday (struct data_in *i, long *weekday)
{
  static const struct enum_name weekday_names[] = 
    {
      {"sunday", true, 1},
      {"su", true, 1},
      {"monday", true, 2},
      {"mo", true, 2},
      {"tuesday", true, 3},
      {"tu", true, 3},
      {"wednesday", true, 4},
      {"we", true, 4},
      {"thursday", true, 5},
      {"th", true, 5},
      {"friday", true, 6},
      {"fr", true, 6},
      {"saturday", true, 7},
      {"sa", true, 7},
      
      {NULL, false, 0},
    };

  return parse_enum (i, _("weekday"), weekday_names, weekday);
}

static bool
parse_spaces (struct data_in *i)
{
  skip_whitespace (i);
  return true;
}

static bool
parse_sign (struct data_in *i, int *sign)
{
  if (!force_have_char (i))
    return false;

  switch (*i->s)
    {
    case '-':
      i->s++;
      *sign = -1;
      break;

    case '+':
      i->s++;
      /* fall through */

    default:
      *sign = 1;
      break;
    }

  return true;
}

/* Date & time formats. */

static void
calendar_error (void *i_, const char *format, ...) 
{
  struct data_in *i = i_;
  va_list args;

  va_start (args, format);
  vdls_error (i, format, args);
  va_end (args);
}

static bool
ymd_to_ofs (struct data_in *i, int year, int month, int day, double *ofs) 
{
  *ofs = calendar_gregorian_to_offset (year, month, day, calendar_error, i);
  return *ofs != SYSMIS;
}

static bool
ymd_to_date (struct data_in *i, int year, int month, int day, double *date) 
{
  if (ymd_to_ofs (i, year, month, day, date)) 
    {
      *date *= 60. * 60. * 24.;
      return true; 
    }
  else
    return false;
}

static bool
parse_DATE (struct data_in *i)
{
  long day, month, year;

  return (parse_leader (i)
          && parse_day (i, &day)
          && parse_date_delimiter (i)
          && parse_month (i, &month)
          && parse_date_delimiter (i)
          && parse_year (i, &year)
          && parse_trailer (i)
          && ymd_to_date (i, year, month, day, &i->v->f));
}

static bool
parse_ADATE (struct data_in *i)
{
  long month, day, year;

  return (parse_leader (i)
          && parse_month (i, &month)
          && parse_date_delimiter (i)
          && parse_day (i, &day)
          && parse_date_delimiter (i)
          && parse_year (i, &year)
          && parse_trailer (i)
          && ymd_to_date (i, year, month, day, &i->v->f));
}

static bool
parse_EDATE (struct data_in *i)
{
  long month, day, year;

  return (parse_leader (i)
          && parse_day (i, &day)
          && parse_date_delimiter (i)
          && parse_month (i, &month)
          && parse_date_delimiter (i)
          && parse_year (i, &year)
          && parse_trailer (i)
          && ymd_to_date (i, year, month, day, &i->v->f));
}

static bool
parse_SDATE (struct data_in *i)
{
  long month, day, year;

  return (parse_leader (i)
          && parse_year (i, &year)
          && parse_date_delimiter (i)
          && parse_month (i, &month)
          && parse_date_delimiter (i)
          && parse_day (i, &day)
          && parse_trailer (i)
          && ymd_to_date (i, year, month, day, &i->v->f));
}

static bool
parse_JDATE (struct data_in *i)
{
  long julian;
  double ofs;
  
  if (!parse_leader (i)
      || !parse_julian (i, &julian)
      || !parse_trailer (i)
      || !ymd_to_ofs (i, julian / 1000, 1, 1, &ofs))
    return false;

  i->v->f = (ofs + julian % 1000 - 1) * 60. * 60. * 24.;
  return true;
}

static bool
parse_QYR (struct data_in *i)
{
  long quarter, year;

  return (parse_leader (i)
          && parse_quarter (i, &quarter)
          && parse_q_delimiter (i)
          && parse_year (i, &year)
          && parse_trailer (i)
          && ymd_to_date (i, year, (quarter - 1) * 3 + 1, 1, &i->v->f));
}

static bool
parse_MOYR (struct data_in *i)
{
  long month, year;

  return (parse_leader (i)
          && parse_month (i, &month)
          && parse_date_delimiter (i)
          && parse_year (i, &year)
          && parse_trailer (i)
          && ymd_to_date (i, year, month, 1, &i->v->f));
}

static bool
parse_WKYR (struct data_in *i)
{
  long week, year;
  double ofs;

  if (!parse_leader (i)
      || !parse_week (i, &week)
      || !parse_wk_delimiter (i)
      || !parse_year (i, &year)
      || !parse_trailer (i))
    return false;

  if (year != 1582) 
    {
      if (!ymd_to_ofs (i, year, 1, 1, &ofs))
        return false;
    }
  else 
    {
      if (ymd_to_ofs (i, 1583, 1, 1, &ofs))
        return false;
      ofs -= 365;
    }

  i->v->f = (ofs + (week - 1) * 7) * 60. * 60. * 24.;
  return true;
}

static bool
parse_TIME (struct data_in *i)
{
  int sign;
  double second;
  long hour, minute;

  if (!parse_leader (i)
      || !parse_sign (i, &sign)
      || !parse_spaces (i)
      || !parse_hour (i, &hour)
      || !parse_time_delimiter (i)
      || !parse_minute (i, &minute)
      || !parse_opt_second (i, &second))
    return false;

  i->v->f = (hour * 60. * 60. + minute * 60. + second) * sign;
  return true;
}

static bool
parse_DTIME (struct data_in *i)
{
  int sign;
  long day_count, hour;
  double second;
  long minute;

  if (!parse_leader (i)
      || !parse_sign (i, &sign)
      || !parse_spaces (i)
      || !parse_day_count (i, &day_count)
      || !parse_time_delimiter (i)
      || !parse_hour (i, &hour)
      || !parse_time_delimiter (i)
      || !parse_minute (i, &minute)
      || !parse_opt_second (i, &second))
    return false;

  i->v->f = (day_count * 60. * 60. * 24.
	     + hour * 60. * 60.
	     + minute * 60.
	     + second) * sign;
  return true;
}

static bool
parse_DATETIME (struct data_in *i)
{
  long day, month, year;
  long hour24;
  double second;
  long minute;

  if (!parse_leader (i)
      || !parse_day (i, &day)
      || !parse_date_delimiter (i)
      || !parse_month (i, &month)
      || !parse_date_delimiter (i)
      || !parse_year (i, &year)
      || !parse_time_delimiter (i)
      || !parse_hour24 (i, &hour24)
      || !parse_time_delimiter (i)
      || !parse_minute (i, &minute)
      || !parse_opt_second (i, &second)
      || !ymd_to_date (i, year, month, day, &i->v->f))
    return false;

  i->v->f += hour24 * 60. * 60. + minute * 60. + second;
  return true;
}

static bool
parse_WKDAY (struct data_in *i)
{
  long weekday;

  if (!parse_leader (i)
      || !parse_weekday (i, &weekday)
      || !parse_trailer (i))
    return false;

  i->v->f = weekday;
  return true;
}

static bool
parse_MONTH (struct data_in *i)
{
  long month;

  if (!parse_leader (i)
      || !parse_month (i, &month)
      || !parse_trailer (i))
    return false;

  i->v->f = month;
  return true;
}

/* Main dispatcher. */

static void
default_result (struct data_in *i)
{
  const struct fmt_desc *const fmt = &formats[i->format.type];

  /* Default to SYSMIS or blanks. */
  if (fmt->cat & FCAT_STRING)
    memset (i->v->s, ' ', i->format.w);
  else
    i->v->f = get_blanks();
}

bool
data_in (struct data_in *i)
{
  const struct fmt_desc *const fmt = &formats[i->format.type];

  assert (check_input_specifier (&i->format, 0));

  /* Check that we've got a string to work with. */
  if (i->e == i->s || i->format.w <= 0)
    {
      default_result (i);
      return true;
    }

  i->f2 = i->f1 + (i->e - i->s) - 1;

  /* Make sure that the string isn't too long. */
  if (i->format.w > fmt->Imax_w)
    {
      dls_error (i, _("Field too long (%d characters).  Truncated after "
		   "character %d."),
		 i->format.w, fmt->Imax_w);
      i->format.w = fmt->Imax_w;
    }

  if (fmt->cat & FCAT_BLANKS_SYSMIS)
    {
      const unsigned char *cp;

      cp = i->s;
      for (;;)
	{
	  if (!isspace (*cp))
	    break;

	  if (++cp == i->e)
	    {
	      i->v->f = get_blanks();
	      return true;
	    }
	}
    }
  
  {
    static bool (*const handlers[FMT_NUMBER_OF_FORMATS])(struct data_in *) = 
      {
	parse_numeric, parse_N, parse_numeric, parse_numeric,
	parse_numeric, parse_numeric, parse_numeric,
	parse_Z, parse_A, parse_AHEX, parse_IB, parse_P, parse_PIB,
	parse_PIBHEX, parse_PK, parse_RB, parse_RBHEX,
	NULL, NULL, NULL, NULL, NULL,
	parse_DATE, parse_EDATE, parse_SDATE, parse_ADATE, parse_JDATE,
	parse_QYR, parse_MOYR, parse_WKYR,
	parse_DATETIME, parse_TIME, parse_DTIME,
	parse_WKDAY, parse_MONTH,
      };

    bool (*handler)(struct data_in *);
    bool success;

    handler = handlers[i->format.type];
    assert (handler != NULL);

    success = handler (i);
    if (!success)
      default_result (i);

    return success;
  }
}

/* Utility function. */

/* Sets DI->{s,e} appropriately given that LINE has length LEN and the
   field starts at one-based column FC and ends at one-based column
   LC, inclusive. */
void
data_in_finite_line (struct data_in *di, const char *line, size_t len,
		     int fc, int lc)
{
  di->s = line + ((size_t) fc <= len ? fc - 1 : len);
  di->e = line + ((size_t) lc <= len ? lc : len);
}
