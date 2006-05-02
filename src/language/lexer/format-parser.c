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
#include <data/format.h>
#include <ctype.h>
#include <libpspp/message.h>
#include <stdlib.h>
#include <libpspp/message.h>
#include "lexer.h"
#include <libpspp/misc.h>
#include <libpspp/str.h>
#include <data/variable.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)


/* Parses the alphabetic prefix of the current token as a format
   specifier name.  Returns the corresponding format specifier
   type if successful, or -1 on failure.  If ALLOW_XT is zero,
   then X and T format specifiers are not allowed.  If CP is
   nonzero, then *CP is set to the first non-alphabetic character
   in the current token on success or to a null pointer on
   failure. */
int
parse_format_specifier_name (const char **cp, enum fmt_parse_flags flags)
{
  char *sp, *ep;
  int idx;

  sp = ep = ds_c_str (&tokstr);
  while (isalpha ((unsigned char) *ep))
    ep++;

  if (sp != ep) 
    {
      /* Find format. */
      for (idx = 0; idx < FMT_NUMBER_OF_FORMATS; idx++)
        if (strlen (formats[idx].name) == ep - sp
            && !buf_compare_case (formats[idx].name, sp, ep - sp))
          break;

      /* Check format. */
      if (idx < FMT_NUMBER_OF_FORMATS)
        {
          if (!(flags & FMTP_ALLOW_XT) && (idx == FMT_T || idx == FMT_X)) 
            {
              if (!(flags & FMTP_SUPPRESS_ERRORS))
                msg (SE, _("X and T format specifiers not allowed here."));
              idx = -1; 
            }
        }
      else 
        {
          /* No match. */
          if (!(flags & FMTP_SUPPRESS_ERRORS))
            msg (SE, _("%.*s is not a valid data format."),
                 (int) (ep - sp), ds_c_str (&tokstr));
          idx = -1; 
        }
    }
  else 
    {
      lex_error ("expecting data format");
      idx = -1;
    }
      
  if (cp != NULL) 
    {
      if (idx != -1)
        *cp = ep;
      else
        *cp = NULL;
    }

  return idx;
}


/* Parses a format specifier from the token stream and returns
   nonzero only if successful.  Emits an error message on
   failure.  Allows X and T format specifiers only if ALLOW_XT is
   nonzero.  The caller should call check_input_specifier() or
   check_output_specifier() on the parsed format as
   necessary.  */
int
parse_format_specifier (struct fmt_spec *input, enum fmt_parse_flags flags)
{
  struct fmt_spec spec;
  struct fmt_desc *f;
  const char *cp;
  char *cp2;
  int type, w, d;

  if (token != T_ID)
    {
      if (!(flags & FMTP_SUPPRESS_ERRORS))
        msg (SE, _("Format specifier expected."));
      return 0;
    }
  type = parse_format_specifier_name (&cp, flags);
  if (type == -1)
    return 0;
  f = &formats[type];

  w = strtol (cp, &cp2, 10);
  if (cp2 == cp && type != FMT_X)
    {
      if (!(flags & FMTP_SUPPRESS_ERRORS))
        msg (SE, _("Data format %s does not specify a width."),
             ds_c_str (&tokstr));
      return 0;
    }
  if ( w > MAX_STRING )
    {
      msg (SE, _("String variable width may not exceed %d"), MAX_STRING);
      return 0;
    }

  cp = cp2;
  if (f->n_args > 1 && *cp == '.')
    {
      cp++;
      d = strtol (cp, &cp2, 10);
      cp = cp2;
    }
  else
    d = 0;

  if (*cp)
    {
      if (!(flags & FMTP_SUPPRESS_ERRORS))
        msg (SE, _("Data format %s is not valid."), ds_c_str (&tokstr));
      return 0;
    }
  lex_get ();

  spec.type = type;
  spec.w = w;
  spec.d = d;
  *input = spec;

  return 1;
}

