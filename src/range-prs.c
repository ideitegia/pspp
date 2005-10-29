#include <config.h>
#include "range-prs.h"
#include <stdbool.h>
#include "data-in.h"
#include "error.h"
#include "lexer.h"
#include "magic.h"
#include "str.h"
#include "val.h"

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
          lex_error (_("System-missing value is not valid here."));
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
