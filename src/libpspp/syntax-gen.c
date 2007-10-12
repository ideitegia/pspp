/* PSPP - a program for statistical analysis.
   Copyright (C) 2007 Free Software Foundation, Inc.

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
#include <stdbool.h>
#include <stdint.h>

#include <libpspp/str.h>
#include <libpspp/assertion.h>
#include <string.h>
#include <mbchar.h>

#include "syntax-gen.h"


static bool
has_single_quote (const struct string *str)
{
  return (SIZE_MAX != ds_find_char (str, '\''));
}

static bool
has_double_quote (const struct string *str)
{
  return (SIZE_MAX != ds_find_char (str, '"'));
}

/*
   Quotes the string STR. If STR contains no '\'' character, then
   the returned value will be enclosed in single quotes.  Else, if STR
   contains no '"' character, then it will be enclosed in double
   quotes. Otherwise, it will be enclosed in single quotes, and each
   '\'' will be padded with another '\''.

   STR must be encoded in UTF-8, and the quoted result will also be
   encoded in UTF-8.
*/
void
gen_quoted_string (struct string *str)
{
  char c;
  static const char single_quote[] = "'";
  static const char double_quote[] = "\"";

  struct string quoted_str;

  bool pad_single_quotes = false;
  const char *delimiter ;
  char *s = ds_cstr (str);

  if ( has_double_quote (str))
    {
      delimiter = single_quote;
      if ( has_single_quote (str))
	pad_single_quotes = true;
    }
  else
    {
      delimiter = double_quote;
    }

  /* This seemingly simple implementation is possible, because UTF-8
     guarantees that bytes corresponding to basic characters (such as
     '\'') cannot appear in a multi-byte character sequence except to
     represent that basic character.
  */
  assert (is_basic ('\''));

  /* Initialise with the opening delimiter */
  ds_init_cstr (&quoted_str, delimiter);
  while ((c = *s++))
    {
      ds_put_char (&quoted_str, c);

      /* If c is a single quote, then append another one */
      if ( c == '\'' && pad_single_quotes)
	ds_put_char (&quoted_str, c);
    }

  /* Add the closing delimiter */
  ds_put_cstr (&quoted_str, delimiter);

  /* Copy the quoted string into str */
  ds_swap (str, &quoted_str);
  ds_destroy (&quoted_str);
}

