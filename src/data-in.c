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
#include "data-in.h"
#include "error.h"
#include <math.h>
#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "error.h"
#include "getline.h"
#include "julcal/julcal.h"
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
dls_error (const struct data_in *i, const char *format, ...)
{
  char buf[1024];

  if (i->flags & DI_IGNORE_ERROR)
    return;

  {
    va_list args;

    va_start (args, format);
    snprintf (buf, 1024, format, args);
    va_end (args);
  }
  
  {
    struct error e;
    struct string title;

    ds_init (NULL, &title, 64);
    if (!getl_reading_script)
      ds_concat (&title, _("data-file error: "));
    if (i->f1 == i->f2)
      ds_printf (&title, _("(column %d"), i->f1);
    else
      ds_printf (&title, _("(columns %d-%d"), i->f1, i->f2);
    ds_printf (&title, _(", field type %s) "), fmt_to_string (&i->format));
    
    e.class = DE;
    err_location (&e.where);
    e.title = ds_value (&title);
    e.text = buf;

    err_vmsg (&e);

    ds_destroy (&title);
  }
}

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
static inline int
have_char (struct data_in *i)
{
  return i->s < i->e;
}

/* Format parsers. */ 

static int parse_int (struct data_in *i, long *result);

/* This function is based on strtod() from the GNU C library. */
static int
parse_numeric (struct data_in *i)
{
  short int sign;		/* +1 or -1. */
  double num;			/* The number so far.  */

  int got_dot;			/* Found a decimal point.  */
  int got_digit;		/* Count of digits.  */

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
  got_dot = 0;
  got_digit = 0;
  exponent = 0;
  for (; have_char (i); i->s++)
    {
      if (isdigit (*i->s))
	{
	  got_digit++;

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
	got_dot = 1;
      else if ((type != FMT_COMMA && type != FMT_DOT) || *i->s != grouping)
	/* Any other character terminates the number.  */
	break;
    }

  if (!got_digit)
    {
      if (got_dot)
	{
	  i->v->f = SYSMIS;
	  return 1;
	}
      goto noconv;
    }
  
  if (have_char (i)
      && (tolower (*i->s) == 'e' || tolower (*i->s) == 'd'
	  || (type == FMT_E && (*i->s == '+' || *i->s == '-'))))
    {
      /* Get the exponent specified after the `e' or `E'.  */
      long exp;

      if (isalpha (*i->s))
	i->s++;
      if (!parse_int (i, &exp))
	goto noconv;

      exponent += exp;
    }
  else if (!got_dot)
    exponent -= i->format.d;

  if (type == FMT_PCT && have_char (i) && *i->s == '%')
    i->s++;
  if (i->s < i->e)
    {
      dls_error (i, _("Field contents followed by garbage."));
      i->v->f = SYSMIS;
      return 0;
    }

  if (num == 0.0)
    {
      i->v->f = 0.0;
      return 1;
    }

  /* Multiply NUM by 10 to the EXPONENT power, checking for overflow
     and underflow.  */

  if (exponent < 0)
    {
      if (-exponent + got_digit > -(DBL_MIN_10_EXP) + 5
	  || num < DBL_MIN * pow (10.0, (double) -exponent))
	goto underflow;
      num *= pow (10.0, (double) exponent);
    }
  else if (exponent > 0)
    {
      if (num > DBL_MAX * pow (10.0, (double) -exponent))
	goto overflow;
      num *= pow (10.0, (double) exponent);
    }

  i->v->f = sign * num;
  return 1;

overflow:
  /* Return an overflow error.  */
  dls_error (i, _("Overflow in floating-point constant."));
  i->v->f = SYSMIS;
  return 0;

underflow:
  /* Return an underflow error.  */
  dls_error (i, _("Underflow in floating-point constant."));
  i->v->f = 0.0;
  return 0;

noconv:
  /* There was no number.  */
  dls_error (i, _("Field does not form a valid floating-point constant."));
  i->v->f = SYSMIS;
  return 0;
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

static inline int
parse_N (struct data_in *i)
{
  const unsigned char *cp;

  i->v->f = 0;
  for (cp = i->s; cp < i->e; cp++)
    {
      if (!isdigit (*cp))
	{
	  dls_error (i, _("All characters in field must be digits."));
	  return 0;
	}

      i->v->f = i->v->f * 10.0 + *cp - '0';
    }

  if (i->format.d)
    i->v->f /= pow (10.0, i->format.d);
  return 1;
}

static inline int
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
	  return 0;
	}

      n = n * 16.0 + hexit_value (*cp);
    }
  
  i->v->f = n;
  return 1;
}

static inline int
parse_RBHEX (struct data_in *i)
{
  /* Validate input. */
  trim_whitespace (i);
  if ((i->e - i->s) % 2)
    {
      dls_error (i, _("Field must have even length."));
      return 0;
    }
  
  {
    const unsigned char *cp;
    
    for (cp = i->s; cp < i->e; cp++)
      if (!isxdigit (*cp))
	{
	  dls_error (i, _("Field must contain only hex digits."));
	  return 0;
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
  
  return 1;
}

static inline int
parse_Z (struct data_in *i)
{
  char buf[64];

  /* Warn user that we suck. */
  {
    static int warned;

    if (!warned)
      {
	msg (MW, 
	     _("Quality of zoned decimal (Z) input format code is "
	       "suspect.  Check your results three times. Report bugs "
		"to %s."),PACKAGE_BUGREPORT);
	warned = 1;
      }
  }

  /* Validate input. */
  trim_whitespace (i);

  if (i->e - i->s < 2)
    {
      dls_error (i, _("Zoned decimal field contains fewer than 2 "
		      "characters."));
      return 0;
    }

  /* Copy sign into buf[0]. */
  if ((i->e[-1] & 0xc0) != 0xc0)
    {
      dls_error (i, _("Bad sign byte in zoned decimal number."));
      return 0;
    }
  buf[0] = (i->e[-1] ^ (i->e[-1] >> 1)) & 0x10 ? '-' : '+';

  /* Copy digits into buf[1 ... len - 1] and terminate string. */
  {
    const unsigned char *sp;
    char *dp;

    for (sp = i->s, dp = buf + 1; sp < i->e - 1; sp++, dp++)
      if (*sp == '.')
	*dp = '.';
      else if ((*sp & 0xf0) == 0xf0 && (*sp & 0xf) < 10)
	*dp = (*sp & 0xf) + '0';
      else
	{
	  dls_error (i, _("Format error in zoned decimal number."));
	  return 0;
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
	return 0;
      }
  }
  
  return 1;
}

static inline int
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
  mm_reverse (buf, i->e - i->s);
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

  if (i->format.d)
    i->v->f /= pow (10.0, i->format.d);

  return 1;
}

static inline int
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

  if (i->format.d)
    i->v->f /= pow (10.0, i->format.d);

  return 1;
}

static inline int
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

  if (i->format.d)
    i->v->f /= pow (10.0, i->format.d);

  return 1;
}

static inline int
parse_PK (struct data_in *i)
{
  const unsigned char *cp;

  i->v->f = 0.0;
  for (cp = i->s; cp < i->e; cp++)
    {
      i->v->f = i->v->f * 10 + (*cp >> 4);
      i->v->f = i->v->f * 10 + (*cp & 15);
    }

  if (i->format.d)
    i->v->f /= pow (10.0, i->format.d);

  return 1;
}

static inline int
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

  return 1;
}

static inline int
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

  return 1;
}

static inline int
parse_AHEX (struct data_in *i)
{
  /* Validate input. */
  trim_whitespace (i);
  if ((i->e - i->s) % 2)
    {
      dls_error (i, _("Field must have even length."));
      return 0;
    }

  {
    const unsigned char *cp;
    
    for (cp = i->s; cp < i->e; cp++)
      if (!isxdigit (*cp))
	{
	  dls_error (i, _("Field must contain only hex digits."));
	  return 0;
	}
  }
  
  {
    int j;
    
    /* Parse input. */
    for (j = 0; j < min (i->e - i->s, i->format.w); j += 2)
      i->v->s[j / 2] = hexit_value (i->s[j]) * 16 + hexit_value (i->s[j + 1]);
    memset (i->v->s + (i->e - i->s) / 2, ' ', (i->format.w - (i->e - i->s)) / 2);
  }
  
  return 1;
}

/* Date & time format components. */

/* Advances *CP past any whitespace characters. */
static inline void
skip_whitespace (struct data_in *i)
{
  while (isspace ((unsigned char) *i->s))
    i->s++;
}

static inline int
parse_leader (struct data_in *i)
{
  skip_whitespace (i);
  return 1;
}

static inline int
force_have_char (struct data_in *i)
{
  if (have_char (i))
    return 1;

  dls_error (i, _("Unexpected end of field."));
  return 0;
}

static int
parse_int (struct data_in *i, long *result)
{
  int negative = 0;
  
  if (!force_have_char (i))
    return 0;

  if (*i->s == '+')
    {
      i->s++;
      force_have_char (i);
    }
  else if (*i->s == '-')
    {
      negative = 1;
      i->s++;
      force_have_char (i);
    }
  
  if (!isdigit (*i->s))
    {
      dls_error (i, _("Digit expected in field."));
      return 0;
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
  return 1;
}

static int
parse_day (struct data_in *i, long *day)
{
  if (!parse_int (i, day))
    return 0;
  if (*day >= 1 && *day <= 31)
    return 1;

  dls_error (i, _("Day (%ld) must be between 1 and 31."), *day);
  return 0;
}

static int
parse_day_count (struct data_in *i, long *day_count)
{
  return parse_int (i, day_count);
}

static int
parse_date_delimiter (struct data_in *i)
{
  int delim = 0;

  while (have_char (i)
	 && (*i->s == '-' || *i->s == '/' || isspace (*i->s)
	     || *i->s == '.' || *i->s == ','))
    {
      delim = 1;
      i->s++;
    }
  if (delim)
    return 1;

  dls_error (i, _("Delimiter expected between fields in date."));
  return 0;
}

/* Formats NUMBER as Roman numerals in ROMAN, or as Arabic numerals if
   the Roman expansion would be too long. */
static void
to_roman (int number, char roman[32])
{
  int save_number = number;

  struct roman_digit
    {
      int value;		/* Value corresponding to this digit. */
      char name;		/* Digit name. */
    };

  static const struct roman_digit roman_tab[7] =
  {
    {1000, 'M'},
    {500, 'D'},
    {100, 'C'},
    {50, 'L'},
    {10, 'X'},
    {5, 'V'},
    {1, 'I'},
  };

  char *cp = roman;

  int i, j;

  assert (32 >= INT_DIGITS + 1);
  if (number == 0)
    goto arabic;

  if (number < 0)
    {
      *cp++ = '-';
      number = -number;
    }

  for (i = 0; i < 7; i++)
    {
      int digit = roman_tab[i].value;
      while (number >= digit)
	{
	  number -= digit;
	  if (cp > &roman[30])
	    goto arabic;
	  *cp++ = roman_tab[i].name;
	}

      for (j = i + 1; j < 7; j++)
	{
	  if (i == 4 && j == 5)	/* VX is not a shortened form of V. */
	    break;

	  digit = roman_tab[i].value - roman_tab[j].value;
	  while (number >= digit)
	    {
	      number -= digit;
	      if (cp > &roman[29])
		goto arabic;
	      *cp++ = roman_tab[j].name;
	      *cp++ = roman_tab[i].name;
	    }
	}
    }
  *cp = 0;
  return;

arabic:
  sprintf (roman, "%d", save_number);
}

/* Returns true if C is a (lowercase) roman numeral. */
#define CHAR_IS_ROMAN(C)				\
	((C) == 'x' || (C) == 'v' || (C) == 'i')

/* Returns the value of a single (lowercase) roman numeral. */
#define ROMAN_VALUE(C)				\
	((C) == 'x' ? 10 : ((C) == 'v' ? 5 : 1))

static int
parse_month (struct data_in *i, long *month)
{
  if (!force_have_char (i))
    return 0;
  
  if (isdigit (*i->s))
    {
      if (!parse_int (i, month))
	return 0;
      if (*month >= 1 && *month <= 12)
	return 1;
      
      dls_error (i, _("Month (%ld) must be between 1 and 12."), *month);
      return 0;
    }

  if (CHAR_IS_ROMAN (tolower (*i->s)))
    {
      int last = ROMAN_VALUE (tolower (*i->s));

      *month = 0;
      for (;;)
	{
	  int value;

	  i->s++;
	  if (!have_char || !CHAR_IS_ROMAN (tolower (*i->s)))
	    {
	      if (last != INT_MAX)
		*month += last;
	      break;
	    }

	  value = ROMAN_VALUE (tolower (*i->s));
	  if (last == INT_MAX)
	    *month += value;
	  else if (value > last)
	    {
	      *month += value - last;
	      last = INT_MAX;
	    }
	  else
	    {
	      *month += last;
	      last = value;
	    }
	}

      if (*month < 1 || *month > 12)
	{
	  char buf[32];

	  to_roman (*month, buf);
	  dls_error (i, _("Month (%s) must be between I and XII."), buf);
	  return 0;
	}
      
      return 1;
    }
  
  {
    static const char *months[12] =
      {
	"january", "february", "march", "april", "may", "june",
	"july", "august", "september", "october", "november", "december",
      };

    char month_buf[32];
    char *mp;

    int j;

    for (mp = month_buf;
	 have_char (i) && isalpha (*i->s) && mp < &month_buf[31];
	 i->s++)
      *mp++ = tolower (*i->s);
    *mp = '\0';

    if (have_char (i) && isalpha (*i->s))
      {
	dls_error (i, _("Month name (%s...) is too long."), month_buf);
	return 0;
      }

    for (j = 0; j < 12; j++)
      if (lex_id_match (months[j], month_buf))
	{
	  *month = j + 1;
	  return 1;
	}

    dls_error (i, _("Bad month name (%s)."), month_buf);
    return 0;
  }
}

static int
parse_year (struct data_in *i, long *year)
{
  if (!parse_int (i, year))
    return 0;
  
  if (*year >= 0 && *year <= 199)
    *year += 1900;
  if (*year >= 1582 || *year <= 19999)
    return 1;

  dls_error (i, _("Year (%ld) must be between 1582 and 19999."), *year);
  return 0;
}

static int
parse_trailer (struct data_in *i)
{
  skip_whitespace (i);
  if (!have_char (i))
    return 1;
  
  dls_error (i, _("Trailing garbage \"%s\" following date."), i->s);
  return 0;
}

static int
parse_julian (struct data_in *i, long *julian)
{
  if (!parse_int (i, julian))
    return 0;
   
  {
    int day = *julian % 1000;

    if (day < 1 || day > 366)
      {
	dls_error (i, _("Julian day (%d) must be between 1 and 366."), day);
	return 0;
      }
  }
  
  {
    int year = *julian / 1000;

    if (year >= 0 && year <= 199)
      *julian += 1900000L;
    else if (year < 1582 || year > 19999)
      {
	dls_error (i, _("Year (%d) must be between 1582 and 19999."), year);
	return 0;
      }
  }

  return 1;
}

static int
parse_quarter (struct data_in *i, long *quarter)
{
  if (!parse_int (i, quarter))
    return 0;
  if (*quarter >= 1 && *quarter <= 4)
    return 1;

  dls_error (i, _("Quarter (%ld) must be between 1 and 4."), *quarter);
  return 0;
}

static int
parse_q_delimiter (struct data_in *i)
{
  skip_whitespace (i);
  if (!have_char (i) || tolower (*i->s) != 'q')
    {
      dls_error (i, _("`Q' expected between quarter and year."));
      return 0;
    }
  i->s++;
  skip_whitespace (i);
  return 1;
}

static int
parse_week (struct data_in *i, long *week)
{
  if (!parse_int (i, week))
    return 0;
  if (*week >= 1 && *week <= 53)
    return 1;

  dls_error (i, _("Week (%ld) must be between 1 and 53."), *week);
  return 0;
}

static int
parse_wk_delimiter (struct data_in *i)
{
  skip_whitespace (i);
  if (i->s + 1 >= i->e
      || tolower (i->s[0]) != 'w' || tolower (i->s[1]) != 'k')
    {
      dls_error (i, _("`WK' expected between week and year."));
      return 0;
    }
  i->s += 2;
  skip_whitespace (i);
  return 1;
}

static int
parse_time_delimiter (struct data_in *i)
{
  int delim = 0;

  while (have_char (i)
	 && (*i->s == ':' || *i->s == '.' || isspace (*i->s)))
    {
      delim = 1;
      i->s++;
    }

  if (delim)
    return 1;
  
  dls_error (i, _("Delimiter expected between fields in time."));
  return 0;
}

static int
parse_hour (struct data_in *i, long *hour)
{
  if (!parse_int (i, hour))
    return 0;
  if (*hour >= 0)
    return 1;
  
  dls_error (i, _("Hour (%ld) must be positive."), *hour);
  return 0;
}

static int
parse_minute (struct data_in *i, long *minute)
{
  if (!parse_int (i, minute))
    return 0;
  if (*minute >= 0 && *minute <= 59)
    return 1;
  
  dls_error (i, _("Minute (%ld) must be between 0 and 59."), *minute);
  return 0;
}

static int
parse_opt_second (struct data_in *i, double *second)
{
  int delim = 0;

  char buf[64];
  char *cp;

  while (have_char (i)
	 && (*i->s == ':' || *i->s == '.' || isspace (*i->s)))
    {
      delim = 1;
      i->s++;
    }
  
  if (!delim || !isdigit (*i->s))
    {
      *second = 0.0;
      return 1;
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

  return 1;
}

static int
parse_hour24 (struct data_in *i, long *hour24)
{
  if (!parse_int (i, hour24))
    return 0;
  if (*hour24 >= 0 && *hour24 <= 23)
    return 1;
  
  dls_error (i, _("Hour (%ld) must be between 0 and 23."), *hour24);
  return 0;
}

     
static int
parse_weekday (struct data_in *i, int *weekday)
{
  /* PORTME */
  #define TUPLE(A,B) 				\
	  (((A) << 8) + (B))

  if (i->s + 1 >= i->e)
    {
      dls_error (i, _("Day of the week expected in date value."));
      return 0;
    }

  switch (TUPLE (tolower (i->s[0]), tolower (i->s[1])))
    {
    case TUPLE ('s', 'u'):
      *weekday = 1;
      break;

    case TUPLE ('m', 'o'):
      *weekday = 2;
      break;

    case TUPLE ('t', 'u'):
      *weekday = 3;
      break;

    case TUPLE ('w', 'e'):
      *weekday = 4;
      break;

    case TUPLE ('t', 'h'):
      *weekday = 5;
      break;

    case TUPLE ('f', 'r'):
      *weekday = 6;
      break;

    case TUPLE ('s', 'a'):
      *weekday = 7;
      break;

    default:
      dls_error (i, _("Day of the week expected in date value."));
      return 0;
    }

  while (have_char (i) && isalpha (*i->s))
    i->s++;

  return 1;

  #undef TUPLE
}

static int
parse_spaces (struct data_in *i)
{
  skip_whitespace (i);
  return 1;
}

static int
parse_sign (struct data_in *i, int *sign)
{
  if (!force_have_char (i))
    return 0;

  switch (*i->s)
    {
    case '-':
      i->s++;
      *sign = 1;
      break;

    case '+':
      i->s++;
      /* fall through */

    default:
      *sign = 0;
      break;
    }

  return 1;
}

/* Date & time formats. */

static int
valid_date (struct data_in *i)
{
  if (i->v->f == SYSMIS)
    {
      dls_error (i, _("Date is not in valid range between "
		   "15 Oct 1582 and 31 Dec 19999."));
      return 0;
    }
  else
    return 1;
}

static int
parse_DATE (struct data_in *i)
{
  long day, month, year;

  if (!parse_leader (i)
      || !parse_day (i, &day)
      || !parse_date_delimiter (i)
      || !parse_month (i, &month)
      || !parse_date_delimiter (i)
      || !parse_year (i, &year)
      || !parse_trailer (i))
    return 0;

  i->v->f = calendar_to_julian (year, month, day);
  if (!valid_date (i))
    return 0;
  i->v->f *= 60. * 60. * 24.;

  return 1;
}

static int
parse_ADATE (struct data_in *i)
{
  long month, day, year;

  if (!parse_leader (i)
      || !parse_month (i, &month)
      || !parse_date_delimiter (i)
      || !parse_day (i, &day)
      || !parse_date_delimiter (i)
      || !parse_year (i, &year)
      || !parse_trailer (i))
    return 0;

  i->v->f = calendar_to_julian (year, month, day);
  if (!valid_date (i))
    return 0;
  i->v->f *= 60. * 60. * 24.;

  return 1;
}

static int
parse_EDATE (struct data_in *i)
{
  long month, day, year;

  if (!parse_leader (i)
      || !parse_day (i, &day)
      || !parse_date_delimiter (i)
      || !parse_month (i, &month)
      || !parse_date_delimiter (i)
      || !parse_year (i, &year)
      || !parse_trailer (i))
    return 0;

  i->v->f = calendar_to_julian (year, month, day);
  if (!valid_date (i))
    return 0;
  i->v->f *= 60. * 60. * 24.;

  return 1;
}

static int
parse_SDATE (struct data_in *i)
{
  long month, day, year;

  if (!parse_leader (i)
      || !parse_year (i, &year)
      || !parse_date_delimiter (i)
      || !parse_month (i, &month)
      || !parse_date_delimiter (i)
      || !parse_day (i, &day)
      || !parse_trailer (i))
    return 0;

  i->v->f = calendar_to_julian (year, month, day);
  if (!valid_date (i))
    return 0;
  i->v->f *= 60. * 60. * 24.;

  return 1;
}

static int
parse_JDATE (struct data_in *i)
{
  long julian;
  
  if (!parse_leader (i)
      || !parse_julian (i, &julian)
      || !parse_trailer (i))
    return 0;

  if (julian / 1000 == 1582)
    i->v->f = calendar_to_julian (1583, 1, 1) - 365;
  else
    i->v->f = calendar_to_julian (julian / 1000, 1, 1);

  if (valid_date (i))
    {
      i->v->f = (i->v->f + julian % 1000 - 1) * 60. * 60. * 24.;
      if (i->v->f < 0.)
	i->v->f = SYSMIS;
    }

  return valid_date (i);
}

static int
parse_QYR (struct data_in *i)
{
  long quarter, year;

  if (!parse_leader (i)
      || !parse_quarter (i, &quarter)
      || !parse_q_delimiter (i)
      || !parse_year (i, &year)
      || !parse_trailer (i))
    return 0;

  i->v->f = calendar_to_julian (year, (quarter - 1) * 3 + 1, 1);
  if (!valid_date (i))
    return 0;
  i->v->f *= 60. * 60. * 24.;

  return 1;
}

static int
parse_MOYR (struct data_in *i)
{
  long month, year;

  if (!parse_leader (i)
      || !parse_month (i, &month)
      || !parse_date_delimiter (i)
      || !parse_year (i, &year)
      || !parse_trailer (i))
    return 0;

  i->v->f = calendar_to_julian (year, month, 1);
  if (!valid_date (i))
    return 0;
  i->v->f *= 60. * 60. * 24.;

  return 1;
}

static int
parse_WKYR (struct data_in *i)
{
  long week, year;

  if (!parse_leader (i)
      || !parse_week (i, &week)
      || !parse_wk_delimiter (i)
      || !parse_year (i, &year)
      || !parse_trailer (i))
    return 0;

  i->v->f = calendar_to_julian (year, 1, 1);
  if (!valid_date (i))
    return 0;
  i->v->f = (i->v->f + (week - 1) * 7) * 60. * 60. * 24.;

  return 1;
}

static int
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
    return 0;

  i->v->f = hour * 60. * 60. + minute * 60. + second;
  if (sign)
    i->v->f = -i->v->f;
  return 1;
}

static int
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
    return 0;

  i->v->f = (day_count * 60. * 60. * 24.
	     + hour * 60. * 60.
	     + minute * 60.
	     + second);
  if (sign)
    i->v->f = -i->v->f;
  return 1;
}

static int
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
      || !parse_opt_second (i, &second))
    return 0;

  i->v->f = calendar_to_julian (year, month, day);
  if (!valid_date (i))
    return 0;
  i->v->f = (i->v->f * 60. * 60. * 24.
	     + hour24 * 60. * 60.
	     + minute * 60.
	     + second);

  return 1;
}

static int
parse_WKDAY (struct data_in *i)
{
  int weekday;

  if (!parse_leader (i)
      || !parse_weekday (i, &weekday)
      || !parse_trailer (i))
    return 0;

  i->v->f = weekday;
  return 1;
}

static int
parse_MONTH (struct data_in *i)
{
  long month;

  if (!parse_leader (i)
      || !parse_month (i, &month)
      || !parse_trailer (i))
    return 0;

  i->v->f = month;
  return 1;
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

int
data_in (struct data_in *i)
{
  const struct fmt_desc *const fmt = &formats[i->format.type];

  /* Check that we've got a string to work with. */
  if (i->e == i->s || i->format.w <= 0)
    {
      default_result (i);
      return 1;
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
	      return 1;
	    }
	}
    }
  
  {
    static int (*const handlers[FMT_NUMBER_OF_FORMATS])(struct data_in *) = 
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

    int (*handler)(struct data_in *);
    int success;

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
