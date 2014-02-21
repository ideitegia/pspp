/* PSPP - a program for statistical analysis.
   Copyright (C) 2005, 2006, 2009, 2010, 2011, 2014 Free Software Foundation, Inc.

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

#include "value-parser.h"

#include <float.h>
#include <stdbool.h>

#include "data/data-in.h"
#include "data/format.h"
#include "data/value.h"
#include "language/lexer/lexer.h"
#include "libpspp/cast.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static bool parse_number (struct lexer *, double *, const enum fmt_type *);

/* Parses and stores a numeric value, or a range of the form "x
   THRU y".  Open-ended ranges may be specified as "LO(WEST) THRU
   y" or "x THRU HI(GHEST)".  Sets *X and *Y to the range or the
   value and returns success.

   Numeric values are always accepted.  If FORMAT is nonnull,
   then string values are also accepted, and converted to numeric
   values using *FORMAT. */
bool
parse_num_range (struct lexer *lexer,
                 double *x, double *y, const enum fmt_type *format)
{
  if (lex_match_id (lexer, "LO") || lex_match_id (lexer, "LOWEST"))
    *x = LOWEST;
  else if (!parse_number (lexer, x, format))
    return false;

  if (lex_match_id (lexer, "THRU"))
    {
      if (lex_match_id (lexer, "HI") || lex_match_id (lexer, "HIGHEST"))
        *y = HIGHEST;
      else if (!parse_number (lexer, y, format))
        return false;

      if (*y < *x)
        {
          double t;
          msg (SW, _("The high end of the range (%.*g) is below the low end "
                     "(%.*g).  The range will be treated as if reversed."),
               DBL_DIG + 1, *y, DBL_DIG + 1, *x);
          t = *x;
          *x = *y;
          *y = t;
        }
      else if (*x == *y)
        msg (SW, _("Ends of range are equal (%.*g)."), DBL_DIG + 1, *x);

      return true;
    }
  else
    {
      if (*x == LOWEST)
        {
          msg (SE, _("%s or %s must be part of a range."), "LO", "LOWEEST");
          return false;
        }
      *y = *x;
    }

  return true;
}

/* Parses a number and stores it in *X.  Returns success.

   Numeric values are always accepted.  If FORMAT is nonnull,
   then string values are also accepted, and converted to numeric
   values using *FORMAT. */
static bool
parse_number (struct lexer *lexer, double *x, const enum fmt_type *format)
{
  if (lex_is_number (lexer))
    {
      *x = lex_number (lexer);
      lex_get (lexer);
      return true;
    }
  else if (lex_is_string (lexer) && format != NULL)
    {
      union value v;

      assert (fmt_get_category (*format) != FMT_CAT_STRING);

      if (!data_in_msg (lex_tokss (lexer), "UTF-8", *format, &v, 0, NULL))
        return false;

      lex_get (lexer);
      *x = v.f;
      if (*x == SYSMIS)
        {
          msg (SE, _("System-missing value is not valid here."));
          return false;
        }
      return true;
    }
  else
    {
      if (format != NULL)
        lex_error (lexer, _("expecting number or data string"));
      else
        lex_force_num (lexer);
      return false;
    }
}

/* Parses the current token from LEXER into value V, which must already have
   been initialized with the specified VAR's WIDTH.  Returns true if
   successful, false otherwise. */
bool
parse_value (struct lexer *lexer, union value *v, const struct variable *var)
{
  int width = var_get_width (var);
  if (width == 0)
    return parse_number (lexer, &v->f, &var_get_print_format (var)->type);
  else if (lex_force_string (lexer))
    {
      const char *s = lex_tokcstr (lexer);
      value_copy_str_rpad (v, width, CHAR_CAST_BUG (const uint8_t *, s), ' ');
    }
  else
    return false;

  lex_get (lexer);

  return true;
}
