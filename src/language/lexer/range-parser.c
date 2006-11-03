/* PSPP - computes sample statistics.
   Copyright (C) 2005, 2006 Free Software Foundation, Inc.
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
#include "range-parser.h"
#include <stdbool.h>
#include <data/data-in.h>
#include <libpspp/message.h>
#include "lexer.h"
#include <libpspp/magic.h>
#include <libpspp/str.h>
#include <data/value.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static bool parse_number (double *, const struct fmt_spec *);

/* Parses and stores a numeric value, or a range of the form "x
   THRU y".  Open-ended ranges may be specified as "LO(WEST) THRU
   y" or "x THRU HI(GHEST)".  Sets *X and *Y to the range or the
   value and returns success.

   Numeric values are always accepted.  If F is nonnull, then
   string values are also accepted, and converted to numeric
   values using the specified format. */
bool
parse_num_range (double *x, double *y, const struct fmt_spec *f) 
{
  if (lex_match_id ("LO") || lex_match_id ("LOWEST"))
    *x = LOWEST;
  else if (!parse_number (x, f))
    return false;

  if (lex_match_id ("THRU")) 
    {
      if (lex_match_id ("HI") || lex_match_id ("HIGHEST"))
        *y = HIGHEST;
      else if (!parse_number (y, f))
        return false;

      if (*y < *x) 
        {
          double t;
          msg (SW, _("Low end of range (%g) is below high end (%g).  "
                     "The range will be treated as reversed."),
               *x, *y);
          t = *x;
          *x = *y;
          *y = t;
        }
      else if (*x == *y) 
        msg (SW, _("Ends of range are equal (%g)."), *x);

      return true;
    }
  else
    {
      if (*x == LOWEST) 
        {
          msg (SE, _("LO or LOWEST must be part of a range."));
          return false;
        }
      *y = *x;
    }
  
  return true;
}

/* Parses a number and stores it in *X.  Returns success.

   Numeric values are always accepted.  If F is nonnull, then
   string values are also accepted, and converted to numeric
   values using the specified format. */
static bool
parse_number (double *x, const struct fmt_spec *f)
{
  if (lex_is_number ()) 
    {
      *x = lex_number ();
      lex_get ();
      return true;
    }
  else if (token == T_STRING && f != NULL) 
    {
      struct data_in di;
      union value v;
      di.s = ds_data (&tokstr);
      di.e = ds_end (&tokstr);
      di.v = &v;
      di.flags = 0;
      di.f1 = 1;
      di.f2 = ds_length (&tokstr);
      di.format = *f;
      data_in (&di);
      lex_get ();
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
      if (f != NULL)
        lex_error (_("expecting number or data string"));
      else
        lex_force_num ();
      return false; 
    }
}
