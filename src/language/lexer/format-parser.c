/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2010 Free Software Foundation, Inc.

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

#include "format-parser.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>

#include "lexer.h"
#include <data/format.h>
#include <data/variable.h>
#include <language/lexer/format-parser.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

static bool
parse_abstract_format_specifier__ (struct lexer *lexer,
                                   char type[FMT_TYPE_LEN_MAX + 1],
                                   int *width, int *decimals)
{
  struct substring s;
  struct substring type_ss, width_ss, decimals_ss;
  bool has_decimals;

  if (lex_token (lexer) != T_ID)
    goto error;

  /* Extract pieces. */
  s = ds_ss (lex_tokstr (lexer));
  ss_get_bytes (&s, ss_span (s, ss_cstr (CC_LETTERS)), &type_ss);
  ss_get_bytes (&s, ss_span (s, ss_cstr (CC_DIGITS)), &width_ss);
  if (ss_match_byte (&s, '.'))
    {
      has_decimals = true;
      ss_get_bytes (&s, ss_span (s, ss_cstr (CC_DIGITS)), &decimals_ss);
    }
  else
    has_decimals = false;

  /* Check pieces. */
  if (ss_is_empty (type_ss) || ss_length (type_ss) > FMT_TYPE_LEN_MAX)
    goto error;
  if (has_decimals && ss_is_empty (decimals_ss))
    goto error;
  if (!ss_is_empty (s))
    goto error;

  /* Return pieces.
     These uses of strtol are valid only because we know that
     their substrings are followed by non-digit characters. */
  str_copy_buf_trunc (type, FMT_TYPE_LEN_MAX + 1,
                      ss_data (type_ss), ss_length (type_ss));
  *width = strtol (ss_data (width_ss), NULL, 10);
  *decimals = has_decimals ? strtol (ss_data (decimals_ss), NULL, 10) : 0;

  return true;

error:
  lex_error (lexer, _("expecting valid format specifier"));
  return false;
}

/* Parses a token taking the form of a format specifier and
   returns true only if successful.  Emits an error message on
   failure.  Stores a null-terminated string representing the
   format type in TYPE, and the width and number of decimal
   places in *WIDTH and *DECIMALS.

   TYPE is not checked as to whether it is really the name of a
   format.  Both width and decimals are considered optional.  If
   missing, *WIDTH or *DECIMALS or both will be set to 0. */
bool
parse_abstract_format_specifier (struct lexer *lexer,
                                 char type[FMT_TYPE_LEN_MAX + 1],
                                 int *width, int *decimals)
{
  bool ok = parse_abstract_format_specifier__ (lexer, type, width, decimals);
  if (ok)
    lex_get (lexer);
  return ok;
}

/* Parses a format specifier from the token stream and returns
   true only if successful.  Emits an error message on
   failure.  The caller should call check_input_specifier() or
   check_output_specifier() on the parsed format as
   necessary.  */
bool
parse_format_specifier (struct lexer *lexer, struct fmt_spec *format)
{
  char type[FMT_TYPE_LEN_MAX + 1];

  if (!parse_abstract_format_specifier__ (lexer, type, &format->w, &format->d))
    return false;

  if (!fmt_from_name (type, &format->type))
    {
      msg (SE, _("Unknown format type `%s'."), type);
      return false;
    }

  lex_get (lexer);
  return true;
}

/* Parses a token containing just the name of a format type and
   returns true if successful. */
bool
parse_format_specifier_name (struct lexer *lexer, enum fmt_type *type)
{
  if (lex_token (lexer) != T_ID)
    {
      lex_error (lexer, _("expecting format type"));
      return false;
    }
  if (!fmt_from_name (ds_cstr (lex_tokstr (lexer)), type))
    {
      msg (SE, _("Unknown format type `%s'."), ds_cstr (lex_tokstr (lexer)));
      return false;
    }
  lex_get (lexer);
  return true;
}
